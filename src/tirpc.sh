#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/linuxbox2/ntirpc.git'
TIRPC_COMMIT='5d88bd500b6aa34eba8acc63009a6519f4e3d696'

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
