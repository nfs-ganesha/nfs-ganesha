#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/nfs-ganesha/ntirpc.git'
TIRPC_COMMIT='2f49149b248da15eb21beca71dffa6704349f040'

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
