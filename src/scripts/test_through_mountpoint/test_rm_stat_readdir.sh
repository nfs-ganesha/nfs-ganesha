#!/bin/sh

# This test create entries, check they are returned by readdir and getattr
# then, it removes the entries and check getattr returns ENOENT and readdir don't display it

TEST_DIR=$1

SUB_DIR1="TESTDIR_RMSTAT-$$"

FILENAME_1="FILEabcdefghijklm"
FILENAME_2="FILEsqklsjdlqsjll"

NB_FILES=1000

if [[ $TEST_DIR = "" ]]; then
  echo "usage : $0 <test_dir>"
  exit 1
fi

if [[ ! -d $TEST_DIR ]]; then
  echo "$1 does not exist or is not a directory";
  exit 1
fi

#creation de l'arborescence initiale
mkdir -p "$TEST_DIR/$SUB_DIR1"

ERR=0

echo "create/getattr/readdir sequence... ($NB_FILES files)"

I=0
while (( $I < (($NB_FILES/2)) )) ; do

  printf "#"
  
  # create file
  PATH1="$TEST_DIR/$SUB_DIR1/${FILENAME_1}_$I"

  touch $PATH1
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error creating file $PATH1"
    ls -li $PATH1
  fi

  # first, getattr
  stat $PATH1 > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error getting file attributes for $PATH1"
    ls -li $PATH1
  fi
 
  # then, check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_1}_$I$" > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_1}_$I does not appear in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi

  # create a second file
  PATH2="$TEST_DIR/$SUB_DIR1/${FILENAME_2}_$I"

  touch $PATH2
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error creating file $PATH2"
    ls -li $PATH2
  fi

  # for this file, we first check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_2}_$I$" > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_2}_$I does not appear in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi


  # then, getattr
  stat $PATH2 > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error getting file attributes for $PATH2"
    ls -li $PATH2
  fi
 
  (( I = $I + 1 ))  
  
done

echo

echo "rm/stat/readdir sequence..."

I=0
while (( $I < (($NB_FILES/2)) )) ; do

  printf "#"
  
  # remove file
  PATH1="$TEST_DIR/$SUB_DIR1/${FILENAME_1}_$I"

  rm $PATH1
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error removing file $PATH1"
    ls -li $PATH1
  fi

  # try to getattr
  ls -l $PATH1 > /dev/null 2>&1
  val=$?
  if (( $val != 2 )); then
    ((ERR=$ERR+1))
    echo "Error: getattr after rm should return ENOENT ($val returned)"
    ls -li $PATH1
  fi
 
  # then, check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_1}_$I$" > /dev/null
  if (( $? == 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_1}_$I still appears in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi

  # remove file
  PATH2="$TEST_DIR/$SUB_DIR1/${FILENAME_2}_$I"

  rm $PATH2
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error removing file $PATH2"
    ls -li $PATH2
  fi

  # check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_2}_$I$" > /dev/null
  if (( $? == 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_2}_$I still appears in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi


  # then, try to getattr
  ls -l $PATH2 > /dev/null 2>&1
  val=$?
  if (( $val != 2 )); then
    ((ERR=$ERR+1))
    echo "Error: getattr after rm should return ENOENT ($val returned)"
    ls -li $PATH2
  fi

 
  (( I = $I + 1 ))  
  
done

echo
echo "create/getattr/readdir sequence AGAIN... ($NB_FILES files)"

I=0
while (( $I < (($NB_FILES/2)) )) ; do

  printf "#"
  
  # create file
  PATH1="$TEST_DIR/$SUB_DIR1/${FILENAME_1}_$I"

  touch $PATH1
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error creating file $PATH1"
    ls -li $PATH1
  fi

  # first, getattr
  stat $PATH1 > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error getting file attributes for $PATH1"
    ls -li $PATH1
  fi
 
  # then, check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_1}_$I$" > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_1}_$I does not appear in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi

  # create a second file
  PATH2="$TEST_DIR/$SUB_DIR1/${FILENAME_2}_$I"

  touch $PATH2
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error creating file $PATH2"
    ls -li $PATH2
  fi

  # for this file, we first check readdir
  ls $TEST_DIR/$SUB_DIR1 | egrep -e "^${FILENAME_2}_$I$" > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error: ${FILENAME_2}_$I does not appear in readdir($TEST_DIR/$SUB_DIR1)"
    ls $TEST_DIR/$SUB_DIR1
  fi


  # then, getattr
  stat $PATH2 > /dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    echo "Error getting file attributes for $PATH2"
    ls -li $PATH2
  fi
 
  (( I = $I + 1 ))  
  
done

echo

echo "cleaning test directory..."

rm -rf "$TEST_DIR/$SUB_DIR1"

echo "Test termine. $ERR erreurs."

