#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH_NAME='duplex-7-for-ah'
TIRPC_COMMIT='161e4d06c06485a8bfaa30100ac0695b393e62dc'

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

