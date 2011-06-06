#!/bin/sh 

export CC='gcc -g -Werror'
export CFLAGS='-g -Werror'

./configure $* 
