#!/bin/sh 

export CC='gcc -g -Werror -Wimplicit -Wformat -Wmissing-braces'
export CFLAGS='-g -Werror -Wimplicit -Wformat -Wmissing-braces'

./configure $* 
