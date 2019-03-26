
########### next target ###############

SET(rquota_STAT_SRCS
   rquota_Null.c
   rquota_getquota.c
   rquota_getactivequota.c
   rquota_setquota.c
   rquota_setactivequota.c
   rquota_common.c
)

add_library(rquota OBJECT ${rquota_STAT_SRCS})
add_sanitizers(rquota)
set_target_properties(rquota PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############
