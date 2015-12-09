#!/bin/sh

#
# Extract configuration from /etc/sysconfig/ganesha and
# copy or generate new environment variables to
# /run/sysconfig/ganesha to be used by nfs-ganesha service
#

CONFIGFILE=/etc/sysconfig/ganesha
if test -r ${CONFIGFILE}; then
	. ${CONFIGFILE}
	[ -x ${EPOCH_EXEC} ] &&  EPOCHVALUE=`${EPOCH_EXEC}`

	mkdir -p /run/sysconfig
	{
		cat ${CONFIGFILE}
		[ -n "${EPOCHVALUE}" ] && echo EPOCH=\"-E $EPOCHVALUE\"
	} > /run/sysconfig/ganesha
fi
