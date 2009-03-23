#!/bin/ksh 

OS=`archi -M`
../../bin/$OS/test_libcache_inode ../../share/conf/ghostfs.conf 
