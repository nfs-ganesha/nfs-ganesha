#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/nfs-ganesha/ntirpc.git'
TIRPC_COMMIT='d3aed429b6dcc8a60333758f16a977b6deeb03ce'

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
