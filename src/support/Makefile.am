AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

#check_PROGRAMS = test_nfs_ip_stats test_nfs_ip_name test_support
check_PROGRAMS = test_nfs_ip_stats test_nfs_ip_name

test_nfs_ip_stats_SOURCES = test_nfs_ip_stats.c
test_nfs_ip_stats_LDADD = libsupport.la ../HashTable/libhashtable.la ../Log/liblog.la

test_nfs_ip_name_SOURCES = test_nfs_ip_name.c
test_nfs_ip_name_LDADD = libsupport.la ../HashTable/libhashtable.la ../Log/liblog.la ../ConfigParsing/libConfigParsing.la


TESTS = test_nfs_ip_stats test_nfs_ip_name $(check_SCRIPTS)

noinst_LTLIBRARIES            = libsupport.la

libsupport_la_SOURCES = nfs4_acls.c                         \
                         nfs_export_list.c                  \
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
                         bsd-base64.c                       \
                         strlcat.c                          \
                         strnlen.c                          \
                         misc.c                             \
                         strlcpy.c

libsupport_la_LIBADD = ../cidr/libcidr.la

if ERROR_INJECTION
libsupport_la_SOURCES += err_inject.c
endif


new: clean all 

testrunner:  $(check_PROGRAMS)
	../tools/maketest -x support -f ./maketest.conf > ../testres-xml/support.xml

