
if USE_FSAL_UP

AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)
noinst_LTLIBRARIES = libfsalup.la

SUBDIRS = DUMB

libfsalup_la_SOURCES = fsal_up_thread.c \
                       DUMB/fsal_up_functions.c \
                      ../include/fsal_up.h
libfsalup_la_LIBADD = DUMB/libdumbfsalup.la
libfsalup_la_DEPENDENCIES = $(libfsalup_la_SOURCES) $(libfsalup_la_LIBADD)
# libfsalup_la_DEPENDENCIES =

endif

new: clean all