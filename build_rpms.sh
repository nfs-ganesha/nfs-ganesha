#!/bin/bash

# Make sure you have correct libntirpc submodule checked out. For
# example, the following would checkout the needed submodules
#
# git clone --branch=ibm2.7 --recursive https://github.com/ganltc/nfs-ganesha.git
# ./build_rpms.sh # to build rpms for RHEL and SLES

# Remove build directory with force option.
if [[ $# -eq 1 && $1 == "-f" ]]
then
	rm -rf build
fi

mkdir build &&                                                             \
cd build && 								   \
cmake ../src -DBUILD_CONFIG=rpmbuild -DCMAKE_BUILD_TYPE=Release            \
	-DUSE_ADMIN_TOOLS=ON -DUSE_GUI_ADMIN_TOOLS=OFF                     \
	-DUSE_DBUS=ON                                                      \
	-D_MSPAC_SUPPORT=ON                                                \
	-DRADOS_URLS=OFF -DUSE_RADOS_RECOV=OFF               		   \
	-DUSE_9P=OFF       						   \
	-DUSE_FSAL_NULL=OFF      					   \
	-DUSE_FSAL_XFS=OFF 						   \
	-DUSE_FSAL_MEM=OFF 						   \
	-DUSE_FSAL_LUSTRE=OFF			                           \
	-DUSE_FSAL_CEPH=OFF						   \
	-DUSE_FSAL_RGW=OFF			 			   \
	-DUSE_FSAL_PANFS=OFF						   \
	-DUSE_FSAL_GLUSTER=OFF						   \
	-DUSE_FSAL_PROXY=OFF						   \
	-DUSE_FSAL_GPFS=ON  &&  					   \
make dist &&                                                               \
QA_RPATHS=2 rpmbuild -ta nfs-ganesha*.tar.gz
