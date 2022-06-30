======================================================================
ganesha-top -- A tool for the information monitoring like top
======================================================================

SYNOPSIS
===================================================================

| ganesha-top [--help] [--interval INTERVAL]

DESCRIPTION
===================================================================

This tool allows the administrator to monitor the system and NFS-Ganesha status.
In the header block, there are pieces of information like memory usage (including
RSIZE/VSIZE/SWAP), CPU usage, number of Exports, Clients, and operations of
NFS-Ganesha. Also included is the cache (MDCache) information.

This tool can also display the detail about Exports, Clients, and all NFSv4 OPs.
For the administrator, that can help analyze the system status by OPs, latency,
and other information.

OPTIONS
===================================================================
**--interval**

Specify the update interval, the default value is 5 seconds.
