
########### next target ###############

SET(nfsacl_STAT_SRCS
   nfsacl_Null.c
   nfsacl_getacl.c
   nfsacl_setacl.c
)

add_library(nfsacl OBJECT ${nfsacl_STAT_SRCS})
add_sanitizers(nfsacl)
set_target_properties(nfsacl PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
