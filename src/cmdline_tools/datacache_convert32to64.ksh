#!/bin/ksh

CHEMIN=$1

CONVERT_CMD=./convert_hpss_indexfile_32to64.pl
BKP_DIR=$CHEMIN/bkp

# Les index valides se terminent par 00000000 (padding)
find $CHEMIN -name "*.index" -exec grep -H  FSAL {} \; | egrep -v "00000000$" | cut -d : -f 1 > /tmp/bad_indexes.out

for i in `cat /tmp/bad_indexes.out`; do
	NEW=$i.new

	SUBDIR=`dirname $i | sed -e s#$CHEMIN##`
	
	BKP=$BKP_DIR/$SUBDIR
	FILE=`basename $i`
	
	echo "mkdir -p $BKP"
	echo "$CONVERT_CMD $i > $NEW"
	echo "mv $i $BKP/$FILE"
	echo "mv $NEW $i"
	
done
