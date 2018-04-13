# Turn on everything in the options for a complete build

set(PROXY_HANDLE_MAPPING ON)
set(USE_DBUS ON)
set(USE_CB_SIMULATOR ON)
set(USE_FSAL_XFS ON)
set(USE_FSAL_CEPH ON)
set(USE_FSAL_RGW ON)
set(USE_FSAL_GLUSTER ON)
set(USE_TOOL_MULTILOCK ON)

message(STATUS "Building everything")
