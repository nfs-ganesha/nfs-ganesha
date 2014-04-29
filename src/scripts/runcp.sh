#!/bin/sh

# Find directory where runcp.sh lives for checkpatch will be there too
# We keep that in CURDIR
CURDIR=$(dirname $(readlink -m $0))

check_one_file()
{
	FDIR=`echo $1 | sed -e "s@$DIR\(.*\)/.*@$ODIR\1@"`
	OUTFILE=`echo $1 | sed -e "s@$DIR\(.*\)@$ODIR\1.cp@"`

	#echo FIDR=$FIDR DIR=$DIR FILE=$1 OUTFILE=$OUTFILE

	if [ $ONEFILE -eq 1 ]
	then
		OUTFILE=$TEMP
	elif [ ! -d $FDIR ]
	then
		if [ -e $FDIR ]
		then
			echo "Oops, $FDIR is not a directory"
			return
		fi

		mkdir -p $FDIR
	fi

	EXTRA_OPT=""

	if [ $NO_SPACING -eq 1 ]
	then
		echo $1 > $OUTFILE

		egrep $NO_SPACING_FILES $OUTFILE 2>&1 >/dev/null

		RC=$?

		if [ $RC -eq 0 ]
		then
			EXTRA_OPT="$EXTRA_OPT --ignore SPACING"
		fi

		egrep $NO_COMPLEX_MACRO_FILES $OUTFILE 2>&1 >/dev/null

		RC=$?

		if [ $RC -eq 0 ]
		then
			EXTRA_OPT="$EXTRA_OPT --ignore COMPLEX_MACRO"
		fi

		egrep $NO_DEEP_INDENTATION_FILES $OUTFILE 2>&1 >/dev/null

		RC=$?

		if [ $RC -eq 0 ]
		then
			EXTRA_OPT="$EXTRA_OPT --ignore DEEP_INDENTATION"
		fi

		egrep $NO_BRACKET_SPACE_FILES $OUTFILE 2>&1 >/dev/null

		RC=$?

		if [ $RC -eq 0 ]
		then
			EXTRA_OPT="$EXTRA_OPT --ignore BRACKET_SPACE"
		fi
	fi

	$CURDIR/checkpatch.pl $TYPEDEF $EXTRA_OPT \
		 --file $1 > $OUTFILE 2> $ERROR_FILE

	RESULT=`grep '^total:'  $OUTFILE`

	if [ -n "$REPORT_FILT" ]
	then
		grep "$REPORT_FILT" $OUTFILE > /dev/null 2>&1
		RC=$?
	else
		RC=1
	fi

	if [ $RC -eq 1 ] || [ -s $ERROR_FILE ]
	then
		if [ $ONEFILE -eq 1 ]
		then
			cat $OUTFILE >>$REPORT_FILE
			cat $ERROR_FILE >>$REPORT_FILE
		else
			echo $1 $RESULT >>$REPORT_FILE
			cat $ERROR_FILE | sed -e "s@\(.*\)@$1 \1@" >>$REPORT_FILE
			cat $ERROR_FILE >>$OUTFILE
		fi
		if [ $QUIET -eq 0 ]
		then
			echo $1 $RESULT
			cat $ERROR_FILE | sed -e "s@\(.*\)@$1 \1@"
		fi
	fi
}

check_files()
{
	while [ -n "$1" ]
	do
		if [ -s "$1" ]
		then
			check_one_file $1
		fi
		shift
	done
}

check_find()
{
	check_files `find $DIR -name '*.[ch]' | egrep -v "$EXCLUDE" | sort`
}

check_git_files()
{
	while [ -n "$1" ]
	do
		check_one_file $DIR/$1
		shift
	done
}

check_git()
{
	echo "diff --name-only $COMMIT | egrep -v $EXCLUDE"
	git diff --name-only $COMMIT | egrep -v "$EXCLUDE"
	check_git_files `git diff --name-only $COMMIT | egrep -v "$EXCLUDE" | sort`
}

show_help()
{
	cat << ...
Options:
-c       Don't show \"Clean\" files (files with no errors and no warnings)
-w       Don't show files that had no errors (even if they have warnings)
         -c prevails over -w
-q       Quiet, don't show report in progress
-1       Put the results of each check into a separate file in the output dir
         for example, results for checkpatch of
             foo/test.c
         would show up in
             /tmp/checkpatch/foo/test.c.cp
-d {dir} Directory to check (including sub-directories), defaults to current
         working directory:
-x       Exclude files egrep expression (libtirpc|libntirpc is prepended to the
         expression)
-v       turn off:
         --ignore SPACING on RPC program header files (like nfs23.h) and
         --ignore COMPLEX_MACRO on certain files
         --ignore BRACKET_SPACE in certain files
         --ignore DEEP_INDENTATION in certain files
-i       Include files agreed on to ignore (config_parsing|Protocols/XDR
-e       Include files from external prohects (murmur3|cidr|atomic_x86|city)
-g       Use git-diff --name-only instead of find (-d will be ignored)
-k       Specify commit for git-diff, default is HEAD
-o {dir} Directory to direct output files to, defaults to /tmp/checkpatch
-r       Output report at end
-t       Ignore typedefs
...
}

CLEAN=0
DIR="."

ALWAYS="libtirpc|libntirpc|CMakeFiles|tools/test_findlog.c|include/config.h"

EXTERNAL="murmur3.h|cidr.h|cidr/|atomic_x86_64.h|include/city|avltree.h"
EXTERNAL="$EXTERNAL|test/test_atomic_x86_86.c|avl/|FSAL/FSAL_GPFS/include"

NO_EXTERNAL=0

IGNORE="config_parsing|Protocols/XDR|NodeList|include/nodelist.h"
IGNORE="$IGNORE|include/gsh_intrinsic.h"

NO_IGNORE=0
EXCLUDE=""
QUIET=0
ODIR=/tmp/checkpatch
NOWARN=0
ONEFILE=1
REPORT=0
FIND=check_find
COMMIT=HEAD
TYPEDEF=""

NO_SPACING_FILES="nfs23.h|nfsv41.h|nlm4.h|nsm.h|rquota.h"

NO_COMPLEX_MACRO_FILES="include/ganesha_dbus.h|include/server_stats_private.h"
NO_COMPLEX_MACRO_FILES="$NO_COMPLEX_MACRO_FILES|include/gsh_intrinsic.h"
NO_COMPLEX_MACRO_FILES="$NO_COMPLEX_MACRO_FILES|support/exports.c"
NO_COMPLEX_MACRO_FILES="$NO_COMPLEX_MACRO_FILES|support/export_mgr.c"

NO_DEEP_INDENTATION_FILES="cache_inode/cache_inode_lru.c|include/rbt_tree.h"

NO_BRACKET_SPACE_FILES="include/nfs_req_queue.h"

NO_SPACING=1

while getopts "cd:x:qo:?w1rgk:tiev" OPT; do
	case $OPT in
		c)	CLEAN=1
			;;
		w)	NOWARN=1
			;;
		q)	QUIET=1
			;;
		1)	ONEFILE=0
			;;
		d)	DIR=$OPTARG
			;;
		x)	EXCLUDE="$OPTARG"
			;;
		i)	NO_IGNORE=1
			;;
		e)	NO_EXTERNAL=1
			;;
		g)	FIND=check_git
			;;
		k)	COMMIT=$OPTARG
			;;
		o)	ODIR=$OPTARG
			;;
		r)	REPORT=1
			;;
		t)	TYPEDEF="--ignore NEW_TYPEDEFS"
			;;
		v)	NO_SPACING=0
			;;
		?)	show_help
			exit
			;;
	esac
done

if [ $DIR = '.' ]
then
	DIR=`pwd`
fi

if [ ! -d $ODIR ]
then
	if [ -e $ODIR ]
	then
		echo "Oops, $ODIR is not a directory"
		return
	fi

	mkdir -p $ODIR
fi

if [ $FIND = check_git ]
then
	DIR=`git rev-parse --show-toplevel`
fi

REPORT_FILE=$ODIR/results.cp
TEMP=$ODIR/results.temp
ERROR_FILE=$ODIR/results.err

date > $REPORT_FILE

if [ $FIND = check_git ]
then
	echo "FILES: git-diff --name-only $COMMIT" >> $REPORT_FILE
else
	echo "FILES: find $DIR -name '*.[ch]'" >> $REPORT_FILE
fi

echo "CLEAN=$CLEAN NOWARN=$NOWARN TYPEDEF=$TYPEDEF" >> $REPORT_FILE

if [ $NO_SPACING -eq 1 ]
then
	echo >> $REPORT_FILE
	echo "NO_SPACING_FILES=$NO_SPACING_FILES" >> $REPORT_FILE
	echo "NO_COMPLEX_MACRO_FILES=$NO_COMPLEX_MACRO_FILES" >> $REPORT_FILE
	echo "NO_DEEP_INDENTATION_FILES=$NO_DEEP_INDENTATION_FILES" >> $REPORT_FILE
	echo "NO_BRACKET_SPACE_FILES=$NO_BRACKET_SPACE_FILES" >> $REPORT_FILE
fi

if [ -n "$EXCLUDE" ]
then
	echo >> $REPORT_FILE
	echo "EXCLUDE=$EXCLUDE" >> $REPORT_FILE
fi

if [ $NO_IGNORE -eq 0 ]
then
	if [ -n "$EXCLUDE" ]
	then
		EXCLUDE="$IGNORE|$EXCLUDE"
	else
		EXCLUDE="$IGNORE"
	fi

	echo >> $REPORT_FILE
	echo "IGNORE=$IGNORE" >> $REPORT_FILE
fi

if [ $NO_EXTERNAL -eq 0 ]
then
	if [ -n "$EXCLUDE" ]
	then
		EXCLUDE="$EXTERNAL|$EXCLUDE"
	else
		EXCLUDE="$EXTERNAL"
	fi

	echo >> $REPORT_FILE
	echo "EXTERNAL=$EXTERNAL" >> $REPORT_FILE
fi

EXCLUDE="$ALWAYS|$EXCLUDE"

echo >> $REPORT_FILE

if [ $QUIET -eq 0 ]
then
	cat $REPORT_FILE
fi

if [ $CLEAN -eq 1 ]
then
	REPORT_FILT="^total: 0 errors, 0 warnings"
elif [ $NOWARN -eq 1 ]
then
	REPORT_FILT="^total: 0 errors"
else
	REPORT_FILT=""
fi

$FIND

if [ -e $TEMP ]
then
	rm -f $TEMP
	rm -f $ERROR_FILE
fi

if [ $REPORT -eq 1 ]
then
	cat $REPORT_FILE
fi
