===================================================================
ganesha-9p-config -- NFS Ganesha 9p Configuration File
===================================================================

.. program:: ganesha-9p-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha obtains configuration data from the configuration file:

	/etc/ganesha/ganesha.conf

This file lists 9p specific config options.

_9P {}
--------------------------------------------------------------------------------

**Nb_Worker(uint32, range 1 to 1024*128, default 256)**
    Number of worker threads.

**_9P_TCP_Port(uint16, range 1 to UINT16_MAX, default 564)**

**_9P_RDMA_Port(uint16, range 1 to UINT16_MAX, default 5640)**

**_9P_TCP_Msize(uint32, range 1024 to UINT32_MAX, default 65536)**

**_9P_RDMA_Msize(uint32, range 1024 to UINT32_MAX, default 1048576)**

**_9P_RDMA_Backlog(uint16, range 1 to UINT16_MAX, default 10)**

**_9P_RDMA_Inpool_size(uint16, range 1 to UINT16_MAX, default 64)**

**_9P_RDMA_Outpool_Size(uint16, range 1 to UINT16_MAX, default 32)**

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
:doc:`ganesha-log-config <ganesha-log-config>`\(8)
:doc:`ganesha-core-config <ganesha-core-config>`\(8)
:doc:`ganesha-export-config <ganesha-export-config>`\(8)
