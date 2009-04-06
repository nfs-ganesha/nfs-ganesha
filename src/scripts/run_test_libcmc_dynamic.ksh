#!/bin/ksh 

OS=`archi -M`
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:../../lib/$OS
../../bin/$OS/test_libcmc_dynamic
