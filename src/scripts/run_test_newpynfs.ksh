#!/bin/ksh 

LIST="LOOKLINK ACC1a LOOKDIR ACC1d LOOKFILE ACC1r"

cd newpynfs
#./testserver.py pinatubo1:/users/test/leibovic --uid=0 --gid=0 --maketree $LIST
./testserver.py pinatubo1:/users/test/leibovic --uid=0 --gid=0 --no-init $LIST
cd - 

