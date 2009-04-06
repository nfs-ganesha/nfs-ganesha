#!/bin/ksh


MNT_1=$1

if [[ ! -d $MNT_1 ]]; then
  echo "Usage $0 <mnt_dir>"
  exit 1
fi

DIR_1="$MNT_1/TEST_STALE.$$"
DIR_2="$MNT_1/TEST_STALE.$$_bis"


#creation du repertoire 1

mkdir "$DIR_1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding mkdir($DIR_1)"
  exit 1
fi

# creation fichier1

touch "$DIR_1/file.1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding touch($DIR_1/file.1)"
  exit 1
fi

ls -l "$DIR_1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding ls ($DIR_1)"
  exit 1
fi

ls -l "$MNT_1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding ls ($DIR_1)"
  exit 1
fi


#creation du repertoire 2

mkdir "$DIR_2" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding mkdir($DIR_2)"
  exit 1
fi

ls -l "$DIR_2" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding ls ($DIR_2)"
  exit 1
fi


echo "###############################################"
echo
echo "Maintenant, supprimer le fichier $DIR_1/file.1"
echo "directement dans le filesystem (via scrub)"
echo "(commande 'unlink TEST_STALE.$$/file.1')"
echo
echo "Taper ensuite <enter> pour continuer"
echo
echo "###############################################"

read input


echo "On tente de deplacer un fichier source supprime"

mv "$DIR_1/file.1" "$DIR_2/file.1"


echo "L'entree source :"
ls -l "$DIR_1/file.1"

echo "L'entree destination :"
ls -l "$DIR_2/file.1"

echo "Entrees dans le repertoire parent"
ls -l "$MNT_1" | grep "TEST_STALE.$$"

echo "On attend 5s et on liste a nouveau"
sleep 5
ls -l "$MNT_1" | grep "TEST_STALE.$$"
