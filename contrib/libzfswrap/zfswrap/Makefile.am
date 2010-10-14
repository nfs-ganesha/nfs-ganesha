lib_LTLIBRARIES = libzfswrap.la

library_includedir=$(includedir)
library_include_HEADERS = libzfswrap.h

libzfswrap_la_SOURCES = ptrace.c zfs_acl.c zfs_dir.c zfs_ioctl.c zfs_log.c zfs_replay.c zfs_rlock.c zfs_vfsops.c zfs_vnops.c zvol.c zfsfuse_socket.c libzfswrap_utils.c libzfswrap.c
libzfswrap_la_LIBADD = ${top_builddir}/lib/libatomic/libatomic.la \
                       ${top_builddir}/lib/libzpool/libzpool-kernel.la \
                       ${top_builddir}/lib/libnvpair/libnvpair-kernel.la \
                       ${top_builddir}/lib/libumem/libumem.la \
                       ${top_builddir}/lib/libsolkerncompat/libsolkerncompat.la \
                       ${top_builddir}/lib/libzfs/libzfs.la \
                       ${top_builddir}/lib/libavl/libavl.la \
                       ${top_builddir}/lib/libuutil/libuutil.la \
                       ${top_builddir}/lib/libsolcompat/libsolcompat.la \
                       ${top_builddir}/lib/libzfscommon/libzfscommon-user.la
libzfswrap_la_CFLAGS = -I${top_srcdir}/lib/libavl/include \
                       -I${top_srcdir}/lib/libnvpair/include \
                       -I${top_srcdir}/lib/libumem/include \
                       -I${top_srcdir}/lib/libzfs/include \
                       -I${top_srcdir}/lib/libuutil/include \
                       -I${top_srcdir}/lib/libzfscommon/include \
                       -I${top_srcdir}/lib/libsolkerncompat/include \
                       -I${top_srcdir}/lib/libsolcompat/include \
                       -D_KERNEL @DEBUG_CFLAGS@
AM_LDFLAGS = -lrt -lpthread -ldl -lz -laio -lcrypto -lm

noinst_HEADERS = kmem_asprintf.h \
                 zfs_ioctl.h \
                 zfsfuse_socket.h \
                 libzfswrap_utils.h

doc:
	doxygen

.PHONY: doc
