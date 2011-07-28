#!/bin/sh

# ce test cree plein d'entrees dans un repertoire
# et les rename une par une

TEST_DIR=$1

SUB_DIR1="create_rename-$$"

FILENAME_1="HDep-n=Temps_u=s+n=NumSDom_g=0200x0203.v=f0000000000000000-v=f3f9f16208bfc1f8d"
FILENAME_2="HDep-n=Temps_u=s+n=NumSDom_g=0184x0187.v=f0000000000000000-v=f3f74bbcde85767c5"

NB_ENTREES=500

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

echo "creating files..."

I=0
while (( $I < $NB_ENTREES )) ; do

  printf "#"
  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I"
  fi
  (( I = $I + 1 ))  
  
done

echo

echo "renaming files..."

I=0
while (( $I < $NB_ENTREES )) ; do

  printf "#"
  
  mv "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I" "$TEST_DIR/$SUB_DIR1/$FILENAME_2-$I"
  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I" "$TEST_DIR/$SUB_DIR1/$FILENAME_2-$I"
  fi
  
  (( I = $I + 1 ))
  
done

echo
echo "renaming files..."

I=0
while (( $I < $NB_ENTREES )) ; do

  printf "#"

  mv "$TEST_DIR/$SUB_DIR1/$FILENAME_2-$I" "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I"

  if (( $? != 0 )); then
    ((ERR=$ERR+1))
    ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2-$I" "$TEST_DIR/$SUB_DIR1/$FILENAME_1-$I"
  fi
  
  (( I = $I + 1 ))
  
done

echo

echo "cleaning test directory..."

rm -rf "$TEST_DIR/$SUB_DIR1"

echo "Test termine. $ERR erreurs."

