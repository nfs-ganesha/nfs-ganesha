AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libsupport.la

libsupport_la_SOURCES =  nfs_export_list.c                  \
                         nfs_filehandle_mgmt.c              \
                         nfs_mnt_list.c                     \
                         nfs_read_conf.c                    \
                         nfs_convert.c                      \
                         nfs_stat_mgmt.c                    \
                         nfs_ip_name.c                      \
                         nfs_ip_stats.c                     \
                         nfs_client_id.c                    \
                         nfs_state_id.c                     \
                         nfs_open_owner.c                   \
                         nfs4_tools.c                       \
                         exports.c                          \
                         ../include/nfs_file_handle.h       \
                         ../include/nfs_core.h              \
                         ../include/nfs_tools.h             \
                         ../include/HashData.h              \
                         ../include/HashTable.h             \
                         ../include/SemN.h                  \
                         ../include/cache_content.h         \
                         ../include/cache_inode.h           \
                         ../include/common_utils.h          \
                         ../include/config_parsing.h        \
                         ../include/fsal.h                  \
                         ../include/log_functions.h         \
                         ../include/mount.h                 \
                         ../include/nfs23.h                 \
                         ../include/nfs4.h                  \
                         ../include/nfs_core.h              \
                         ../include/nfs_creds.h             \
                         ../include/nfs_dupreq.h            \
                         ../include/nfs_exports.h           \
                         ../include/nfs_proto_functions.h   \
                         ../include/nfs_proto_tools.h       \
                         ../include/nfs_stat.h              \
                         ../include/stuff_alloc.h

if USE_NLM
libsupport_la_SOURCES += nlm_util.c nlm_async.c nlm4_send_reply.c nsm.c
endif

if USE_NFS4_1
libsupport_la_SOURCES += nfs_session_id.c 
endif


new: clean all 

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

