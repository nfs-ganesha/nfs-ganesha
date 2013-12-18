#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/nfs-ganesha/ntirpc.git'
TIRPC_COMMIT='2f10e28a739f2d561c70c18a666fcc55da260864'

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
