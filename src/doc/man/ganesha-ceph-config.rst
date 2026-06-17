.. SPDX-License-Identifier: LGPL-3.0-or-later

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

sec_label_xattr(char, default "security.selinux xattr of the file")
    Enable NFSv4.2 security label attribute. Ganesha supports
    "Limited Server Mode" as detailed in RFC 7204. Note that
    not all FSALs support security labels.

cmount_path(string, no default)
    If specified, the path within the ceph filesystem to mount this
    export on. It is allowed to be any complete path hierarchy between `/` and
    the EXPORT {path}. (i.e. if EXPORT { Path } parameter is `/foo/bar` then
    cmount_path could be `/`, `/foo` or `/foo/bar`).

    If this and the other EXPORT { FSAL {} } options are the same
    between multiple exports, those exports will share a single
    cephfs client. With the default, this effectively defaults to
    the same path as EXPORT { Path }.

CEPH {}
--------------------------------------------------------------------------------

Ceph_Conf(path, default "")
    Path to the ceph config file to inherit ceph configuration from.

umask(mode, range 0 to 0777, default 0)

client_oc(bool, default false)
    Enable or disable client_oc (object cache). This defaults to false because
    Ganesha runs better with it disabled.

async(bool, default false)
    Enable ceph async operations (read and write).

zerocopy(bool, default false)
    Enable ceph zero copy I/O. Zero copy and client_oc are incompatible.

use_old_uuid(bool, default false)
    Use old uuid logic for ceph client. Useful when upgrading from pre 7.0
    version to latest. If this flag is set, then continue using it. As per old
    logic the uuid is formed as "ganesha-<nodeid>-<export-id>". As per new logic
    the uuid is formed as "ganesha-<64-bytes-hash>", and the hash is formed
    using nodeid, userid, fs_name and mount path. For fresh deployments of 7.0
    and later versions do not set this parameter.

register_service(bool, default false)
    Enable registration of the NFS service with the Ceph cluster.
    This allows Ceph to track the NFS service for health monitoring
    and service discovery.

nodeid(string, default "")
    Identifier used when registering this NFS instance with Ceph.
    This value must be unique per NFS node within the same Ceph cluster.
    Required only when register_service is set to true.

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
:doc:`ganesha-fscrypt-config <ganesha-fscrypt-config>`\(8)

