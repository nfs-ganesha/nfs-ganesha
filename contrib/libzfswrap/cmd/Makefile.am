bin_PROGRAMS = lzw_zfs lzw_zpool

lzw_zfs_SOURCES = lzw_zfs.c
lzw_zfs_LDADD = ../zfswrap/libzfswrap.la
lzw_zfs_CFLAGS = -I${top_srcdir}/zfswrap/
lzw_zfs_LDFLAGS = -L${top_builddir}/zfswrap -lzfswrap -static

lzw_zpool_SOURCES = lzw_zpool.c
lzw_zpool_LDADD = ../zfswrap/libzfswrap.la
lzw_zpool_CFLAGS = -I${top_srcdir}/zfswrap/
lzw_zpool_LDFLAGS = -L${top_builddir}/zfswrap -lzfswrap -static
