#!/bin/sh -x
#
# External variables (to be set by Jenkins) are:
#  - FSAL (VFS,XFS,ZFS,GPFS,POSIX,HPSS,LUSTRE,CEPH,PROXY,FUSE,DYNFSAL)
#  - MFSL (NULL,NONE)
#  - sharedfsal (dynamic,static) 
#
BASE=`pwd`

cd src
autoreconf --install || exit 1 

failed="FALSE"

if [[  $sharedfsal == "dynamic" ]] ; then
  DYNOPT=" --enable-buildsharedfsal"
else
  DYNOPT=""
fi

./configure --with-fsal=$FSAL --with-db=PGSQL --with-nfs4-minorversion=1 --with-mfsl=$MFSL --enable-nlm --enable-snmp-adm $DYNOPT|| exit 1 


make -j 2 || make -j 2 || make || failed="TRUE"

make clean

cd ..

# If many compilation occurs, clean the workspace
rm -fr ./*


if [[ $failed == "TRUE" ]] ; then
  exit 1
else
  exit 0 
fi
