#!/bin/sh 

OPWD=`pwd`

TIRPC_REPO='https://github.com/mattbenjamin/libtirpc-lbx.git'
TIRPC_BRANCH='duplex-3'

cd ../contrib
git clone -b ${TIRPC_BRANCH} ${TIRPC_REPO} libtirpc
cd ${OPWD}

./autogen.sh

