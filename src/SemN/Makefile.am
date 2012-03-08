noinst_LTLIBRARIES    = libSemN.la

libSemN_la_SOURCES  = SemN.c              \
                      ../include/SemN.h

TESTS = test_sem1 test_sem2

check_PROGRAMS        = test_sem1 test_sem2

test_sem1_SOURCES       = test_sem1.c
test_sem1_LDADD         = libSemN.la 

test_sem2_SOURCES       = test_sem2.c
test_sem2_LDADD         = libSemN.la 

new: clean all

new: clean all

