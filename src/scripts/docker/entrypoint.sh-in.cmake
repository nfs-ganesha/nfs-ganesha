#!/bin/bash
#
# Entrypoint for the Ganesha docker container.  If "shell" is given as the
# argument, then a shell is run; otherwise, Ganseha is started.

# These are the options for starting Ganesha.  Set them in the environment.
: ${GANESHA_LOGFILE:="@SYSSTATEDIR@/log/ganesha.log"}
: ${GANESHA_CONFFILE:="@SYSCONFDIR@/ganesha/ganesha.conf"}
: ${GANESHA_OPTIONS:="-N NIV_EVENT"}
: ${GANESHA_EPOCH:=""}
: ${GANESHA_LIBPATH:="@LIB_INSTALL_DIR@"}

function rpc_init {
	rpcbind
	rpc.statd -L
	rpc.idmapd
}

if [ "$1" == "shell" ]; then
	/bin/bash
else
	rpc_init
	LD_LIBRARY_PATH="${GANESHA_LIBPATH}" @CMAKE_INSTALL_PREFIX@/bin/ganesha.nfsd -F -L ${GANESHA_LOGFILE} -f ${GANESHA_CONFFILE} ${GANESHA_OPTIONS} ${GANESHA_EPOCH}
fi
