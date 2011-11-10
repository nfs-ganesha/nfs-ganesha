noinst_LTLIBRARIES            = libhashtable.la

if USE_BUDDY_SYSTEM
BUDDY_LIB_FLAGS = ../BuddyMalloc/libBuddyMalloc.la
else
BUDDY_LIB_FLAGS =
endif

libhashtable_la_SOURCES       = HashTable.c                \
                                ../include/HashTable.h     \
                                ../include/HashData.h      \
                                ../include/err_HashTable.h
   
TESTS = test_libcmc test_libcmc_bugdelete 

check_PROGRAMS                  = test_libcmc test_libcmc_bugdelete test_libcmc_config

test_libcmc_SOURCES             = test_cmchash.c
test_libcmc_LDADD               = libhashtable.la $(BUDDY_LIB_FLAGS) ../RW_Lock/librwlock.la ../Log/liblog.la ../test/liboutils_profiling.la -lpthread

test_libcmc_bugdelete_SOURCES   = test_cmchash_bugdelete.c
test_libcmc_bugdelete_LDADD     = libhashtable.la $(BUDDY_LIB_FLAGS) ../RW_Lock/librwlock.la ../Log/liblog.la ../test/liboutils_profiling.la -lpthread

test_libcmc_config_SOURCES      = test_configurable_hash.c
test_libcmc_config_LDADD        = libhashtable.la $(BUDDY_LIB_FLAGS) ../RW_Lock/librwlock.la ../Log/liblog.la ../test/liboutils_profiling.la -lpthread

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make

testrunner:  $(check_PROGRAMS)
	 ../tools/maketest -x HashTable -f ./maketest.conf > ../testres-xml/HashTable.xml	
