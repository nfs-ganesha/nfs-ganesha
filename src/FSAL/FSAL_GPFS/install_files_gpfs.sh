#!/bin/sh

mkdir -p /usr/lpp/mmfs/
cp -r ./include/ /usr/lpp/mmfs/
cp -r ./lib/ /usr/lpp/mmfs/
ln -s /usr/lpp/mmfs/include/gpfs.h /usr/include/
ln -s /usr/lpp/mmfs/include/gpfs.h /usr/include/gpfs_gpl.h
ln -s /usr/lpp/mmfs/include/gpfs_nfs.h /usr/include/
ln -s /usr/lpp/mmfs/include/gpfs_fcntl.h /usr/include/
ln -s /usr/lpp/mmfs/include/gpfs_lweTypes.h /usr/include/
ln -s /usr/lpp/mmfs/lib/i386/libgpfs.so /usr/lib/
ln -s /usr/lpp/mmfs/lib/libgpfs.so /usr/lib64/

