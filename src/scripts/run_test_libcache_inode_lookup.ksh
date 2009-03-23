#!/bin/ksh 

OS=`archi -M`
../../bin/$OS/test_libcache_inode_lookup ../../share/conf/ghostfs.conf
