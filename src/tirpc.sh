#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/nfs-ganesha/ntirpc.git'
TIRPC_COMMIT='27a92ef1324b140bb2b4f18230dcc03c32827faa'

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
