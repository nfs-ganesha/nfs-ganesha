===================================================================
ganesha-export-config -- NFS Ganesha Export Configuration File
===================================================================

.. program:: ganesha-export-config


SYNOPSIS
==========================================================

    /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha obtains configuration data from the configuration file:

    /etc/ganesha/ganesha.conf

This file lists NFS-Ganesha Export block config options.

EXPORT_DEFAULTS {}
--------------------------------------------------------------------------------
These options are all "export permissions" options, and will be
repeated in the EXPORT {} and EXPORT { CLIENT {} } blocks.

These options will all be dynamically updateable.

Access_Type(enum, default None)
    Possible values:
        None, RW, RO, MDONLY, MDONLY_RO

Protocols(enum list, default [3,4])
    Possible values:
        3, 4, NFS3, NFS4, V3, V4, NFSv3, NFSv4, 9P
        Transports(enum list, values [UDP, TCP, RDMA], default [UDP, TCP])
        Anonymous_uid(anonid, range INT32MIN to UINT32MAX, default -2)
        Anonymous_gid(anonid, range INT32MIN to UINT32MAX, default -2)

SecType(enum list, default [none, sys])
    Possible values:
        none, sys, krb5, krb5i, krb5p
        PrivilegedPort(bool, default false)
        Manage_Gids(bool, default false)

Squash(enum, default root_sqaush)
    Possible values:
        root, root_squash, rootsquash,
        rootid, root_id_squash, rootidsquash,
        all, all_squash, allsquash,
        all_anomnymous, allanonymous,
        no_root_squash, none, noidsquash

    Each line of defaults above are synonyms

**NFS_Commit(bool, default false)**

Delegations(enum, default None)
    Possible values:
        None, read, write, readwrite, r, w, rw

**Attr_Expiration_Time(int32, range -1 to INT32_MAX, default 60)**

EXPORT {}
--------------------------------------------------------------------------------
Export_id (required):
    An identifier for the export, must be unique and betweem 0 and 65535.
    If Export_Id 0 is specified, Pseudo must be the root path (/).

Path (required)
    The directory in the exported file system this export is rooted on
    (may be ignored for some FSALs). It need not be unique if Pseudo and/or Tag are specified.

Pseudo (required v4)
    This option specifies the position in the Pseudo FS this export occupies if
    this is an NFS v4 export. It must be unique. By using different Pseudo options,
    the same Path may be exported multiple times.

Tag (no default)
    This option allows an alternative access for NFS v3
    mounts. The option MUST not have a leading /. Clients
    may not mount subdirectories (i.e. if Tag = foo, the
    client may not mount foo/baz). By using different
    Tag options, the same Path may be exported multiple
    times.

MaxRead (4194304)
    The maximum read size on this export
MaxWrite (4194304)
    The maximum write size on this export
PrefRead (4194304)
    The preferred read size on this export
PrefWrite (4194304)
   The preferred write size on this export
PrefReaddir (16384)
   The preferred readdir size on this export

MaxOffsetWrite (18446744073709551615)
    Maximum file offset that may be written

MaxOffsetRead (18446744073709551615)
    Maximum file offset that may be read

CLIENT (optional)
    See the ``EXPORT { CLIENT  {} }`` block.

FSAL (required)
    See the ``EXPORT { FSAL  {} }`` block.

EXPORT { CLIENT  {} }
--------------------------------------------------------------------------------
Take all the "export permissions" options from EXPORT_DEFAULTS.
The client lists are dynamically updateable.


Clients(client list, empty)
    Client list entries can take on one of the following forms:
    Match any client::

        @name       Netgroup name
        x.x.x.x/y   IPv4 network address
        wildcarded  If the string contains at least one ? or *
                    character (and is not simply "*"), the string is
                    used to pattern match host names. Note that [] may
                    also be used, but the pattern MUST have at least one
                    ? or *
        hostname    Match a single client (match is by IP address, all
                    addresses returned by getaddrinfo will match, the
                    getaddrinfo call is made at config parsing time)
        IP address  Match a single client


EXPORT { FSAL {} }
--------------------------------------------------------------------------------

NFS-Ganesha supports the following FSALs:
**Ceph**
**Gluster**
**GPFS**
**Proxy**
**RGW**
**VFS**
**ZFS**

Refer to individual FSAL config file for list of config options.


.. FSAL PNFS

    Stripe_Unit(uint32, range 1024 to 1024*1024, default 8192)

    pnfs_enabled(bool, default false)

    FSAL_NULL:

    EXPORT { FSAL { FSAL {} } }
    describes the stacked FSAL's parameters

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
:doc:`ganesha-rgw-config <ganesha-rgw-config>`\(8)
:doc:`ganesha-vfs-config <ganesha-vfs-config>`\(8)
:doc:`ganesha-xfs-config <ganesha-xfs-config>`\(8)
:doc:`ganesha-zfs-config <ganesha-zfs-config>`\(8)
:doc:`ganesha-gpfs-config <ganesha-gpfs-config>`\(8)
:doc:`ganesha-9p-config <ganesha-9p-config>`\(8)
:doc:`ganesha-proxy-config <ganesha-proxy-config>`\(8)
:doc:`ganesha-ceph-config <ganesha-ceph-config>`\(8)
