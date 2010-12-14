#!/bin/sh
##
## test_liblog_STD.sh
## test log functions
## 
## Made by Frank Filz
##

cleanup()
{
	if [ $CLEANUP -eq 1 ]
	then
		rm -f $MSGFILE
		rm -f $OUTFILE
		rm -f $ERRFILE
		rm -f $FILE
	else
		ls -l $MSGFILE $OUTFILE $ERRFILE $FILE
	fi
}

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
		cat $2 | grep -v "Changing"
		echo "***************************************************"
		cleanup
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
	if [ $SYSLOG -eq 1 ]
	then
		test_file "$1" $MSGFILE
	else
		echo "Skipping test of syslog for $1"
	fi
}

run()
{
	echo STDOUT
	echo $DATE
	echo STDERR 1>&2
	echo $DATE 1>&2
	./test_liblog STD "$DATE" $FILE
}

if [ -z "$1" ]
then
	CLEANUP=1
else
	CLEANUP=0
fi

OUTFILE=/tmp/test_liblog.out.$$
ERRFILE=/tmp/test_liblog.err.$$
MESSAGES=/var/log/messages
MSGFILE=/tmp/test_liblog.msg.$$
FILE=/tmp/test_liblog.file.$$
DATE=`date`
export COMPONENT_MEMCORRUPT=NIV_FULL_DEBUG
export COMPONENT_LOG=NIV_FULL_DEBUG
echo $FILE > $FILE
echo $DATE >> $FILE
echo /var/log/messages > $MSGFILE
echo $DATE >> $MSGFILE
run >$OUTFILE 2>$ERRFILE

RC=$?
if [ $RC -ne 0 ]
then
	echo "Test failed rc =" $RC
	echo ***********************************
	echo $OUTFILE
	cat $OUTFILE
	echo ***********************************
	echo $ERRFILE
	cat $ERRFILE
	cleanup
	exit 1
fi

# make sure syslog has a chance to get all the messages out
sleep 1

test_stdout "My PID = "
if [ -r $MESSAGES ]
then
	PID=`grep "My PID = " $OUTFILE | sed -e 's@\(.*My PID = \)\([1-9][0-9*]\)@\2@'`
	grep $PID $MESSAGES >> $MSGFILE
	SYSLOG=1
else
	SYSLOG=0
fi

test_stdout "LOG: Using environment variable to switch log level for COMPONENT_MEMCORRUPT from NIV_EVENT to NIV_FULL_DEBUG"
test_stdout "AddFamilyError = 3"
test_stdout "The family which was added is Family Dummy"

test_stdout "A numerical error : error 5 = ERR_SIGACTION(5) : 'sigaction impossible', in ERR_DUMMY_2 ERR_DUMMY_2(1) : 'Second Dummy Error'"
test_stdout "A numerical error : error 40 = ERR_OPEN(40) : 'open impossible', in ERR_DUMMY_1 ERR_DUMMY_1(0) : 'First Dummy Error'"

test_stdout "Test log_snprintf$"
test_stdout "CONFIG: Error ERR_MALLOC : malloc impossible : status 22 : Invalid argument : Line"
test_stdout "This should appear if environment is set properly"
test_stdout "localhost : test_liblog-[1-9][0-9]*\[monothread\] :NFS STARTUP: Starting Log Tests$"

test_stdout "DISPATCH: EVENT: This should go to stdout"
test_stderr "DISPATCH: EVENT: This should go to stderr"
test_syslog "DISPATCH: EVENT: This should go to syslog (verf = $DATE)"
test_file   "DISPATCH: EVENT: This should go to $FILE" $FILE

echo "PASSED!"

cleanup
