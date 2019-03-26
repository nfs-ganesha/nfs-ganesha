# All we need to do here is control the
# build of chosen platform

if(FREEBSD)
  SET(gos_STAT_SRCS
      freebsd/atsyscalls.c
      freebsd/mntent_compat.c
      freebsd/subr.c
      freebsd/xattr.c
  )
endif(FREEBSD)

if(LINUX)
  SET(gos_STAT_SRCS
      linux/subr.c
  )
endif(LINUX)

add_library(gos OBJECT ${gos_STAT_SRCS})
add_sanitizers(gos)
set_target_properties(gos PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
