AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#check_PROGRAMS = test_nfs_ip_stats test_nfs_ip_name test_support
check_PROGRAMS = test_nfs_ip_stats test_nfs_ip_name

test_nfs_ip_stats_SOURCES = test_nfs_ip_stats.c
test_nfs_ip_stats_LDADD = libsupport.la ../HashTable/libhashtable.la ../Log/liblog.la ../RW_Lock/librwlock.la

test_nfs_ip_name_SOURCES = test_nfs_ip_name.c
test_nfs_ip_name_LDADD = libsupport.la ../HashTable/libhashtable.la ../Log/liblog.la ../RW_Lock/librwlock.la ../ConfigParsing/libConfigParsing.la


TESTS = test_nfs_ip_stats test_nfs_ip_name $(check_SCRIPTS)

noinst_LTLIBRARIES            = libsupport.la

libsupport_la_SOURCES =  nfs_export_list.c                  \
                         nfs_filehandle_mgmt.c              \
                         nfs_mnt_list.c                     \
                         nfs_read_conf.c                    \
                         nfs_convert.c                      \
                         nfs_stat_mgmt.c                    \
                         nfs_ip_name.c                      \
                         nfs_ip_stats.c                     \
                         exports.c                          \
                         fridgethr.c                        \
                         lookup3.c                          \
                         murmur3.c                          \
                         generic_weakref.c                  \
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
                         ../include/log.h         \
                         ../include/lookup3.h               \
                         ../include/mount.h                 \
                         ../include/nfs23.h                 \
                         ../include/nfs4.h                  \
                         ../include/nfs_core.h              \
                         ../include/err_inject.h            \
                         ../include/nfs_creds.h             \
                         ../include/nfs_dupreq.h            \
                         ../include/nfs_exports.h           \
                         ../include/nfs_proto_functions.h   \
                         ../include/nfs_proto_tools.h       \
                         ../include/nfs_stat.h              \
                         ../include/err_inject.h

libsupport_la_LIBADD = ../cidr/libcidr.la

if RESULT_IS_DAEMON
libsupport_la_LIBADD         +=   ../RPCAL/librpcal.la 
endif

if USE_NFS4_ACL
libsupport_la_SOURCES += nfs4_acls.c
endif

if ERROR_INJECTION
libsupport_la_SOURCES += err_inject.c
endif


new: clean all 

testrunner:  $(check_PROGRAMS)
	../tools/maketest -x support -f ./maketest.conf > ../testres-xml/support.xml

