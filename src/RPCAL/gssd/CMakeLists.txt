
########### next target ###############

SET(gssd_STAT_SRCS
   context.c
   context_mit.c
   context_heimdal.c
   context_lucid.c
   context_spkm3.c
   gss_util.c
   gss_oids.c
   err_util.c
   context.h
   err_util.h
   gss_oids.h
   gss_util.h
   krb5_util.c
   gssd.c
   gssd.h
   krb5_util.h
   write_bytes.h
)

add_library(gssd STATIC ${gssd_STAT_SRCS})


########### install files ###############
