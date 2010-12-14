noinst_LTLIBRARIES = libnvpair-user.la libnvpair-kernel.la

libnvpair_user_la_SOURCES = libnvpair.c nvpair.c nvpair_alloc_fixed.c nvpair_alloc_system.c
libnvpair_user_la_CFLAGS = -I${top_srcdir}/lib/libnvpair/include \
                           -I${top_srcdir}/lib/libsolcompat/include \
                           @DEBUG_CFLAGS@

libnvpair_kernel_la_SOURCES = libnvpair.c nvpair.c nvpair_alloc_fixed.c nvpair_alloc_system.c
libnvpair_kernel_la_CFLAGS = -I${top_srcdir}/lib/libnvpair/include \
                             -I${top_srcdir}/lib/libumem/include \
                             -I${top_srcdir}/lib/libnvpair/include1 \
                             -I${top_srcdir}/lib/libsolcompat/include \
                             -D_KERNEL @DEBUG_CFLAGS@

noinst_HEADERS = include/sys/nvpair.h \
                 include/sys/nvpair_impl.h \
                 include/libnvpair.h \
                 include1/sys/kmem.h
