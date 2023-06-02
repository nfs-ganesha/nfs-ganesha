# ganesha-container - A script for creating containers for NFS Ganesha testing

## Motivation

It can be hard to know if changes, particularly build system changes,
will affect platforms other than a developer's main development
platform.

This script generates containers for a wide variety of Linux
distributions.  They include many dependencies required for building
NFS Ganesha.  The number of Linux distributions and the dependencies
can be improved over time.

The current directory is mounted into the container and used as the
container's working directory.  This allows at least the following use
cases:

* The current directory is a parent of the NFS Ganesha git repository,
  with build directories alongside.  This allows those existing build
  directories to be used within the container.  Build artifacts can
  then be examined outside the container.

* The current directory is the NFS Ganesha git repository.  This
  allows a build directory to be created in the container user's home
  directory, so it disappears when the container is stopped.

## Examples

### Run interactive Fedora 38 container

    $ ganesha-container fedora 38
    [user@fedora38 nfs-ganesha]$ grep PRETTY_NAME /etc/os-release
    PRETTY_NAME="Fedora Linux 38 (Container Image)"
    [user@fedora38 nfs-ganesha]$ <control-d>
    exit
    $

Interactive containers are removed upon exit from the container's
shell.

### Run interactive Debian (default version) container

    $ ganesha-container debian
    user@debian11:/home/martins$ grep PRETTY_NAME /etc/os-release
    PRETTY_NAME="Debian GNU/Linux 11 (bullseye)"
    user@debian11:/tmp$ exit
    exit
    $

### Run a build test in all supported containers

    $ cmake_cmd="cmake -DUSE_FSAL_VFS=ON -DUSE_DBUS=ON -DENABLE_VFS_POSIX_ACL=YES -DUSE_ADMIN_TOOLS=YES ../nfs-ganesha/src"
    $ ganesha-container all sh -c "rm -rf build/* && cd build && ${cmake_cmd} && make -j" 2>&1 | tee build.wip
    =======================================
    almalinux 8 (Tue 06 Jun 2023 08:56:51 AEST)
    CMake Deprecation Warning at CMakeLists.txt:26 (cmake_minimum_required):
    [...]

The output from each container begins with a header indicating the
distribution, version and current time (via the date(1) command).
Given that the date always changes, the date will always appear in
diffs.  So, this makes it useful for comparing output from runs for
different branches (e.g. next, wip).

### Generate all supported container images

    $ ganesha-container all </dev/null
    ========================================
    almalinux 8 (Tue 06 Jun 2023 09:11:20 AEST)
    [...]

This is useful for pre-generating all container images so the output
from container image generation doesn't pollute the output of builds.
