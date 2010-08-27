noinst_LTLIBRARIES    = librwlock.la

librwlock_la_SOURCES  = RW_Lock.c            \
                        ../include/RW_Lock.h 
                          
TESTS = test_rw

check_PROGRAMS        = test_rw

test_rw_SOURCES       = test_rw.c
test_rw_LDADD         = librwlock.la ../Log/liblog.la

new: clean all

doc:
	echo "No docs to be generated for RW_Lock"

