#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH_NAME='duplex-8'
TIRPC_COMMIT='9068c15a9a9ff7e3c4c33be8e343cf22b964c591'

# remove libtirpc if present;  try to avoid making
# a mess
if [ -d ../src -a -d ../contrib ]; then
    if [ -e libtirpc ]; then
	rm -rf libtirpc
    fi
fi

git clone ${TIRPC_REPO} libtirpc
cd libtirpc
git checkout -b $TIRPC_BRANCH_NAME ${TIRPC_COMMIT}
cd ${OPWD}

./autogen.sh

