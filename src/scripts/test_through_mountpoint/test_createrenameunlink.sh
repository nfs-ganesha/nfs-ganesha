#!/bin/sh

# ce test cree plein d'entrees dans un repertoire
# et les rename une par une

TEST_DIR=$1

SUB_DIR1="create_rename_unlink-$$"

FILENAME1="kjdfslkjflskdjflsdkjflksdjflsdkjf"
FILENAME2="mdslkmdf_lkdsjfksedf_9879"

NB_ITER_1=100
NB_ITER_2=10

if [[ $TEST_DIR = "" ]]; then
  echo "usage : $0 <test_dir>"
  exit 1
fi

if [[ ! -d $TEST_DIR ]]; then
  echo "$1 n'existe pas ou n'est pas un repertoire";
  exit 1
fi

#creation de l'arborescence initiale
mkdir -p "$TEST_DIR/$SUB_DIR1"

ERR=0

J=0
while (( $J < $NB_ITER_2 )) ; do


  echo "create/rename/unlink sequence..."

  I=0
  while (( $I < $NB_ITER_1 )) ; do

    printf "#"

# sequence 1 = create, rename unlink

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME1" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME1 already exists" >&2
      ((ERR=$ERR+1))    
    else
      touch "$TEST_DIR/$SUB_DIR1/$FILENAME1"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1"
      fi
    fi

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME2" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME2 already exists" >&2
      ((ERR=$ERR+1))    
    else
      mv "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      fi
    fi

    rm -f "$TEST_DIR/$SUB_DIR1/$FILENAME2"
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
    fi

# sequence 2 = create, rename, rename, unlink

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME1" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME1 already exists" >&2
      ((ERR=$ERR+1))    
    else
      touch "$TEST_DIR/$SUB_DIR1/$FILENAME1"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1"
      fi
    fi

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME2" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME2 already exists" >&2
      ((ERR=$ERR+1))    
    else
      mv "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      fi
    fi

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME1" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME1 already exists" >&2
      ((ERR=$ERR+1))    
    else
      mv "$TEST_DIR/$SUB_DIR1/$FILENAME2" "$TEST_DIR/$SUB_DIR1/$FILENAME1"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      fi
    fi

    rm -f "$TEST_DIR/$SUB_DIR1/$FILENAME1"
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
    fi

    (( I = $I + 1 ))  

  done

    printf "#"

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME2" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME2 already exists" >&2
      ((ERR=$ERR+1))    
    else
      touch "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME2"
      fi
    fi

  echo

  echo "readdir/lookup/getattr sequence..."

  I=0
  while (( $I < $NB_ITER_1 )) ; do

    printf "#"

    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME2" > /dev/null
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME1" "$TEST_DIR/$SUB_DIR1/$FILENAME2"
    fi

    ls -li "$TEST_DIR/$SUB_DIR1" > /dev/null
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1"
    fi

    (( I = $I + 1 ))

  done

  echo
  echo "removing file..."

  rm -f "$TEST_DIR/$SUB_DIR1/$FILENAME2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME2"
  fi
  
  (( J = $J + 1 ))

done

echo "Test termine. $ERR erreurs."

