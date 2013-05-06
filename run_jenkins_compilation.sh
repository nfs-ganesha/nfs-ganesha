#!/bin/sh 
#
# External variables (to be set by Jenkins) are:
#  - FSAL (VFS,XFS,ZFS,GPFS,POSIX,HPSS,LUSTRE,CEPH,PROXY,FUSE,DYNFSAL)
#  - MFSL (NULL,NONE)
#  - sharedfsal (dynamic,static) 
#
BASE=`pwd`
mkdir -p build 
cd build 
cmake CCMAKE_BUILD_TYPE=Debug $BASE/src && make 

