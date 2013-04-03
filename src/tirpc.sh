#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_COMMIT='7aec2269a15aa7d68991fd083077c78a77ffc6e7'

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
