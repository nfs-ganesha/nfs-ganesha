########### next target ###############

SET(hashtable_STAT_SRCS
   hashtable.c
)

add_library(hashtable OBJECT ${hashtable_STAT_SRCS})
add_sanitizers(hashtable)
set_target_properties(hashtable PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
