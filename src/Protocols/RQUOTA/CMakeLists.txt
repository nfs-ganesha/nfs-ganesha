
########### next target ###############

SET(rquota_STAT_SRCS
   rquota_Null.c
   rquota_getquota.c
   rquota_getactivequota.c
   rquota_setquota.c
   rquota_setactivequota.c
   rquota_common.c
)

add_library(rquota STATIC ${rquota_STAT_SRCS})
add_sanitizers(rquota)

target_link_libraries(rquota
  string_utils
)

########### install files ###############
