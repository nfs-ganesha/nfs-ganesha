include_directories(
)

########### next target ###############

SET(gos_STAT_SRCS
    atsyscalls.c
    mntent_compat.c
    xattr.c
)

add_library(gos STATIC ${gos_STAT_SRCS})


########### install files ###############
