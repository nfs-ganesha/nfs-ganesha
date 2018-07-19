===================================================================
ganesha-gluster-config -- NFS Ganesha Gluster Configuration File
===================================================================

.. program:: ganesha-gluster-config


SYNOPSIS
==========================================================

| /etc/ganesha/gluster.conf

DESCRIPTION
==========================================================

NFS-Ganesha installs the following sample config file for Gluster FSAL:
| /etc/ganesha/gluster.conf

This file lists Gluster specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------
Name(string, "GLUSTER")
    Name of FSAL should always be GLUSTER.

**volume(string, no default, required)**

**hostname(string, no default, required)**

**volpath(path, default "/")**

**glfs_log(path, default "/var/log/ganesha/ganesha-gfapi.log")**

**up_poll_usec(uint64, range 1 to 60*1000*1000, default 10)**

**enable_upcall(bool, default true)**

**transport(enum, values [tcp, rdma], default tcp)**

GLUSTER {}
--------------------------------------------------------------------------------

**PNFS_MDS(bool, default FALSE)**
  Set this parameter to true to select this node as MDS

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
