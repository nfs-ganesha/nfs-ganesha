#!/bin/sh

# de test cree simplement deux fichiers dans un repertoire
# et les rename

TEST_DIR=$1

SUB_DIR1="hercule-$$/depouillement"
SUB_DIR2="hercule-$$/protections"
FILENAME_1="HDep-n=Temps_u=s+n=NumSDom_g=0200x0203.v=f0000000000000000-v=f3f9f16208bfc1f8d"
FILENAME_2="HDep-n=Temps_u=s+n=NumSDom_g=0184x0187.v=f0000000000000000-v=f3f74bbcde85767c5"

NB_LOOP_1=10
NB_LOOP_2=100
NB_FILES=100

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
mkdir -p "$TEST_DIR/$SUB_DIR2"


F=0
while (( $F < $NB_FILES )) ; do
  touch "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F"
  (( F = $F + 1 ))
done


I=0
ERR=0

while (( $I < $NB_LOOP_1 )) ; do

  echo "test 1"
  
  F=0
  while (( $F < $NB_FILES )) ; do
  
    printf "."

    J=0 
    while (( $J < $NB_LOOP_2 )); do

      mv "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F"
        exit 1
      fi

      mv "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F"
        exit 1
      fi

      (( J = $J + 1))

    done
    
    (( F = $F + 1 ))
  done
  echo

  echo "test 2"

  F=0
  while (( $F < $NB_FILES )) ; do
  
    printf "."

    mv "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F"
    if (( $? != 0 )); then ((ERR=$ERR+1)); fi

    J=0
    while (( $J < $NB_LOOP_2 )); do

      mv "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR2/$FILENAME_2.$F"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR2/$FILENAME_2.$F"
        exit 1
      fi

      mv "$TEST_DIR/$SUB_DIR2/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR2/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F"
        exit 1
      fi

      mv "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F"
      if (( $? != 0 )); then
        ((ERR=$ERR+1))
        ls -li "$TEST_DIR/$SUB_DIR1/$FILENAME_2.$F" "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F"
        exit 1
      fi

      (( J = $J + 1))

    done

    mv "$TEST_DIR/$SUB_DIR2/$FILENAME_1.$F" "$TEST_DIR/$SUB_DIR1/$FILENAME_1.$F"
    if (( $? != 0 )); then ((ERR=$ERR+1)); fi
    
    (( F = $F + 1 ))
  
  done
  echo

  (( I = $I + 1 ))
  
done

rm -rf "$TEST_DIR/$SUB_DIR1"
rm -rf "$TEST_DIR/$SUB_DIR2"

(( NB_RENAME = 5*$NB_LOOP_2 + 2 ))
(( NB_RENAME = $NB_LOOP_1 * $NB_RENAME * $NB_FILES ))

echo "Test termine. $NB_RENAME rename effectues. $ERR erreurs."

