===================================================================
ganesha-zfs-config -- NFS Ganesha ZFS Configuration File
===================================================================

.. program:: ganesha-zfs-config


SYNOPSIS
==========================================================

| /etc/ganesha/zfs.conf

DESCRIPTION
==========================================================

NFS-Ganesha installs the config example for ZFS FSAL:

| /etc/ganesha/zfs.conf

This file lists zfs specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "ZFS")
    Name of FSAL should always be ZFS.

**zpool(string, default "tank", must be supplied)**

ZFS {}
--------------------------------------------------------------------------------

**link_support(bool, default true)**

**symlink_support(bool, default true)**

**cansettime(bool, default true)**

**maxread(uint64, range 512 to 64*1024*1024, default 64*1024*1024)**

**maxwrite(uint64, range 512 to 64*1024*1024, default 64*1024*1024)**

**umask(mode, range 0 to 0777, default 0)**

**auth_xdev_export(bool, default false)**

**xattr_access_rights(mode, range 0 to 0777, default 0400)**

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
