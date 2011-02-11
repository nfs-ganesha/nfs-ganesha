
# check: user1.sav user2.sav user3.sav testdir.sav
#	../../tools/maketest -qce -f maketest.conf

testrunner:  $(check_PROGRAMS)
	../../tools/maketest -e -x From_Ganeshell -f ./maketest.conf > ../../testres-xml/From_Ganeshell.xml
	
user1.sav:
	./config_tests.ksh
user2.sav:
	./config_tests.ksh
user3.sav:
	./config_tests.ksh
testdir.sav:
	./config_tests.ksh

CLEANFILES =  *.sav maketest.conf

EXTRA_DIST=test_cache_access2.gansh     test_cache_stat.gansh      test_fsal_rename.gansh      test_nfs_mkdir.gansh \
	test_cache_chown.gansh       test_cache_symlinks.gansh  test_fsal_rights.gansh      test_nfs_rename.gansh \
	config_tests.ksh         test_cache_handlecmp.gansh   test_fsal_access.gansh     test_fsal_rootaccess.gansh  test_nfs_rights.gansh \
	test_cache_mkdir.gansh       test_fsal_access2.gansh    test_fsal_stat.gansh        test_nfs_rootaccess.gansh \
	maketest.conf.in         test_cache_rename.gansh      test_fsal_chown.gansh      test_fsal_symlinks.gansh    test_nfs_stat.gansh \
	test_cache_rights.gansh      test_fsal_handlecmp.gansh  test_nfs_chown.gansh        test_nfs_symlinks.gansh \
	test_cache_access.gansh  test_cache_rootaccess.gansh  test_fsal_mkdir.gansh      test_nfs_handlecmp.gansh

