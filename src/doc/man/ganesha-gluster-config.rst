===================================================================
ganesha-gluster-config -- NFS Ganesha Gluster Configuration File
===================================================================

.. program:: ganesha-gluster-config


SYNOPSIS
==========================================================

| /etc/ganesha/gluster.conf

DESCRIPTION
==========================================================

NFS-Ganesha install the following config file for Gluster FSAL:
| /etc/ganesha/gluster.conf

This file lists Gluster specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------
Name(string, "Gluster")
    Name of FSAL should always be RGW.

**volume(string, no default, required)**

**hostname(string, no default, required)**

**volpath(path, default "/")**

**glfs_log(path, default "/tmp/gfapi.log")**


See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
