
########### next target ###############

SET(hpssapiext_STAT_SRCS
   api_fgetattr.c
   api_fsetattr.c
   api_mkdir.c
   api_rddir.c
   api_symlink.c
   api_open.c
   hpssclapiext.h
)

add_library(hpssapiext STATIC ${hpssapiext_STAT_SRCS})


########### install files ###############




#original Makefile.am contents follow:

#AM_CFLAGS=$(FSAL_CFLAGS) $(SEC_CFLAGS)
#
#noinst_LTLIBRARIES          = libhpssapiext.la 
#
#libhpssapiext_la_SOURCES    = api_fgetattr.c  api_fsetattr.c  api_mkdir.c  api_rddir.c  api_symlink.c  api_open.c hpssclapiext.h
