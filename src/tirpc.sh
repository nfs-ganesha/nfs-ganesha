#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_COMMIT='d33e68fba7e6ed07da2698951a2fd6f922ca5bf7'

if [ -d libtirpc/.git ]; then
    cd libtirpc
    git remote update --prune
else
    git clone ${TIRPC_REPO} libtirpc
    cd libtirpc
fi

git checkout --quiet ${TIRPC_COMMIT}
cd ${OPWD}

./autogen.sh
