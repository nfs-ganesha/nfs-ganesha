===================================================================
ganesha-gpfs-config -- NFS Ganesha GPFS Configuration File
===================================================================

.. program:: ganesha-gpfs-config


SYNOPSIS
==========================================================

| /etc/ganesha/gpfs.conf

DESCRIPTION
==========================================================

NFS-Ganesha install the following config file for GPFS FSAL:
| /etc/ganesha/gpfs.conf

This file lists GPFS specific config options.

GPFS {}
--------------------------------------------------------------------------------

**link_support(bool, default true)**

**symlink_support(bool, default true)**

**cansettime(bool, default true)**

**umask(mode, range 0 to 0777, default 0)**

**auth_xdev_export(bool, default false)**

**Delegations(enum, default read)**

  Possible values:
	None, read, write, readwrite, r, w, rw

**pnfs_file(bool, default false)**

**fsal_trace(bool, default true)**

**fsal_grace(bool, default false)**

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
