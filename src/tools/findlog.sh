#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright IBM Corporation, 2011
#  Contributor: Frank Filz  <ffilz@us.ibm.com>
#
#
# This software is a server that implements the NFS protocol.
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
#
# This script is primarily intented to extract all the LogXXX functions calls
# from the source code, however it can also be used to find other function calls
# given certain limitations.
#
# Each function call found will have all of it's parameters pulled onto a single
# output line, with some white space trimming, and the output line will be
# tagged with the file name and line number.
#
# Limitation 1:
#
#    There must be a blank or tab before the function name.
#
# Limitation 2:
#
#    Code like th following will stump this script somewhat:
#
#       #define DEFINED_TWICE_WARNING( _str_ ) \
#         LogWarn(COMPONENT_CONFIG,            \
#                 "NFS READ_EXPORT: WARNING: %s defined twice !!! (ignored)", _str_ )
#
#    The result will be to pull in extra code
#
# Note:
#
# The file tools/test_findlog.c can be used to see the types of things this
# script is expected to deal with. If you find a new case the script doesn't
# deal with, add it to this file, and possibly fix the script.
#-------------------------------------------------------------------------------

# NOTES:
FUNC="Log[a-zA-Z][a-zA-Z0-9]*"
PRINTF="[vsnf]*printf"
FILES=""
TODO="find_func_in_file"
CSCOPE=0
FINAL="final_massage"
LINESONLY=0
DIR="."

find_funcs()
{
	sed -ne "
		:again
		/[^a-zA-Z_]$FUNC[ \t\]*(.*\*\//{
			=
			p
			s/\*/\*/
			t end
			}
		/[^a-zA-Z_]$FUNC[ \t]*(.*)[ \t]*;[ \t\\]*$/{
			=
			p
			s/;/;/
			t end
			}
		/[^a-zA-Z_]$FUNC[ \t]*(.*)[ \t]*{[ \t\\]*$/{
			=
			p
			s/{/{/
			t end
			}
		/[^a-zA-Z_]$FUNC[ \t]*(.*$/{
			=
			N
			s/\n[ \t]*/ /
			s/[ \t\\]*;[ \t\\]*$/;/
			t again
			}
		/[^a-zA-Z_]$FUNC[ \t]*[\[\=]/d
		/[^a-zA-Z_]$FUNC[^)]*)/d
		/[^a-zA-Z_]$FUNC[^;()]*;/d
		/[^a-zA-Z_]$FUNC[ \t\\]*$/{
			=
			N
			s/[ \t\\]*;[ \t\\]*$/;/
			s/\n[ \t]*(/(/
			t again
			}
		:end"
}

debug_mode()
{
	cat $1 | find_funcs
}

no_xref()
{
	cat $1 | find_funcs | grep -v '^[0-9][0-9]*$' | sed -e "s@.*\($FUNC.*\)@\1@"
}

final_massage()
{
	sed -e "s@\([0-9][0-9]*\):[0-9:]*[ \t]*\(.*$FUNC.*\)@$1:\1: \2@"
}

final_lines_only()
{
	sed -e "s@\([0-9][0-9]*\):[0-9:]*[ \t]*\(.*$FUNC.*\)@$1:\1@"
}

find_func_in_file()
{
	cat $1 | find_funcs | sed -ne '
		:done
		/[0-9][0-9]*:.*\*\//{
			p
			d
			}
		/[0-9][0-9]*:.*[{;][ \t\\]*$/{
			p
			d
			}
		/[0-9][0-9]*/{
			N
			s/\n/:/
			s/;/;/
			t done
			}
		' \
	| $FINAL $1
}

find_files()
{
	while read line; do
		$TODO $line
	done
}

while getopts ":f:l:cp\?k:dxsnD:" OPT; do
	case $OPT in
		f)	FUNC="$OPTARG"
			;;
		l)	FILES="$OPTARG"
			;;
		c)	FILES="cscope.files"
			;;
		p)	FUNC="$PRINTF"
			;;
		d)	TODO="debug_mode"
			;;
		D)	DIR="$OPTARG"
			;;
		s)	CSCOPE=1
			;;
		x)	TODO="no_xref"
			;;
		n)	LINESONLY=1
			FINAL="final_lines_only"
			;;
		\?)	echo "Usage: findlog.sh [-f function] [-l file] [-p] [-c] [-s] [-x] [-n] [list of files]"
			echo
			echo "Extract all examples of specified function calls from the files, combining all"
			echo "lines of the function call onto one line"
			echo
			echo "  -f function   grep pattern defining function, default is \"$FUNC\""
			echo "  -l file       file containing list of files to search"
			echo "  -D dir        search in directory"
			echo "  -c            equivalent of -l cscope.files"
			echo "  -p            search for printf, equivalent of -f \"$PRINTF\""
			echo "  -d            debug mode, don't massage the output"
			echo "  -s            call cscope instead of using script"
			echo "  -x            don't output file names and line numbers"
			echo "  -n            output file names and line numbers only"
			echo
			echo "if no files are specified, will do a find . -name '*.[ch]'"
			exit
			;;
	esac
done

if [ $CSCOPE -eq 1 ]
then
	if [ $LINESONLY -eq 0 ]
	then
		cscope -d -L -0 $FUNC | sed -e "s@\([^ ][^ ]*\) [^ ][^ ]* \([0-9][0-9]*\) \(Log.*\)@\1:\2:\3@"
	else
		cscope -d -L -0 $FUNC | sed -e "s@\([^ ][^ ]*\) [^ ][^ ]* \([0-9][0-9]*\) \(Log.*\)@\1:\2@"
	fi
	exit
fi

shift $(($OPTIND - 1))

if [ -n "$FILES" ]
then
	cat $FILES | find_files
	if [ -z "$1" ]
	then
		exit
	fi
fi

if [ -z "$1" ]
then
	find $DIR -name '*.[ch]' | find_files
fi

while [ -n "$1" ]
do
	$TODO $1
	shift
done
