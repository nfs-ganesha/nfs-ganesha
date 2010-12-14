noinst_LTLIBRARIES          = libBuddyMalloc.la

libBuddyMalloc_la_SOURCES   = BuddyMalloc.c BuddyConfig.c ../include/BuddyMalloc.h ../include/config_parsing.h

TESTS = $(check_SCRIPTS)

check_SCRIPTS = test_buddy_1.sh test_buddy_3.sh test_buddy_5.sh test_buddy_7.sh test_buddy_9.sh test_buddy_B.sh \
		test_buddy_2.sh test_buddy_4.sh test_buddy_6.sh test_buddy_8.sh test_buddy_A.sh \
		test_buddy_1mt.sh test_buddy_3mt.sh test_buddy_5mt.sh test_buddy_7mt.sh test_buddy_9mt.sh \
		test_buddy_2mt.sh test_buddy_4mt.sh test_buddy_6mt.sh test_buddy_8mt.sh test_buddy_Bmt.sh



check_PROGRAMS              = test_buddy 

test_buddy_SOURCES          = test_buddy.c

test_buddy_LDADD            = libBuddyMalloc.la ../Log/liblog.la -lpthread

new: clean all


testrunner:  $(check_PROGRAMS)
	../tools/maketest -x BuddyMalloc -f ./maketest.conf > ../testres-xml/BuddyMalloc.xml

