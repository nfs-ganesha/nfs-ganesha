#!/bin/sh

# update/fetch linked git modules
cd ..
git submodule init
git submodule update
cd -

# configure source tree
rm -f config.cache
aclocal -I m4
libtoolize --force --copy
autoconf
autoheader
automake -a --add-missing -Wall

