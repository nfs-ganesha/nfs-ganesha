===================================================================
ganesha-ceph-config -- NFS Ganesha CEPH Configuration File
===================================================================

.. program:: ganesha-ceph-config


SYNOPSIS
==========================================================

| /etc/ganesha/ceph.conf

DESCRIPTION
==========================================================

NFS-Ganesha install config example for CEPH FSAL:
| /etc/ganesha/ceph.conf

This file lists CEPH specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------
Name(string, "Ceph")
    Name of FSAL should always be Ceph.

Filesystem(string, no default)
    Ceph filesystem name string, for mounting an alternate filesystem within
    the cluster. The default is to mount the default filesystem in the cluster
    (usually, the first one created).

User_Id(string, no default)
    cephx userid used to open the MDS session. This string is what gets appended
    to "client.". If not set, the ceph client libs will sort this out based on
    ceph configuration.

Secret_Access_Key(string, no default)
    Key to use for the session (if any). If not set, then it uses the normal
    search path for cephx keyring files to find a key.

CEPH {}
--------------------------------------------------------------------------------

**Ceph_Conf(path, default "")**

**umask(mode, range 0 to 0777, default 0)**

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)

