# this shouldn't need to be here...
include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(hashtable_STAT_SRCS
   HashTable.c
)

add_library(hashtable STATIC ${hashtable_STAT_SRCS})


########### install files ###############
