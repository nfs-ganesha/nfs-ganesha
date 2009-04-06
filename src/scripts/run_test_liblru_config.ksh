#!/bin/ksh 

OS=`archi -M`
../../bin/$OS/test_liblru_config < ../scripts/test_lru1.tst
