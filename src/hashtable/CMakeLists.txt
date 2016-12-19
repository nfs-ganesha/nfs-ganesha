########### next target ###############

SET(hashtable_STAT_SRCS
   hashtable.c
)

add_library(hashtable STATIC ${hashtable_STAT_SRCS})
add_sanitizers(hashtable)


########### install files ###############
