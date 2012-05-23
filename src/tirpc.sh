#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='git://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH='duplex-4'

cd ../contrib
git clone ${TIRPC_REPO} libtirpc
cd libtirpc
git checkout -b ${TIRPC_BRANCH} origin/${TIRPC_BRANCH}
cd ${OPWD}

./autogen.sh

