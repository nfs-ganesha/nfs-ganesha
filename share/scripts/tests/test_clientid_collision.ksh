for i in `cat /cea/T/home/s8/deniel/hostnames ` ;do
	../../../bin/RHEL_4__i686/test_clientid $i
done > /tmp/clientid_by_names

grep -- '-->'  /tmp/clientid_by_names > /tmp/t1

wc -l  /tmp/t1

cat /tmp/t1 | awk '{print $3;}' > /tmp/t2

wc -l /tmp/t2
sort -u /tmp/t2 | wc -l

for i in `cat /tmp/t2` ; do 
  j=`grep $i /tmp/t2 | wc -l`
  if (( $j > 1 )) ; then 
	grep $i /tmp/t2
	echo "--------------"
  fi
done
 
