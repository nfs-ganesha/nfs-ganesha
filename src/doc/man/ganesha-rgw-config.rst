===================================================================
ganesha-rgw-config -- NFS Ganesha RGW Configuration File
===================================================================

.. program:: ganesha-rgw-config


SYNOPSIS
==========================================================

| /etc/ganesha/rgw.conf

| /etc/ganesha/rgw_bucket.conf

DESCRIPTION
==========================================================

NFS-Ganesha install two config examples for RGW FSAL:

| /etc/ganesha/rgw.conf

| /etc/ganesha/rgw_bucket.conf

This file lists RGW specific config options.

EXPORT { }
--------------------------------------------------------------------------------
RGW supports exporting both the buckets and filesystem.

.. Explain in detail about exporting bucket and filesystem

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "RGW")
    Name of FSAL should always be RGW.

**User_Id(string, no default)**

**Access_Key(string, no default)**

**Secret_Access_Key(string, no default)**

RGW {}
--------------------------------------------------------------------------------
The following configuration variables customize the startup of the FSAL's
radosgw instance.

ceph_conf
    optional full-path to the Ceph configuration file (equivalent to passing
    "-c /path/to/ceph.conf" to any Ceph binary

name
    optional instance name (equivalent to passing "--name client.rgw.foohost" to
    the radosgw binary);  the value provided here should be the same as the
    section name (sans brackets) of the radosgw facility in the Ceph
    configuration file (which must exist)

cluster
    optional cluster name (equivalent to passing "--cluster foo" to any Ceph
    binary);  use of a non-default value for cluster name is uncommon, but can
    be verified by examining the startup options of Ceph binaries

init_args
    additional argument strings which will be passed verbatim to the radosgw
    instance startup process as if they had been given on the radosgw command
    line provided for customization in uncommon setups

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
