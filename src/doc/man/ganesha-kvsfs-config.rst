===================================================================
ganesha-kvsfs-config -- NFS Ganesha KVSFS Configuration File
===================================================================

.. program:: ganesha-kvsfs-config


SYNOPSIS
==========================================================

| /etc/ganesha/kvsfs.conf

DESCRIPTION
==========================================================

NFS-Ganesha install the following config file for KVSFS FSAL:
| /etc/ganesha/kvsfs.conf

This file lists KVSFS specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "KVSFS")
    Name of FSAL should always be KVSFS.

**kvsns_config(string default "/etc/kvsns/d/kvsns.ini")**
        the path to the kvsns.ini file. If not specified, default value is used

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
