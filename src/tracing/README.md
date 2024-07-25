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
have an `#ifdef USE_LTTNG` around them so the default of 'OFF'
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

/usr/local/bin/ganesha.nfsd -G ${*}
```
The `-G` flag tells ganesha to load `libganesha_trace.so` and
`libntirpc_tracepoints.so`, which turns on tracing before
before the server starts.  If you don't use `-G`, ganesha runs as if tracing is
not there and the only overhead is the predicted missed branch to the tracer.

There are never enough tracepoints.  Like log messages, they get added from time
to time as needed.  There are two paths for adding tracepoints.

- Sets of tracepoints are categorized in *components* (or providers in
  the LTTNG phrasing).  These should conform to the same general categories as
  logging components. All the tracepoints in a component should be functionally
  related in some way.

- Finer grained tracing adds new tracepoints to an existing category.

LTTNG traces automation
-----------------------
Ganesha uses LTTNG trace generator, which is an infrastructure to automatically
generate all the boilerplate code for a tracepoint and adds a format string.
With this, the user can just include the header file and add the tracepoint.

For more information, see the trace generator README in:
```
src/libntirpc/src/lttng/generator/README.md
```

Adding a new tracepoint
----------------------
In order to add a tracepoint in a file, the file must include
`gsh_lttng/gsh_lttng.h`, which provides the utility functions to add an
auto-generated tracepoint.

In addition, you should include the auto-generated header for the specific
provider (or component you want to use). This file is auto-generated, so it
will not exist usually, and it should be within an `#ifdef`, as follows:
```
#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/<provider>.h"
#endif
```

Then, a tracepoint can be defined anywhere in the code in this way:
```
GSH_AUTO_TRACEPOINT(<provider>, <event_name>, <debug_level>,
			<format_string>, args...);
```
for example:
```
#include "gsh_lttng/gsh_lttng.h"
#if defined(USE_LTTNG) && !defined(LTTNG_PARSING)
#include "gsh_lttng/generated_traces/nfs.h"
#endif

int arg = 1;
GSH_AUTO_TRACEPOINT(nfs, test, TRACE_DEBUG, "Test tracepoint with arg: {}",
  arg);
```

Note that the event name must be unique. If you want to use the same event name
in more than one place, or in a macro that can be called from several places,
use `GSH_UNIQUE_AUTO_TRACEPOINT`. This will add a unique suffix to the event
name automatically.

Note that all the relevant headers and tacepoint calls are wrapped with
`#ifdef USE_LTTNG`, so adding traces in this way doesn't have any impact when
building without LTTNG.

Notes on Using Tracepoints
==========================
All this trace point organization is for the "enable-event" command above.
The `-a` turns them all on.  See the LTTng docs for the details on how to filter
or turn on specific components.

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

In addition, instead of using babeltrace, you can use
`src/libntirpc/src/lttng/generator/trace_formatter.py` which will format the
traces and show them in a pretty way with the format string.
