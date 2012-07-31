lib_LTLIBRARIES           = libfsalceph.la

libfsalceph_la_SOURCES    = main.c                     \
			    export.c                   \
			    handle.c                   \
			    ds.c                       \
			    internal.c                 \
			    internal.h                 \
	                    ../../include/fsal.h       \
                            ../../include/fsal_types.h \
	                    ../../include/err_fsal.h

libfsalceph_la_CFLAGS                = $(FSAL_CFLAGS) $(SEC_CFLAGS) @CEPH_CFLAGS@
libfsalceph_la_LDFLAGS               = @CEPH_LDFLAGS@
libfsalceph_la_LIBADD                = @CEPH_LIB@

new: clean all

