#!/bin/sh

cd src 

./configure --with-fsal=DYNFSAL && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=XFS  --enable-buildsharedfsal && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=PROXY  --enable-buildsharedfsal  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=ZFS  --enable-buildsharedfsal  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=POSIX --with-db=PGSQL --enable-buildsharedfsal && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=XFS  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=PROXY  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=ZFS  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=SNMP  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

./configure --with-fsal=POSIX --with-db=PGSQL  && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&


./configure --with-fsal=FUSE && make rpms && \
cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp && make clean && make rpms && cp /opt/GANESHA/src/rpm/RPMS/x86_64/*.rpm /tmp &&

echo "Ok"



