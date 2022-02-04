Building and using LTTng an enabled server
==========================================

LTTng is a big topic.  Consult the online documentation for the
details of running the tools. The development is being done using Fedora
using their packages, version 2.3.0.

Install the following RPMs on Fedora or, if on RHEL, use the EPEL repo.
There are other lttng related packages but these are the relevant ones
for this level.

lttng-tools
lttng-ust-devel
lttng-ust
babeltrace

Build with `-DUSE_LTTNG=ON` to enable it.  All of the tracepoints
have an `#ifdev USE_LTTNG` around them so the default of 'OFF'
creates no build or runtime dependencies on lttng.

The build creates and installs an additional shared object.  The
`libganesha_trace.so` object which contains all the defined tracepoint
code and the code in the application that uses it are constructed such
that the tracepoint code does not require the shared object to be
loaded.  This means that the application can be built with LTTng code
but it can be run on a system that has no LTTng support.  Tracepoints are
enabled once the module is loaded.  See below for details.

I use the following script to run:
```
#!/bin/sh

DIR=/usr/local/lib64/ganesha

lttng create
lttng enable-event -u -a
lttng start

LD_PRELOAD=$DIR/libganesha_trace.so /usr/local/bin/ganesha.nfsd ${*}
```
In this case, I am preloading `libganesha_trace.so` which turns on tracing before
before the server starts.  If you do not preload, ganesha runs as if tracing is
not there and the only overhead is the predicted missed branch to the tracer.
LTTng supports the loading of the tracing module into a running application by
loading (dlopen) the module  This would be useful for production environments
but that feature is on the TODO list.

There are never enough tracepoints.  Like log messages, they get added from time
to time as needed.  There are two paths for adding tracepoints.

- Sets of tracepoints are categorized in *components*.  These should conform
  to the same general categories as logging components.  All the tracepoints
  in a component should be functionally related in some way.

- Finer grained tracing adds new tracepoints to an existing category.

Creating a New Component
------------------------
Two files must be created and a number of other files must be edited to create
a new tracepoint component.

The tracepoint itself is created as a new include (.h) file in
`src/include/gsh_lttng`.  This is the bulk of the work.  Note that the
file has a specific form.  It is best to copy and edit an existing file so as
to get all the necessary definitions in the right places.  There can be any
number of tracepoint definitions but each one *must* have a `TRACEPOINT_EVENT`
and `TRACEPOINT_LOGLEVEL` defined (after it).

The tracepoint include file has a companion file in src/tracing that includes
the header after defining `TRACEPOINT_CREATE_PROBES`.  Add this file to the
sources list in tracing/CMakeLists.txt so that it gets built into the tracing
module.

Every source file that will use these tracepoints must also include the specific
include file(s) for the tracepoint component(s).  Note that components are
defined separately.  Both the #include and the tracepoints themselves should be
wrapped with an `#ifdef USE_LTTNG`.

The CMakeLists.txt file of a source sub-directory should be edited to add a
conditional `include_directories` command so that `LTTNG_INCLUDE_DIR` will added
to the includes path.

The last step in adding a component is to add an `#include` of the new header
in `MainNFSD/nfs_main.c`.  This bit is LTTng magic to set up the dynamic linkage.
Every component header file is listed here ONLY ONCE.  This includes any
tracepoints that may be added to `nfs_main.c`.

Adding New Tracepoints
----------------------
Adding a new tracepoint to an existing component is really simple.  First,
add the `TRACEPOINT_EVENT` and its `TRACEPOINT_LOGLEVEL` to the component's
header file.  This is all that is necessary to create the tracepoint.  The
next step is to add the new tracepoint into the code.  There are a few extra
bits to remember in adding the tracepoint.

- If the tracepoint is added to a file in a subdirectory that had no
  tracepoints before, add the `include_directories` directive to the
  CMakeLists.txt file.

- If the tracepoint category is new to the source file, add a `#include`
  for it.

- Wrap the #include and all tracepoints with `USE_LTTNG`.

Notes on Using Tracepoints
==========================
All this trace point organization is for the "enable-event" command above.
The `-a` turns them all on.  If all you wanted was logging, you would change
the `-a` to `ganesha_logger:log`.  See the LTTng docs for the details.

Tracepoint logs are placed in your `$HOME/lttng-traces` directory.  Note that
if you are running as 'root' which is necessary for running nfs-ganesha, this
directory is actually `/root/lttng-traces`, not your regular `$HOME`.  The lttng
commands will report the subdirectory where the traces are placed.  You can
also specify a directory name if you like.  If the lttng command reports
`auto-20140804-102010` as the trace directory, you can find those files in
`$HOME/lttng-traces/auto-20140804-102010`.

The `babeltrace` tool is used to process and display traces.  There are others
but this is the simplest and most general tool.  To do a simple dump of
the trace from above, use:
```
$ babeltrace $HOME/lttng-traces/auto-20140804-102010
```
This will dump a trace in text form.  See the man page for all the options.
There are a number of other tools that can also munch traces.  Traces
are in a common format that many tools can read and process/display them.
