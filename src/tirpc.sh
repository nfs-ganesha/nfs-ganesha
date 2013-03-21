#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_COMMIT='d75ed77681efdb32fa0e2b90aa2fd338bfcc145e'

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
