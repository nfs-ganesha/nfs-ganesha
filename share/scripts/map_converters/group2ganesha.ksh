#!/bin/ksh 
#
# This scripts turns a 'group' map (like /etc/group for example) to a GANESHA UIDMAPPER file
#
# Use: cat /etc/group | group2ganesha > /etc/ganesha.pwfile
#      ypcat group    | group2ganesha > /etc/ganesha.pwfile
#      getent group   | group2ganesha > /etc/ganesha.pwfile
#

awk -F ':' 'BEGIN { print "Groups\n{" ;}{ print "\t"  $1 " = " $3 ";" ;}END{print"}";}'

