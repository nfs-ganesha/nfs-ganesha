# All we need to do here is control the
# build of chosen platform

if(FREEBSD)
  if(USE_FSAL_PANFS)
      add_definitions(-D__PanFS__)
  endif(USE_FSAL_PANFS)
  SET(fsal_os_STAT_SRCS
      freebsd/handle_syscalls.c
  )
endif(FREEBSD)

if(LINUX)
  SET(fsal_os_STAT_SRCS
      linux/handle_syscalls.c
  )
endif(LINUX)

add_library(fsal_os STATIC ${fsal_os_STAT_SRCS})
add_sanitizers(fsal_os)

########### install files ###############

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(fsal_os PROPERTIES COMPILE_FLAGS "-fPIC")
