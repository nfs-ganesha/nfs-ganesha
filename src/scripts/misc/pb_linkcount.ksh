#!/bin/ksh

##### LE TEST SUPPOSE QUE LE FILESYSTEM EST MONTE SUR LE REPERTOIRE /mnt #####

set -x trace

TEST_REP=/mnt/dir.$$

# creation d'une respertoire vide
mkdir $TEST_REP

# on se place dedans
cd $TEST_REP

# on cree un nouveau repertoire
mkdir toto

# find gueule car le linkcount n'est pas bon
find . -ls
