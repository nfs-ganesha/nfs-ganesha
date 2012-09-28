CMAKE Build Instructions
------------------------

This is preliminary and by no means exhaustive information.  The port
is new and there are most likely missing pieces.

The autotools files are still in place and useable.  They will not be
removed until the cmake environment is settled enough for all the
developers can use it.

*** So what is the difference? ***

The biggest difference is that Cmake is an integrated tool rather than
a collection of script/text munchers and some script snippets for them
to munch on.  As such, one gets a readable source to look at and
intelligible error messages (with line numbers!) when things break.

The second difference is that Cmake can and does prefer to build out-
of-source.  In other words, your build tree is over here and your
git source tree is over there.  The Makefiles are created by Cmake in the
build tree, the objects and targets are in the build tree but the source
is referenced "over there".  For example, in a Ganesha build, we would do:

   $ cd some-build-sandbox
   $ rm -rf build_dir; mkdir build_dir
   $ cd build_dir
   $ cmake ~/git/nfs-ganesha/src
   $ make
   $ make install

This gets the build completely away from where the git repo is. Note
that I have thoroughly scrubbed the area before doing the build.  You can
also build in-tree but this litters the git repo with extra files just like
autotools.  See the Cmake manual for the details and restrictions.

Building is a two step process.  You first run Cmake to configure the build
and then you do a conventional Make.  You can do iterative development by
editing files, including Cmake files (CMakeLists.txt) in the source tree
and go back to the build directory and do a "make".  The makefile will do the
right thing and re-run Cmake if you messed with configuration files.  Your
configuration and build parameters are preserved in the build tree so you
only have to do the full configuration step once.

Unlike autotools where the build and source are in the same tree, having a
separate build area allows you to do a couple of thing safely.

  * You can delete the whole build tree at any time.  Simply repeat the
    configuration step and you get it all back.  Your source is safely
    somewhere else.  Be aware of which window/terminal you are in before
    doing an "rm -rf" however.  Yes, I did that once so now I have the
    windows on separate monitors...

  * You can easily build multiple configurations.  Simply create one build
    directory, enter it, and run Cmake with one set of parameters.  Repeat
    in another build directory with a different set of parameters.  Nice.

TIRPC build integration
-----------------------
This brings up another item of interest in a Ganesha build.  Rather than
use the tirpc.sh script to get the correct library build, the top level
Cmake file (src/CMakeLists.txt) has a hard coded variable with the correct
git commit.  The Cmake step sets this "external project" up and the first
build clones the tirpc repo and checks out the correct commit.

   NOTE: Cmake does not do dependency analysis inside an external project
   so if you make changes inside tirpc, you have to run an appropriate 
   build there to have the changes take effect.  There is a "stamp" file
   inside the libtirpc build area for those intrepid enough to remove it
   to force a re-build.

The top level Makefile is smart enough to do the clone and build only once.

Configuration Tweaking
----------------------
Cmake allows the setting of configuration parameters from the command line.
You would use this in a similar way to how autotools works.

You can discover what you can tweak by doing the following:

   $ mkdir tweaktest; cd tweaktest
   $ cmake -i ~/git/nfs-ganesha/src

This will enter you into a "wizard" configuration (no fancy GUI stuff).
Simply step through the configuration and note what knobs and switches
are available and what their defaults are.  After this, you can explicitly
change parameters from the command line.  For example:

   $ mkdir mybuild; cd mybuild
   $ cmake -D_USE_9P=OFF -D_HANDLE_MAPPING=ON -DALLOCATOR=tcmalloc \
     ~/git/nfs-ganesha/src

This will disable a 9P build, use handle mapping in the PROXY fsal and
pick the tcmalloc allocator.

There are two other variables of interest:

CMAKE_BUILD_TYPE
   This is a global setting for the type of build.  See the Cmake documentation
   for what this means.  I have added a "Maintainer" type which forces strict
   compiles.  It is what I intend to use on builds.

BUILD_CONFIG
   This setting triggers the loading of a file in src/cmake/build_configurations.
   This is useful for having a "canned" configuration.  There is only one
   file currently in use which will turn on every option available.

Put these together and you get the build I use for merge testing:

  $ cmake -DCMAKE_BUILD_TYPE=Maintainer -DBUILD_CONFIG=everything \
    ~/git/nfs-ganesha/src

Look at src/cmake/build_configuration/everything.cmake to see what this turns
on.  If you want a custom, for say just a 9P server or only some features,
create a file on the model of everything.cmake in that directory and then
reference it on the command line.  This eliminates the various shell scripts
we have laying around...  I stole this from the Mysql build where they use
this trick to have things like 'redhat.cmake' and 'debian.cmake'.

Housekeeping
------------
There are a few bits in the src/CMakeLists.txt file that need to be maintained
during development.  These are located at the top of the file.

The project major, minor, and patchlevel values must reflect the release tag
in the repository.  The top level commit that any release tag points to
should contain a patch to change these values.

The "tirpc_commit" variable contains the correct TIRPC commit for a build.
Whenever TIRPC is updated, there should be a commit to update this value.
This is functionally equivalent to an update to the tirpc.sh script.

Caveats and Status
------------------
The Cmake port builds a functioning server that tests identically with one
built via autotools using the same configuration parameters.  This does not
mean that it is either complete or bug free.  There are some areas that 
require further work.

* The configure.ac file is full of magic.  I'm not sure I got it all in
  this pass.  Some of it is old dead stuff, some of it hasn't tripped me
  up yet.

* One of the nice "features" of Cmake is that it insists that you name
  the dependencies for archive libraries.  This has exposed just how
  messy some directories are, the biggest culprit being src/support.
  Once autotools is history, we should make a reordering pass and move
  files to more appropriate places.  There are a few libraries that
  have only one file in them and others have mixes of all sorts of
  stuff.

* Some directories such as src/rpm and src/debian are untouched.  Cmake
  has tools for building packages for those distros so fixing them should
  be easier.  I've done nothing as yet for the the documentation or
  scripts/configs directories either.

* We need to install the fsal plugins properly.  Right now they just get
  dumped into /usr/local/lib as libraries.  They really should use the
  O/S standard for application specific modules.

Any changes that would require retro-fitting to autotools will be postponed
until autotools is gone.  After all, the whole point of doing the Cmake
port was to not have to hack autotools anymore.

Jim
