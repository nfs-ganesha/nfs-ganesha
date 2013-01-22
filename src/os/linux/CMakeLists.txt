add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

include_directories(
)

########### next target ###############

SET(gos_STAT_SRCS
    mntent.c
    subr.c
)

add_library(gos STATIC ${gos_STAT_SRCS})


########### install files ###############

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(gos PROPERTIES COMPILE_FLAGS "-fPIC")
