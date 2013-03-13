
########### next target ###############

SET(hpssapiext_STAT_SRCS
   api_fgetattr.c
   api_fsetattr.c
   api_mkdir.c
   api_rddir.c
   api_symlink.c
   api_open.c
)

add_library(hpssapiext SHARED ${hpssapiext_STAT_SRCS})


########### install files ###############

