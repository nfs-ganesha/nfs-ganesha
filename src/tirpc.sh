#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_COMMIT='9d926718d27fdbdff0f6a8b2739f4515cd290975'

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
