AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = lib9p.la

lib9p_la_SOURCES = 9p_interpretor.c  \
                   9p_proto_tools.c  \
                   9p_read_conf.c    \
                   9p_attach.c       \
                   9p_clunk.c        \
                   9p_create.c       \
                   9p_flush.c        \
                   9p_getattr.c      \
                   9p_lopen.c        \
                   9p_link.c         \
                   9p_mkdir.c        \
                   9p_mknod.c        \
                   9p_read.c         \
                   9p_readdir.c      \
                   9p_readlink.c     \
                   9p_remove.c       \
                   9p_rename.c       \
                   9p_renameat.c     \
                   9p_unlinkat.c     \
                   9p_setattr.c      \
                   9p_statfs.c       \
                   9p_symlink.c      \
                   9p_version.c      \
                   9p_walk.c         \
                   9p_write.c        \
                   9p_rerror.c       \
                  ../../include/9p.h

EXTRA_DIST = 9P_Structures.txt 

new: clean all
