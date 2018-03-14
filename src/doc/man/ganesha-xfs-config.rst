===================================================================
ganesha-xfs-config -- NFS Ganesha XFS Configuration File
===================================================================

.. program:: ganesha-xfs-config


SYNOPSIS
==========================================================

    /etc/ganesha/xfs.conf

DESCRIPTION
==========================================================

NFS-Ganesha installs the config example for XFS FSAL:

    /etc/ganesha/xfs.conf

This file lists xfs specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "XFS")
    Name of FSAL should always be XFS.

XFS {}
--------------------------------------------------------------------------------
**link_support(bool, default true)**

**symlink_support(bool, default true)**

**cansettime(bool, default true)**

**maxread(uint64, range 512 to 64*1024*1024, default 64*1024*1024)**

**maxwrite(uint64, range 512 to 64*1024*1024, default 64*1024*1024)**

**umask(mode, range 0 to 0777, default 0)**

**auth_xdev_export(bool, default false)**

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
