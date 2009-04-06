#!/bin/bash

echo "==================================================================================="
echo " As you run this test for the first time, a few configuration options are needed"
echo " for beeing able to test access rights, and for creating the test tree."
echo
echo " Also, make sure that the filesystem options specified in 'all_fs.ganesha.nfsd.conf'"
echo " are set correctly (db options, server options, credential files...)"
echo "==================================================================================="

if [[ ! -f user1.sav ]]; then
	nok=1
	while (( $nok )); do	
		echo -n "Please enter a user's name: "
		read user1

		id $user1 > /dev/null 2> /dev/null
		if (( $? != 0 )); then
			echo "Invalid user '$user1' !"
		else
			nok=0
			echo "$user1" > user1.sav
		fi
	done
else
	user1=`cat user1.sav`
fi

group1=`id -g $user1`


if [[ ! -f user2.sav ]]; then
	nok=1
	while (( $nok )); do	
		echo -n "Then, enter a different user in the same group as $user1: "
		read user2

		id $user2 > /dev/null 2> /dev/null
		if (( $? != 0 )); then
			echo "Invalid user '$user2' !"
		else

			group2=`id -g $user2`
			
			if [[ x$group2 != x$group1 ]]; then
				echo "$user2 is not in the same group as $user1"
			else
				nok=0
				echo "$user2" > user2.sav
			fi
		fi
	done
else
	user2=`cat user2.sav`
fi


if [[ ! -f user3.sav ]]; then
        nok=1
        while (( $nok )); do
                echo -n "Now, enter another user in a group different from $user1 and $user2: "
                read user3

                id $user3 > /dev/null 2> /dev/null
                if (( $? != 0 )); then
                        echo "Invalid user '$user3' !"
                else
			group3=`id -g $user3`
			if [[ x$group3 == x$group1 ]]; then
                                echo "$user3 is in the same group as $user1 and $user2 !"
                        else
                        	nok=0
                        	echo "$user3" > user3.sav
			fi
                fi
        done
else
	user3=`cat user3.sav`
fi


if [[ ! -f testdir.sav ]]; then
        nok=1
        while (( $nok )); do
                echo -n "Enter a path in the exported file system where both $user1, $user2 and $user3 can create entries: "
                read testdir 
		if [[ ! -z $testdir ]]; then
                	echo "$testdir" > testdir.sav
			nok=0
		fi
        done
else
        testdir=`cat testdir.sav`
fi

