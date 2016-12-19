
########### next target ###############

SET(avltree_STAT_SRCS
   avl.c
   bst.c
   rb.c
   splay.c
)

add_library(avltree STATIC ${avltree_STAT_SRCS})
add_sanitizers(avltree)

########### install files ###############
