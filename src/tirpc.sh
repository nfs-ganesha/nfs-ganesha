#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_BRANCH_NAME='duplex-9'
TIRPC_COMMIT='35087a24be5d5c304c3128e583b5486cde5eb23b'

# remove libtirpc if present;  try to avoid making
# a mess
if [ -d ../src -a -d ../contrib ]; then
    if [ -e libtirpc ]; then
	rm -rf libtirpc
    fi
fi

git clone ${TIRPC_REPO} libtirpc
cd libtirpc
git checkout ${TIRPC_COMMIT}
cd ${OPWD}

./autogen.sh

