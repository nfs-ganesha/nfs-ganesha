#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH_NAME='duplex-8-par-5'
TIRPC_COMMIT='19a4fcf63368e83b9331078d4264ed3a756f7452'

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

