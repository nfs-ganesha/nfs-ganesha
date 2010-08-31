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
		echo "***************************************************"
		echo "Test failed rc = " $RC
		echo "Expected to find '"$1"' in $2"
		echo "Contents of $2:"
		echo "---------------------------------------------------"
		cat $2
		echo "***************************************************"
		exit 1
	fi
}

test_stderr ()
{
	test_file "$1" $ERRFILE
}

test_stdout ()
{
	test_file "$1" $OUTFILE
}

test_syslog ()
{
	test_file "$1" $MSGFILE
}

OUTFILE=/tmp/test_liblog.out
ERRFILE=/tmp/test_liblog.err
MESSAGES=/var/log/messages
MSGFILE=/tmp/test_liblog.msg
DATE=`date`
export COMPONENT_MEMCORRUPT=NIV_FULL_DEBUG
echo $DATE > /tmp/test_liblog.file
echo $DATE > $ERRFILE
echo $DATE > $OUTFILE
echo $DATE > $MSGFILE
./test_liblog STD "$DATE" >>$OUTFILE 2>>$ERRFILE
RC=$?
if [ $RC -ne 0 ]
then
	echo "Test failed rc =" $RC
	cat $OUTFILE
	exit 1
fi

# make sure syslog has a chance to get all the messages out
sleep 1

test_stdout "My PID = "
PID=`grep "My PID = " $OUTFILE | sed -e 's@\(.*My PID = \)\([1-9][0-9*]\)@\2@'`
grep $PID $MESSAGES >> $MSGFILE

test_stdout "LOG: NIV_MAJ: Changing log level for all components to at least NIV_EVENT"
test_stdout "LOG: NIV_MAJ: Using environment variable to switch log level for COMPONENT_MEMCORRUPT from NIV_EVENT to NIV_FULL_DEBUG"
test_stdout "AddFamilyError = 3"
test_stdout "The family which was added is Family Dummy"
test_stdout "Testing possible environment variable"
test_stdout "localhost : test_liblog-[1-9][0-9]*\[monothread\] :Starting Log Tests$"
test_stdout "LOG: NIV_MAJ: Changing log level for all components to at least NIV_EVENT"
test_stdout "Error ERR_FORK : fork impossible : status 2 : No such file or directory : Line"
test_stdout "Error ERR_SOCKET : socket impossible : status 4 : Interrupted system call : Line"
test_stdout "Error ERR_DUMMY_2 : Second Dummy Error : status 2 : No such file or directory : Line"

test_stdout "DISPATCH: NIV_EVENT: This should go to stdout"
test_stderr "DISPATCH: NIV_EVENT: This should go to stderr"
test_syslog "DISPATCH: NIV_EVENT: This should go to syslog (verf = $DATE)"
test_file   "DISPATCH: NIV_EVENT: This should go to /tmp/test_liblog.file" /tmp/test_liblog.file

echo "PASSED!"
