
if USE_FSAL_CB

AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)
noinst_LTLIBRARIES = libfsalcb.la

SUBDIRS = DUMB

libfsalcb_la_SOURCES = fsal_cb_thread.c \
                       DUMB/fsal_cb_functions.c \
                      ../include/fsal_cb.h
libfsalcb_la_LIBADD = DUMB/libdumbfsalcb.la
libfsalcb_la_DEPENDENCIES = $(libfsalcb_la_SOURCES) $(libfsalcb_la_LIBADD)
# libfsalcb_la_DEPENDENCIES =

endif

new: clean all