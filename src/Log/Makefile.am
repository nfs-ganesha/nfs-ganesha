noinst_LTLIBRARIES            = liblog.la

TESTS = $(check_SCRIPTS)

check_SCRIPTS = test_liblog_MT.sh  test_liblog_STD.sh

check_PROGRAMS                = test_liblog

liblog_la_SOURCES = log_functions.c \
		    ../include/log.h

test_liblog_SOURCES    	= test_liblog_functions.c
test_liblog_LDADD    	= liblog.la

new: clean all

testrunner:  $(check_PROGRAMS)
	../tools/maketest -x Log -f ./maketest.conf > ../testres-xml/Log.xml

