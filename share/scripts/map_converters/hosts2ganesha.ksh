#!/bin/ksh 
#
# This scripts turns a 'passwd' map (like /etc/passwd for example) to a GANESHA UIDMAPPER file
#
# Use: cat /etc/passwd | passwd2ganesha > /etc/ganesha.pwfile
#      ypcat passwd    | passwd2ganesha > /etc/ganesha.pwfile
#      getent passwd   | passwd2ganesha > /etc/ganesha.pwfile
#

grep -v '#' | egrep -v '^\s*$' | awk 'BEGIN { print "Hosts\n{" ;}{ print "\t"  $2 " = " $1 ";" ;}END{print"}";}' 

