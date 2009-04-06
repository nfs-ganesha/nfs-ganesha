#!/bin/ksh


MNT_1=$1

if [[ ! -d $MNT_1 ]]; then
  echo "Usage $0 <mnt_dir>"
  exit 1
fi

DIR_1="$MNT_1/TEST_STALE.$$"


#creation du repertoire

mkdir "$DIR_1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding mkdir($DIR_1)"
  exit 1
fi

# creation fichier

touch "$DIR_1/file.1" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding touch($DIR_1/file.1)"
  exit 1
fi

touch "$DIR_1/file.2" > /dev/null

status=$?

if (( $status != 0 )); then
  echo "ERROR status proceeding touch($DIR_1/file.2)"
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

echo "###############################################"
echo
echo "Maintenant, supprimer le repertoire $DIR_1"
echo "directement dans le filesystem (via scrub)"
echo "(commande 'unlink TEST_STALE.$$ recurse top')"
echo
echo "Taper ensuite <enter> pour continuer"
echo
echo "###############################################"

read input

echo "On tente de supprimer le repertoire STALE"
rm -rf "$DIR_1"

# listing dans le second repertoire
echo "On tente de lister le repertoire STALE"
ls -l "$DIR_1"

echo "Entree dans le parent"
ls -l "$MNT_1" | grep "TEST_STALE.$$"

echo "On attend 5s et on liste a nouveau"
sleep 5
ls -l "$MNT_1" | grep "TEST_STALE.$$"
