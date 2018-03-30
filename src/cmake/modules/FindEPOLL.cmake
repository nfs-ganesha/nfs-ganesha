# - Find EPOLL
#
# This module defines the following variables:
#    EPOLL_FOUND       = Was EPOLL found or not?
#
# On can set EPOLL_PATH_HINT before using find_package(EPOLL) and the
# module with use the PATH as a hint to find EPOLL.
#
# The hint can be given on the command line too:
#   cmake -DEPOLL_PATH_HINT=/DATA/ERIC/EPOLL /path/to/source

# epoll is emulated on FreeBSD
if (FREEBSD)
    set (EPOLL_FOUND ON)
    return ()
endif (FREEBSD)

include(CheckIncludeFiles)
include(CheckFunctionExists)

check_include_files("sys/epoll.h" EPOLL_HEADER)
check_function_exists(epoll_create EPOLL_FUNC)

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(EPOLL REQUIRED_VARS EPOLL_HEADER EPOLL_FUNC)
