#!/bin/sh
rm -r src/debian
echo "Creating build dependencies ..."
mk-build-deps --install debian/control
echo "Building packages ..."
dpkg-buildpackage -b -us -uc

