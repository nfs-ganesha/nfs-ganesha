#!/bin/sh
res=`echo $1 | sed -e 's/[_-]/\n/g' | grep git | sed -e 's/git//g'`

if [ -z "$res" ] ; then
  echo "NOT-GIT"
else
  echo $res
fi

