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
set_target_properties(fsal_os PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
