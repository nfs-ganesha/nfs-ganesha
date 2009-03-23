#!/bin/ksh 

OS=`archi -M`
../../bin/$OS/test_libcache_inode_readdir ../../share/conf/ghostfs.conf
