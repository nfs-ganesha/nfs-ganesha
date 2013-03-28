#!/bin/sh -x

# To be set externally:
# SERVERIP
# BUILT_SERVER
# SIGMUND

if [[ -z $BUILT_SERVER || -z $SERVERIP || -z $SIGMUND ]] ; then
	echo "You must set SERVERIP, BUILT_SERVER and SIGMUND"
	exit 1
fi

# Make sure no other ganesha than the one we want to test is running
ssh $SERVERIP "pkill -9 ganesha.nfsd" || exit 1

# Copy binaries
scp $BUILT_SERVER/MainNFSD/ganesha.nfsd $SERVERIP:/tmp || exit 1
scp  $BUILT_SERVER/FSAL/FSAL_VFS/libfsalvfs.so.4.2.0  $SERVERIP:/tmp || exit 1
scp ganesha.test.conf  $SERVERIP:/tmp || exit 1


# start Ganesha remotely
ssh $SERVERIP "nohup /tmp/ganesha.nfsd -d -L /tmp/ganesha.log.$$ -f /tmp/ganesha.test.conf" &

# Wait 5 seconds
echo "Wait 60 seconds. The server must quit grace period"
sleep 60

# mount Ganesha
mount -o vers=3,lock $SERVERIP:/tmp /mnt || exit 1

# Run sigmund test as root
$SIGMUND nfs -j ./sigmund_as_root.rc

# cleanup
umount /mnt
ssh $SERVERIP "pkill -9 ganesha.nfsd" 
