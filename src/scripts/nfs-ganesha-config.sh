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

		# Set NUMA options if numactl is present
		NUMACTL=$(command -v numactl 2>/dev/null)
		if [ -n "${NUMACTL}" ]; then
			echo NUMACTL=$NUMACTL
			echo NUMAOPTS=--interleave=all
		fi
	} > /run/sysconfig/ganesha
fi
