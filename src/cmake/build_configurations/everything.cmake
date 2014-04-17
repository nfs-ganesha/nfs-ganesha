# Turn on everything in the options for a complete build

#set(USE_TIRPC_IPV6 ON) # exports.c is broken here...
set(PROXY_HANDLE_MAPPING ON)
set(_NO_XATTRD OFF)
set(USE_DBUS ON)
set(USE_CB_SIMULATOR ON)

message(STATUS "Building everything")
