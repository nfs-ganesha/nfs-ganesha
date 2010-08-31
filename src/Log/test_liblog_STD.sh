#!/bin/sh
##
## test_liblog_STD.sh
## test log functions
## 
## Made by Frank Filz
##

test_file ()
{
	grep "$1" $2 >/dev/null

	RC=$?
	if [ $RC -ne 0 ]
	then
		echo "Test failed rc = " $RC
		echo "Expected to find '"$1"'"
		cat /tmp/test_liblog.err
		exit 1
	fi
}

test_stderr ()
{
	test_file "$1" /tmp/test_liblog.err
}

test_stdout ()
{
	test_file "$1" /tmp/test_liblog.out
}

./test_liblog STD >/tmp/test_liblog.out 2>/tmp/test_liblog.err
RC=$?
if [ $RC -ne 0 ]
then
	echo "Test failed rc =" $RC
	cat /tmp/test_liblog.out
	exit 1
fi

test_stdout "Starting Log Tests"
test_stdout "LOG: NIV_MAJ: Changing log level for all components to at least NIV_EVENT"
test_stdout "Error ERR_FORK : fork impossible : status 2 : No such file or directory : Line"
test_stdout "Error ERR_SOCKET : socket impossible : status 4 : Interrupted system call : Line"
test_stdout "Error ERR_DUMMY_2 : Second Dummy Error : status 2 : No such file or directory : Line"

test_stderr "Error ERR_DUMMY_1 : First Dummy Error : status 1 : Operation not permitted : Line"
test_stderr "This should go to stderr"
test_stderr "DISPATCH: NIV_EVENT: This should also go to stderr"

echo "PASSED!"
