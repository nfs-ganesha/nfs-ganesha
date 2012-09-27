AM_CFLAGS          = $(FSAL_CFLAGS) $(SEC_CFLAGS)
noinst_LTLIBRARIES = libfsalup.la

libfsalup_la_SOURCES = fsal_up_thread.c \
                       fsal_up_top.c \
                       fsal_up_utils.c

libfsalup_la_DEPENDENCIES = $(libfsalup_la_SOURCES)

new: clean all
