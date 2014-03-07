
########### next target ###############

SET(NodeList_STAT_SRCS
   nodelist_internals.h
   nodelist_common.c
   nodelist.c
   nodelist_range.c
   nodelist_map.c
)

add_library(NodeList STATIC ${NodeList_STAT_SRCS})


########### install files ###############
