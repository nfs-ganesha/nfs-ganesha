#!/bin/sh
res=`echo $1 | sed -e 's/^.*_desc_//g'  | sed -e 's/-[0-9]*\.[0-9]*\.[0-9]*$//g'`

if [ -z "$res" ] ; then
  echo "NO-GIT"
else
  echo $res
fi

