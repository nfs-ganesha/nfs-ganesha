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

This file lists NFS-Ganesha Cache config options.

MDCACHE {}
--------------------------------------------------------------------------------

NParts (uint32, range 1 to 32633, default 7)
    Partitions in the MDCACHE tree.

Cache_Size(uint32, range 1 to UINT32_MAX, default 32633)
    Per-partition hash table size.

Use_Getattr_Directory_Invalidation(bool, default false)
    Use getattr for directory invalidation.

Dir_Chunk(uint32, range 0 to UINT32_MAX, default 128)
    Size of per-directory dirent cache chunks, 0 means directory chunking is not
    enabled. Dir_Chunk should always be enabled. Most FSAL modules especially
    FSAL_RGW/FSAL_GLUSTER need it to make readdir work well.

Detached_Mult(uint32, range 1 to UINT32_MAX, default 1)
    Max number of detached directory entries expressed as a multiple of the
    chunk size.

Entries_HWMark(uint32, range 1 to UINT32_MAX, default 100000)
    The point at which object cache entries will start being reused.

Entries_Release_Size(uint32, range 0 to UINT32_MAX, default 100)
    The number of entries attempted to release each time when the handle
    cache has exceeded the entries high water mark.

Chunks_HWMark(uint32, range 1 to UINT32_MAX, default 1000)
    The point at which dirent cache chunks will start being reused.

Chunks_LWMark(uint32, range 1 to UINT32_MAX, default 1000)
    The target for reaping dirent cache chunks to drain the cache.

    Entries_HWMark, Dir_Chunk, Chunks_HWMark, Chunks_LWMark all play together.
    While a dirent chunk is cached, the object cache entries that are part of
    that dirent chunk will be retained in the cache indefinitely. Note that
    this means that possibly Chunks_LWMark * Dir_Chunk entries will be retained.
    If this is larger than Entries_HWMark, then the object cache may end up
    remaining above Entries_HWMark for a significant time. Consider the average
    size of the directories and the number of directories desired to be
    maintained in the cache and set Chunks_LWMark appropriately.

    Note that the Chunks_LWMark defaults to the same value as Chunks_HWMark
    and these are set to 1/100 of Entries_HWMark suggesting an average directory
    size of 100. It may be desirable to set Chunks_LWMark less than Chunks_HWMark
    if it is desirable to allow large directory chunks to not immediately be
    re-used, but it is desirable to in the short term drain the dirent cache
    down to a smaller number.

LRU_Run_Interval(uint32, range 1 to 24 * 3600, default 90)
    Base interval in seconds between runs of the LRU cleaner thread.

Cache_FDs(bool, true)
    If "Cache_FDs" is set to false, the reaper thread aggressively
    closes FDs , significantly reducing the number of open FDs.
    This will help to maintain a minimal number of open FDs.

    If "Cache_FDs" is set to true (default), FDs are cached, and the
    LRU reaper thread closes FDs only when the current open FD count
    reaches or exceeds the "fds_lowat" threshold.

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
    This is the number of handles per lane to scan when performing LRU
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

Files_Delegatable_Percent(int32, range 10 to 90, default 90)
    Total number of files ganesha can delegate to clients as a percent of
    Entries_HWMark

See also
==============================
:doc:`ganesha-config <ganesha-config>`\(8)
