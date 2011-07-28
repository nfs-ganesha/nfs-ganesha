#!/bin/sh 

valgrind  --leak-check=full --max-stackframe=3280592  --log-file=/tmp/valgrind.log $* 
