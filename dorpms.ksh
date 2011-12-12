#!/bin/sh

cd src 

./configure --with-fsal=DYNFSAL --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=XFS  --enable-buildsharedfsal --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=PROXY  --enable-buildsharedfsal  --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=ZFS  --enable-buildsharedfsal --enable-nlm  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=POSIX --with-db=PGSQL --enable-buildsharedfsal --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=XFS  --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=PROXY  --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=ZFS  --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=POSIX --with-db=PGSQL  --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=LUSTRE --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=FUSE --enable-nlm && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

echo "Ok"



