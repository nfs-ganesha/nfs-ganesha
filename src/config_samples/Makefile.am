EXTRA_DIST = hosts.ganesha uidgid_mapfile.ganesha snmp.conf \
	     hpss.ganesha.nfsd.conf \
	     posix.ganesha.nfsd.conf \
	     proxy.ganesha.nfsd.conf \
	     snmp.ganesha.nfsd.conf \
	     lustre.ganesha.nfsd.conf \
	     shook.ganesha.nfsd.conf \
	     xfs.ganesha.nfsd.conf \
	     gpfs.ganesha.nfsd.conf \
	     gpfs.ganesha.main.conf \
	     gpfs.ganesha.exports.conf \
	     vfs.ganesha.nfsd.conf \
	     vfs.ganesha.main.conf \
	     vfs.ganesha.exports.conf \
	     dynfsal.ganesha.nfsd.conf \
	     zfs.ganesha.nfsd.conf \
	logrotate_ganesha

if USE_FSAL_FUSE
sysconf_DATA = hosts.ganesha snmp.conf uidgid_mapfile.ganesha
else
sysconf_DATA = hosts.ganesha snmp.conf uidgid_mapfile.ganesha \
	     @FS_NAME@.ganesha.nfsd.conf
endif

if USE_FSAL_GPFS
sysconf_DATA += gpfs.ganesha.main.conf \
	     gpfs.ganesha.exports.conf
endif

if USE_FSAL_VFS
sysconf_DATA += vfs.ganesha.main.conf \
	     vfs.ganesha.exports.conf
endif
