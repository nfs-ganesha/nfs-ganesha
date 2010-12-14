noinst_LTLIBRARIES = libuutil.la
libuutil_la_SOURCES = uu_alloc.c uu_avl.c uu_dprintf.c uu_ident.c uu_list.c uu_misc.c uu_open.c uu_pname.c uu_strtoint.c

AM_CFLAGS = -I${top_srcdir}/lib/libuutil/include \
            -I${top_srcdir}/lib/libavl/include \
            -I${top_srcdir}/lib/libsolcompat/include \
            -DNATIVE_BUILD @DEBUG_CFLAGS@

noinst_HEADERS = include/libuutil_common.h \
                 include/libuutil.h \
                 include/libuutil_impl.h
