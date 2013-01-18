include_directories(
)

########### next target ###############

SET(gos_STAT_SRCS
    atsyscalls.c
    mntent_compat.c
    subr.c
    xattr.c
)

add_library(gos STATIC ${gos_STAT_SRCS})

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(gos PROPERTIES COMPILE_FLAGS "-ggdb -fPIC")

########### install files ###############
