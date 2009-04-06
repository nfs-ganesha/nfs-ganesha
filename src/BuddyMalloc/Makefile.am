noinst_LTLIBRARIES          = libBuddyMalloc.la

libBuddyMalloc_la_SOURCES   = BuddyMalloc.c BuddyConfig.c ../include/BuddyMalloc.h ../include/config_parsing.h

check_PROGRAMS              = test_buddy
test_buddy_SOURCES          = test_buddy.c

test_buddy_LDADD            = libBuddyMalloc.la ../Log/liblog.la -lpthread

new: clean all

