AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = lib9p.la

lib9p_la_SOURCES = 9p_interpretor.c  \
                   9p_fid.c          \
                   9p_proto_tools.c  \
                   9p_read_conf.c    \
                   9p_attach.c       \
                   9p_version.c      \
                   9p_rerror.c       \
                  ../../include/9p.h

EXTRA_DIST = 9P_Structures.txt 

new: clean all
