#!/bin/sh

# ce test cree plein d'entrees dans un repertoire
# et les rename une par une

TEST_DIR=$1

SUB_DIR1="mkdir_cascade-$$"

NAME="TEST_ENTRY"

NB_ITER=5000
((SEED=1999*$$+`date +%s`))
((RAND=((1999*$SEED + 857))%715827883))
LAST_VAL=0

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

function random
{
  MODULUS=$1
  
  ((RAND=((1999*$RAND + 857))%715827883))
  
  ((value=$RAND%$MODULUS))
  while (( $value < 0 )); do
    ((value=$value+$MODULUS))
  done
  
  LAST_VAL=$value
  
}

function create_file
{
  chemin=$1
  
  touch "$chemin/$NAME"
  status=$?
  if (( $status != 0 )); then
    echo "Error $status creating file $chemin/$NAME" >&2
    exit 1
  fi  
}

function create_dir
{
  chemin=$1
  mkdir "$chemin/$NAME"
  status=$?
  if (( $status != 0 )); then
    echo "Error $status creating subdirectory $chemin/$NAME" >&2
    exit 1
  fi  
}

function remove_file
{
  chemin=$1  
  rm -f "$chemin/$NAME"
  status=$?
  if (( $status != 0 )); then
    echo "Error $status removing file $chemin/$NAME" >&2
    exit 1
  fi  
}

function remove_dir
{
  chemin=$1  
  rmdir "$chemin/$NAME"
  status=$?
  if (( $status != 0 )); then
    echo "Error $status removing directory $chemin/$NAME" >&2
    exit 1
  fi  
}

function go_up
{
  chemin=$1
  upper=`dirname $chemin`
  echo "$upper"
}

function go_in
{
  chemin=$1
  echo "$chemin/$NAME"
}


# on choisi aleatoirement une sequence des actions suivantes:
# *si etat = vide :
# - on peut creer un fichier => etat=fichier
# - on peut creer un sous repertoire => etat=directory
# - on peut remonter d'un cran (si depth > 0 )
# *si etat = fichier :
# - on peut supprimer le fichier => etat = vide
# *si etat = directory :
# - on peut descendre dans ce repertoire => etat=vide
# - on peut supprimer ce repertoire => etat=vide

depth=0
path="$TEST_DIR/$SUB_DIR1"
etat=vide

I=0
while (( $I < $NB_ITER )) ; do

  case $etat in
    vide)
        random 4
        case $LAST_VAL in
          0)
            echo "Creating file $path/$NAME"
            create_file $path
            etat=fichier
            ;;
          1)
            echo "Creating dir $path/$NAME"
            create_dir $path
            etat=directory
            ;;
          2|3)
            # go up more often than down
            if (( $depth > 0 )); then            
              path=`go_up $path`
              echo "Going back to $path"
              etat=directory
              ((depth=$depth - 1))
            fi
            # sinon (depth=0) : on ne fait rien
            ;;
        esac
        ;;
    fichier)
        echo "Removing file $path/$NAME"
        remove_file $path
        etat=vide
        ;;
    directory)
        random 2
        case $LAST_VAL in
          0)
              path=`go_in $path`
              echo "Entering in $path"
              etat=vide
              ((depth=$depth + 1))
              ;;
          1)
              echo "Removing dir $path/$NAME"
              remove_dir $path
              etat=vide
              ;;
        esac
        ;;
  esac
  
  (( I = $I +1 ))

done

echo "Cleaning all..."
rm -rf "$TEST_DIR/$SUB_DIR1"
status=$?

if (( $status != 0 )); then
  echo "Error $status removing $TEST_DIR/$SUB_DIR1" >&2
  exit 1
fi  


exit 0

