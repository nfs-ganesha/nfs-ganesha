noinst_LTLIBRARIES = libatomic.la
libatomic_la_SOURCES = @ARCH@/atomic.S

AM_CFLAGS = -I${top_srcdir}/lib/libatomic/include \
            -I${top_srcdir}/lib/libatomic/include/@ARCH@ \
            -I${top_srcdir}/lib/libsolkerncompat/include @DEBUG_CFLAGS@
AM_CCASFLAGS = $(AM_CFLAGS)

EXTRA_DIST = amd64/atomic.S \
             i386/atomic.S \
             generic/atomic.c \
             sparc64/atomic.S

noinst_HEADERS = include/amd64/sys/asm_linkage.h \
                 include/ia32/sys/asm_linkage.h \
                 include/i386/sys/asm_linkage.h \
                 include/sparc64/sys/asm_linkage.h
