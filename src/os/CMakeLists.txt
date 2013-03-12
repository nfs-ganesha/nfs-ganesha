# All we need to do here is control the
# build of chosen platform

include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

if(FREEBSD)
  SET(gos_STAT_SRCS
      freebsd/atsyscalls.c
      freebsd/mntent_compat.c
      freebsd/subr.c
      freebsd/xattr.c
      freebsd/memstream.c
  )
endif(FREEBSD)

if(LINUX)
  SET(gos_STAT_SRCS
      linux/mntent.c
      linux/subr.c
  )
endif(LINUX)

add_library(gos STATIC ${gos_STAT_SRCS})

########### install files ###############

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(gos PROPERTIES COMPILE_FLAGS "-fPIC")
