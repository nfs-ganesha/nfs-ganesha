#!/bin/sh

COMMANDE_CONTENU="find /etc -ls"

REPERTOIRE=$1
DATE=`date +"%d%m%Y"`

if [[ ! -d $REPERTOIRE ]]; then
   echo "$REPERTOIRE N'EST PAS UN REPERTOIRE"
   exit 1
fi


FICH_TEST="$REPERTOIRE/TEST_RW.$DATE"

echo "Creation d'un fichier temoin..."

TEMOIN="/tmp/TEST_RW.$DATE"

$COMMANDE_CONTENU > $TEMOIN

echo "Copie dans Ganesha (cp)..."

cp $TEMOIN $FICH_TEST

ls -l $TEMOIN $FICH_TEST

echo "Comparaison du contenu..."

diff $TEMOIN $FICH_TEST

echo "Copie de Ganesha vers /tmp (cp)..."

cp $FICH_TEST $TEMOIN.2

echo "Comparaison du temoin et du fichier exporte..."

diff $TEMOIN $TEMOIN.2

echo "Suppression du fichier dans Ganesha..."
rm $FICH_TEST 

echo "Copie dans Ganesha (cp -p)..."

cp -p $TEMOIN $FICH_TEST

ls -l $TEMOIN $FICH_TEST

echo "Comparaison du contenu..."

diff $TEMOIN $FICH_TEST

echo "Copie de Ganesha vers /tmp (cp -p)..."

cp -p $FICH_TEST $TEMOIN.2

echo "Comparaison du temoin et du fichier exporte..."

diff $TEMOIN $TEMOIN.2

echo "Suppression des fichiers du test..."
rm $FICH_TEST 
rm $TEMOIN
rm $TEMOIN.2
