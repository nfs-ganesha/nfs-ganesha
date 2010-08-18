noinst_LTLIBRARIES = libumem.la libumem_malloc.la

libumem_la_LDFLAGS = -lpthread -ldl
libumem_la_SOURCES =	init_lib.c \
			umem_agent_support.c \
			umem_fail.c \
			umem_fork.c \
			umem_update_thread.c \
			vmem_mmap.c \
			vmem_sbrk.c \
			envvar.c \
			getpcstack.c \
			misc.c \
			misc.h \
			vmem_base.c \
			vmem_base.h \
			vmem_stand.h \
			umem.c \
			umem.h \
			umem_base.h \
			umem_impl.h \
			sol_compat.h \
			vmem.c \
			sys/vmem.h \
			sys/vmem_impl_user.h
libumem_la_CFLAGS = @DEBUG_CFLAGS@

libumem_malloc_la_SOURCES = malloc.c
libumem_malloc_la_LDFLAGS = -lpthread -ldl -R$(libdir) -lumem 
libumem_malloc_la_CFLAGS = @DEBUG_CFLAGS@
libumem_malloc_la_LIBADD = libumem.la

noinst_HEADERS = include/umem.h

