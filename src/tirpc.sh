#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/nfs-ganesha/ntirpc.git'
TIRPC_COMMIT='c9e2f88f6551019ba881ee949d8044861ccd4c4c'

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
