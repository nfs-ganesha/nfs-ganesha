#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH='duplex-7'
TIRPC_COMMIT='9c1b29ba7bb7562a42b930c7ecc3ec41d83c589a'

# remove libtirpc if present;  try to avoid making
# a mess
if [ -d ../src -a -d ../contrib ]; then
    if [ -e libtirpc ]; then
	rm -rf libtirpc
    fi
fi

git clone ${TIRPC_REPO} libtirpc
cd libtirpc
git checkout -b origin/${TIRPC_BRANCH}
git checkout ${TIRPC_COMMIT}
cd ${OPWD}

./autogen.sh

