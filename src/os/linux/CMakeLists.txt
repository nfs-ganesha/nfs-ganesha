include_directories(
)

########### next target ###############

SET(gos_STAT_SRCS
    mntent.c
)

add_library(gos STATIC ${gos_STAT_SRCS})


########### install files ###############
