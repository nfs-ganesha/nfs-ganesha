#!/bin/sh 

./configure --with-fsal=DYNFSAL && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=XFS  --enable-buildsharedfsal && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=PROXY  --enable-buildsharedfsal  && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=ZFS  --enable-buildsharedfsal  && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=POSIX --with-db=PGSQL --enable-buildsharedfsal && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=XFS  && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=PROXY  && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=ZFS  && \
make clean && ( make -j 6 || make -j 3 || make )  && \

./configure --with-fsal=SNMP  && \
make clean && ( make -j 6 || make -j 3 || make ) && \

./configure --with-fsal=POSIX --with-db=PGSQL  && \
make clean && ( make -j 6 || make -j 3 || make )  && \


./configure --with-fsal=FUSE && \
make clean && ( make -j 6 || make -j 3 || make )  && \

echo "Ok"


