#!/bin/sh -x
#
# External variables (to be set by Jenkins) are:
#  - FSAL
#  - NFS_VERS
#  - SERVER 
#

BASE=`pwd`

# Make the related export entries in *.ganesha.nfsd.conf file
# To be done : had other FSALs configuration
case $FSAL in
  VFS)
     header=vfs
     mntdirv3=/tmp
     mntdirv4=/vfs
     ;;
  XFS)
     header=xfs
     mntdirv3=/xfs
     mntdirv4=/fs/xfs
     ;;
  *)
     echo "unsupported FSAL $FSAL"
     exit 1 
     ;;
esac

case $NFS_VERS in
  3)
    MOUNT_CMD="mount -o vers=3,lock $SERVER:$mntdirv3 /mnt" 
    ;;
  4)
    MOUNT_CMD="mount -o vers=4,lock $SERVER:$mntdirv4 /mnt" 
    ;;
  4.1)
    MOUNT_CMD="mount -o vers=4,minorversion=1,lock $SERVER:$mntdirv4 /mnt" 
    ;;
  *)
    echo "unsupported NFS_VERS $NFS_VERS"
    exit 1 
    ;;
esac

# Compile ganesha
cd src
autoreconf --install || exit 1 

df -k .

failed="FALSE"

./configure --with-fsal=$FSAL  --with-nfs4-minorversion=1 --enable-nlm || exit 1 

make clean 

make -j 3 || make -j 2 || make 

# Copy ganesha to server
ssh $SERVER "pkill -9 ganesha "
scp ./MainNFSD/$header.ganesha.nfsd $SERVER:/tmp

ssh $SERVER "/tmp/$header.ganesha.nfsd -d -L /tmp/ganesha.log -f /root/$header.ganesha.nfsd.conf -p /tmp/pid.ganesha.$$ &" 

# run the test
sleep 5

umount -f /mnt 
$MOUNT_CMD || exit 1 
cd $BASE/non_regression_tests/thomasthon ; ./run_tests.sh  /mnt || exit 1
umount -f /mnt

ssh $SERVER "pkill -9 ganesha "

echo
echo "===== thomasthon test is OK ===="
echo 
exit 0 
