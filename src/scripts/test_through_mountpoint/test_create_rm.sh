#!/bin/sh

# ce test cree plein d'entrees dans un repertoire
# et les rename une par une

TEST_DIR=$1

SUB_DIR1="touch_rm-$$"

FILENAME_1="HDep-n=Temps_u=s+n=NumSDom_g=0200x0203.v=f0000000000000000-v=f3f9f16208bfc1f8d"
FILENAME_2="HDep-n=Temps_u=s+n=NumSDom_g=0184x0187.v=f0000000000000000-v=f3f74bbcde85767c5"

NB_LOOP=100

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

echo "touch/rm sequence... ($NB_LOOP times)"

I=0
while (( $I < $NB_LOOP )) ; do

  printf "#"
  
  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  fi
  
  rm "$TEST_DIR/$SUB_DIR1/$FILENAME_1"  
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  fi
  
  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi
  
  rm "$TEST_DIR/$SUB_DIR1/$FILENAME_2"  
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi
  
  (( I = $I + 1 ))  
  
done

echo

echo "touch/mv/rm sequence... ($NB_LOOP times)"

I=0
while (( $I < $NB_LOOP )) ; do

  printf "#"

  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  fi

  mv "$TEST_DIR/$SUB_DIR1/$FILENAME_1" "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1" "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi

  rm "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi

  (( I = $I + 1 ))

done

echo
echo "touch/ls/mv/rm sequence... ($NB_LOOP times)"

I=0
while (( $I < $NB_LOOP )) ; do

  printf "#"

  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1"
  fi
  
  ls -l "$TEST_DIR/$SUB_DIR1" >/dev/null
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1"
  fi  

  mv "$TEST_DIR/$SUB_DIR1/$FILENAME_1" "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1" "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi

  rm "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2"
  fi

  (( I = $I + 1 ))

done



echo "cleaning test directory..."

rm -rf "$TEST_DIR/$SUB_DIR1"

echo "Test termine. $ERR erreurs."

