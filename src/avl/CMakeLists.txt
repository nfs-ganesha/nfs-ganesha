
########### next target ###############

SET(avltree_STAT_SRCS
   avl.c
   bst.c
   rb.c
   splay.c
)

add_library(avltree OBJECT ${avltree_STAT_SRCS})
add_sanitizers(avltree)
set_target_properties(avltree PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
