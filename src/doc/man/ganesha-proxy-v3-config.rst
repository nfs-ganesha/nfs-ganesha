===================================================================
ganesha-proxy-v3-config -- NFSv3 Ganesha Proxy Configuration File
===================================================================

.. program:: ganesha-proxy-v3-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha install the following config file for Proxy FSAL:
| /etc/ganesha/ganesha.conf

This file lists Proxy specific config options.

EXPORT { FSAL {} }
--------------------------------------------------------------------------------

Name(string, "proxy_v3")
    Name of FSAL should always be proxy_v3.

**Srv_Addr(ipv4_addr default "127.0.0.1")**

**FSAL_MAXIOSIZE(default 64 MB)**

PROXY_V3 {}
--------------------------------------------------------------------------------

**FSAL_MAXIOSIZE(default 64 MB)**

**maxread(uint64, default 1 MB)**
    range 1024 to FSAL_MAXIOSIZE

    Note that this value will get clamped to the backend's FSINFO response.

**maxwrite(uint64, default 1 MB)**
    range 1024 to FSAL_MAXIOSIZE

    Note that this value will get clamped to the backend's FSINFO response.

See also
==============================
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
