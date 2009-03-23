AM_CFLAGS = -DSTDARG

check_PROGRAMS = basic_test1  \
                 basic_test2  \
                 basic_test3  \
                 basic_test4  \
                 basic_test4a \
                 basic_test5  \
                 basic_test5a \
                 basic_test5b \
                 basic_test6  \
                 basic_test7  \
                 basic_test7a \
                 basic_test7b \
                 basic_test8  \
                 basic_test9 

basic_test1_SOURCES = subr.c test1.c tests.h
basic_test1_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test2_SOURCES = subr.c test2.c tests.h
basic_test2_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test3_SOURCES = subr.c test3.c tests.h
basic_test3_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test4_SOURCES = subr.c test4.c tests.h
basic_test4_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test4a_SOURCES = subr.c test4a.c tests.h
basic_test4a_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test5_SOURCES = subr.c test5.c tests.h
basic_test5_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test5a_SOURCES = subr.c test5a.c tests.h
basic_test5a_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test5b_SOURCES = subr.c test5b.c tests.h
basic_test5b_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test6_SOURCES = subr.c test6.c tests.h
basic_test6_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test7_SOURCES = subr.c test7.c tests.h
basic_test7_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test7a_SOURCES = subr.c test7a.c tests.h
basic_test7a_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test7b_SOURCES = subr.c test7b.c tests.h
basic_test7b_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test8_SOURCES = subr.c test8.c tests.h
basic_test8_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

basic_test9_SOURCES = subr.c test9.c tests.h
basic_test9_LDADD = ../ConnectathonConfigParsing/libConnectathon_ConfigParsing.la

new: clean check

runtest: check
	../../tools/maketest -q -f maketest.conf

EXTRA_DIST = maketest.conf connectathon.tests.conf README.txt

