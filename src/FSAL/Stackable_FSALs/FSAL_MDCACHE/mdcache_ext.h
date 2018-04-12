/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2015-2017 Red Hat, Inc. and/or its affiliates.
 * Author: Daniel Gryniewicz <dang@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

/**
 * @file mdcache_ext.h
 * @brief MDCache external interface
 *
 * Stuff that can be accessed outside MDCACHE.  Things in here are generally
 * hacks that should be removed.
 */

#ifndef MDCACHE_EXT_H
#define MDCACHE_EXT_H

/**
 * @defgroup config_mdcache Structure and defaults for MDCACHE
 *
 * @{
 */

/**
 * @brief Structure to hold MDCACHE paramaters
 */

struct mdcache_parameter {
	/** Partitions in the Cache_Inode tree.  Defaults to 7,
	 * settable with NParts. */
	uint32_t nparts;
	/** Per-partition hash table size.  Defaults to 32633,
	 * settable with Cache_Size. */
	uint32_t cache_size;
	/** Use getattr for directory invalidation.  Defaults to
	    false.  Settable with Use_Getattr_Directory_Invalidation. */
	bool getattr_dir_invalidation;
	struct {
		/** Size of per-directory dirent cache chunks, 0 means
		 *  directory chunking is not enabled.
		 */
		uint32_t avl_chunk;
		/** Size of a dirent chunk at which point the chunk should
		 *  be split. Pre-computed for simplicity.
		 */
		uint32_t avl_chunk_split;
		/** Detached dirent multiplier (of avl_chunk) */
		uint32_t avl_detached_mult;
		/** Computed max detached dirents */
		uint32_t avl_detached_max;
	} dir;
	/** High water mark for cache entries.  Defaults to 100000,
	    settable by Entries_HWMark. */
	uint32_t entries_hwmark;
	/** High water mark for chunks.  Defaults to 100000,
	    settable by Chunks_HWMark. */
	uint32_t chunks_hwmark;
	/** Base interval in seconds between runs of the LRU cleaner
	    thread. Defaults to 60, settable with LRU_Run_Interval. */
	uint32_t lru_run_interval;
	/** The percentage of the system-imposed maximum of file
	    descriptors at which Ganesha will deny requests.
	    Defaults to 99, settable with FD_Limit_Percent. */
	uint32_t fd_limit_percent;
	/** The percentage of the system-imposed maximum of file
	    descriptors above which Ganesha will make greater efforts
	    at reaping. Defaults to 90, settable with
	    FD_HWMark_Percent. */
	uint32_t fd_hwmark_percent;
	/** The percentage of the system-imposed maximum of file
	    descriptors below which Ganesha will not reap file
	    descriptonot reap file descriptors.  Defaults to 50,
	    settable with FD_LWMark_Percent. */
	uint32_t fd_lwmark_percent;
	/** Roughly, the amount of work to do on each pass through the
	    thread under normal conditions.  (Ideally, a multiple of
	    the number of lanes.)  Defaults to 1000, settable with
	    Reaper_Work. */
	uint32_t reaper_work;
	/** The amount of work for the reaper thread to do per-lane
	    under normal conditions. Settable with Repaper_Work_Per_Thread */
	uint32_t reaper_work_per_lane;
	/** The largest window (as a percentage of the system-imposed
	    limit on FDs) of work that we will do in extremis.
	    Defaults to 40, settable with Biggest_Window */
	uint32_t biggest_window;
	/** Percentage of progress toward the high water mark required
	    in in a pass through the thread when in extremis.
	    Defaults to 5, settable with Required_Progress. */
	uint32_t required_progress;
	/** Number of failures to approach the high watermark before
	    we disable caching, when in extremis.  Defaults to 8,
	    settable with Futility_Count */
	uint32_t futility_count;
};

extern struct mdcache_parameter mdcache_param;
/** @} */

#endif /* MDCACHE_EXT_H */
