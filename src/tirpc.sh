#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH='duplex-7'
TIRPC_COMMIT='16d84ae09c64a453ec4f01fb49733ae15eee77a0'

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

