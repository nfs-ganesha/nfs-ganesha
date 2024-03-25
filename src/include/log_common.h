/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * definition des codes d'error
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 *
 *
 */

#ifndef _LOG_COMMON_H
#define _LOG_COMMON_H

/*
 * Log message severity constants
 */
typedef enum log_levels {
	NIV_NULL,
	NIV_FATAL,
	NIV_MAJ,
	NIV_CRIT,
	NIV_WARN,
	NIV_EVENT,
	NIV_INFO,
	NIV_DEBUG,
	NIV_MID_DEBUG,
	NIV_FULL_DEBUG,
	NB_LOG_LEVEL
} log_levels_t;

/*
 * Log components used throughout the code.
 */
typedef enum log_components {
	COMPONENT_ALL = 0, /* Used for changing logging for all
				 * components */
	COMPONENT_LOG, /* Keep this first, some code depends on it
				 * being the first component */
	COMPONENT_MEM_ALLOC,
	COMPONENT_MEMLEAKS,
	COMPONENT_FSAL,
	COMPONENT_NFSPROTO,
	COMPONENT_NFS_V4,
	COMPONENT_EXPORT,
	COMPONENT_FILEHANDLE,
	COMPONENT_DISPATCH,
	COMPONENT_MDCACHE,
	COMPONENT_MDCACHE_LRU,
	COMPONENT_HASHTABLE,
	COMPONENT_HASHTABLE_CACHE,
	COMPONENT_DUPREQ,
	COMPONENT_INIT,
	COMPONENT_MAIN,
	COMPONENT_IDMAPPER,
	COMPONENT_NFS_READDIR,
	COMPONENT_NFS_V4_LOCK,
	COMPONENT_CONFIG,
	COMPONENT_CLIENTID,
	COMPONENT_SESSIONS,
	COMPONENT_PNFS,
	COMPONENT_RW_LOCK,
	COMPONENT_NLM,
	COMPONENT_TIRPC,
	COMPONENT_NFS_CB,
	COMPONENT_THREAD,
	COMPONENT_NFS_V4_ACL,
	COMPONENT_STATE,
	COMPONENT_9P,
	COMPONENT_9P_DISPATCH,
	COMPONENT_FSAL_UP,
	COMPONENT_DBUS,
	COMPONENT_NFS_MSK,
	COMPONENT_XPRT,
	COMPONENT_COUNT
} log_components_t;

#endif
