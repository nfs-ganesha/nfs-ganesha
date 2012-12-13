# All we need to do here is control the
# build of chosen platform

if(FREEBSD)
  add_subdirectory(freebsd)
endif(FREEBSD)

if(LINUX)
  add_subdirectory(linux)
endif(LINUX)
