#!/bin/sh

# ce test cree plein d'entrees dans un repertoire
# et les rename une par une

TEST_DIR=$1

SUB_DIR1="create_unlink-$$"

FILENAME="kjdfslkjflskdjflsdkjflksdjflsdkjf"

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


  echo "create/unlink sequence..."

  I=0
  while (( $I < $NB_ITER_1 )) ; do

    printf "#"

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME already exists" >&2
      ((ERR=$ERR+1))    
    else
      touch "$TEST_DIR/$SUB_DIR1/$FILENAME"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME"
      fi
    fi

    rm -f "$TEST_DIR/$SUB_DIR1/$FILENAME"
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME"
    fi

    (( I = $I + 1 ))  

  done

    printf "#"

    if [ -e "$TEST_DIR/$SUB_DIR1/$FILENAME" ]; then
      echo "Error : $TEST_DIR/$SUB_DIR1/$FILENAME already exists" >&2
      ((ERR=$ERR+1))    
    else
      touch "$TEST_DIR/$SUB_DIR1/$FILENAME"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME"
      fi
    fi

  echo

  echo "readdir/lookup/getattr sequence..."

  I=0
  while (( $I < $NB_ITER_1 )) ; do

    printf "#"

    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME" > /dev/null
    if (( $? != 0 )); then
      ((ERR=$ERR+1))
      ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME"
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

  rm -f "$TEST_DIR/$SUB_DIR1/$FILENAME"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME"
  fi
  
  (( J = $J + 1 ))

done

echo "Test termine. $ERR erreurs."

