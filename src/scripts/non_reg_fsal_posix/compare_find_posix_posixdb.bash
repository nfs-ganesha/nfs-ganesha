#!/bin/bash


LOGIN=mickael
HOST=galion18
PORT=5432
DBNAME=posixdb_mickael
PASSWORDFILE=~/.pgpass


BIN=`dirname $0`/../../../bin/`archi -M`

if [ ! -d "$1" ]
then
  echo "Usage: `basename $0` /path"
  exit
fi


echo "Populating db..."
$BIN/fsal_posixdb_tool -H $HOST -P $PORT -L $LOGIN -D $DBNAME -K $PASSWORDFILE empty_database > /dev/null
$BIN/fsal_posixdb_tool -H $HOST -P $PORT -L $LOGIN -D $DBNAME -K $PASSWORDFILE populate $1 > /dev/null
echo

touch /tmp/test_fsal_posix_finddb
touch /tmp/test_fsal_posix_findfs
  
echo "find (in the database)..."
$BIN/fsal_posixdb_tool -H $HOST -P $PORT -L $LOGIN -D $DBNAME -K $PASSWORDFILE find |sort > /tmp/test_fsal_posix_finddb


echo "find (on the filesystem)..."
find $1 2> /dev/null |sort > /tmp/test_fsal_posix_findfs

if RES=`diff /tmp/test_fsal_posix_finddb /tmp/test_fsal_posix_findfs`
then
  echo OK
  exit 0
else
  echo BAD !
  echo $RES
  exit -1
fi
