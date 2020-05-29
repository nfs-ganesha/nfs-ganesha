===================================================================
ganesha-cache-config -- NFS Ganesha Cache Configuration File
===================================================================

.. program:: ganesha-cache-config


SYNOPSIS
==========================================================

| /etc/ganesha/ganesha.conf

DESCRIPTION
==========================================================

NFS-Ganesha reads the configuration data from:
| /etc/ganesha/ganesha.conf

This file lists NFS-Ganesha Cache config options.  These options used to be
configured in the CACHEINODE block.  They may still be used in that block, but
it is deprecated and will go away.  The MDCACHE block takes precidence over the
CACHEINODE block.

MDCACHE {}
--------------------------------------------------------------------------------

NParts (uint32, range 1 to 32633, default 7)
    Partitions in the Cache_Inode tree.

Cache_Size(uint32, range 1 to UINT32_MAX, default 32633)
    Per-partition hash table size.

Use_Getattr_Directory_Invalidation(bool, default false)
    Use getattr for directory invalidation.

Dir_Chunk(uint32, range 0 to UINT32_MAX, default 128)
    Size of per-directory dirent cache chunks, 0 means directory chunking is not
    enabled.

Detached_Mult(uint32, range 1 to UINT32_MAX, default 1)
    Max number of detached directory entries expressed as a multiple of the
    chunk size.

Entries_HWMark(uint32, range 1 to UINT32_MAX, default 100000)
    The point at which object cache entries will start being reused.

Entries_Release_Size(uint32, range 0 to UINT32_MAX, default 100)
    The number of entries attempted to release each time when the handle
    cache has exceeded the entries high water mark.

Chunks_HWMark(uint32, range 1 to UINT32_MAX, default 100000)
    The point at which dirent cache chunks will start being reused.

LRU_Run_Interval(uint32, range 1 to 24 * 3600, default 90)
    Base interval in seconds between runs of the LRU cleaner thread.

FD_Limit_Percent(uint32, range 0 to 100, default 99)
    The percentage of the system-imposed maximum of file descriptors at which
    Ganesha will deny requests.

FD_HWMark_Percent(uint32, range 0 to 100, default 90)
    The percentage of the system-imposed maximum of file descriptors above which
    Ganesha will make greater efforts at reaping.

FD_LWMark_Percent(uint32, range 0 to 100, default 50)
    The percentage of the system-imposed maximum of file descriptors below which
    Ganesha will not reap file descriptors.

Reaper_Work(uint32, range 1 to 2000, default 0)
    Roughly, the amount of work to do on each pass through the thread under
    normal conditions.  (Ideally, a multiple of the number of lanes.)  *This
    setting is deprecated.  Please use Reaper_Work_Per_Lane*

Reaper_Work_Per_Lane(uint32, range 1 to UINT32_MAX, default 50)
    This is the numer of handles per lane to scan when performing LRU
    maintenance.  This task is performed by the Reaper thread.

Biggest_Window(uint32, range 1 to 100, default 40)
    The largest window (as a percentage of the system-imposed limit on FDs) of
    work that we will do in extremis.

Required_Progress(uint32, range 1 to 50, default 5)
    Percentage of progress toward the high water mark required in in a pass
    through the thread when in extremis

Futility_Count(uint32, range 1 to 50, default 8)
    Number of failures to approach the high watermark before we disable caching,
    when in extremis.

Dirmap_HWMark(uint32, range 1 to UINT32_MAX, default 10000)
    The point at which dirmap entries are reused.  This puts a practical limit
    on the number of simultaneous readdirs that may be in progress on an export
    for a whence-is-name FSAL (currently only FSAL_RGW)

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
