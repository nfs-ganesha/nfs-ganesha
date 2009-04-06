noinst_LTLIBRARIES    = librwlock.la

librwlock_la_SOURCES  = RW_Lock.c            \
                        RW_Lock.h            \
                        ../include/RW_Lock.h 
                          

check_PROGRAMS        = test_rw

test_rw_SOURCES       = test_rw.c
test_rw_LDADD         = librwlock.la

new: clean all

doc:
	echo "No docs to be generated for RW_Lock"

