# All we need to do here is control the
# build of chosen platform

include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

if(FREEBSD)
  add_subdirectory(freebsd)
endif(FREEBSD)

if(LINUX)
  add_subdirectory(linux)
endif(LINUX)
