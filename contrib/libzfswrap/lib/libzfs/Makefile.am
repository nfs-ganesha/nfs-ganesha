noinst_LTLIBRARIES = libzfs.la
libzfs_la_SOURCES = libzfs.c \
                    libzfs_changelist.c \
                    libzfs_config.c \
                    libzfs_dataset.c \
                    libzfs_graph.c \
                    libzfs_import.c \
                    libzfs_mount.c \
                    libzfs_pool.c \
                    libzfs_sendrecv.c \
                    libzfs_status.c \
                    libzfs_util.c \
                    libzfs_zfsfuse.c

libzfs_la_CFLAGS = -I${top_srcdir}/lib/libzfs/include \
                   -I${top_srcdir}/lib/libavl/include \
                   -I${top_srcdir}/lib/libnvpair/include \
                   -I${top_srcdir}/lib/libumem/include \
                   -I${top_srcdir}/lib/libzfscommon/include \
                   -I${top_srcdir}/lib/libzpool/include \
                   -I${top_srcdir}/lib/libuutil/include \
                   -I${top_srcdir}/lib/libsolcompat/include \
                   -I${top_srcdir}/lib/libzfs/include \
                   @DEBUG_CFLAGS@
AM_LDFLAGS = -lcrypto

noinst_HEADERS = include/acl.h \
                 include/acl_impl.h \
                 include/libzfs.h \
                 include/libzfs_impl.h \
                 include/zfsfuse.h
