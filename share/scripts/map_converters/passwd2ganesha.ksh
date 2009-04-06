#!/bin/ksh 
#
# This scripts turns a 'passwd' map (like /etc/passwd for example) to a GANESHA UIDMAPPER file
#
# Use: cat /etc/passwd | passwd2ganesha > /etc/ganesha.pwfile
#      ypcat passwd    | passwd2ganesha > /etc/ganesha.pwfile
#      getent passwd   | passwd2ganesha > /etc/ganesha.pwfile
#

awk -F ':' 'BEGIN { print "Users\n{" ;}{ print "\t"  $1 " = " $3 ";" ;}END{print"}";}'

