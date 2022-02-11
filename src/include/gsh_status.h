/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright © 2014 CohortFS, LLC.
 * Author: William Allen Simpson <bill@CohortFS.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/**
 * @file   gsh_status.h
 * @author William Allen Simpson <bill@CohortFS.com>
 * @author Daniel Gryniewicz <dang@CohortFS.com>
 * @date   Mon Nov 17 14:11:17 2014
 *
 * @brief Ganesha Unified Status
 */

#ifndef GSH_STATUS_H
#define GSH_STATUS_H

/**
 * @brief Possible Errors from SAL Code
 *
 * @note A lot of these errors don't make sense in the context of the
 *       SAL and ought to be pruned.
 */
typedef enum state_status {
	STATE_SUCCESS,
	STATE_MALLOC_ERROR,
	STATE_POOL_MUTEX_INIT_ERROR,
	STATE_GET_NEW_LRU_ENTRY,
	STATE_INIT_ENTRY_FAILED,
	STATE_FSAL_ERROR,
	STATE_LRU_ERROR,
	STATE_HASH_SET_ERROR,
	STATE_NOT_A_DIRECTORY,
	STATE_INCONSISTENT_ENTRY,
	STATE_BAD_TYPE,
	STATE_ENTRY_EXISTS,
	STATE_DIR_NOT_EMPTY,
	STATE_NOT_FOUND,
	STATE_INVALID_ARGUMENT,
	STATE_INSERT_ERROR,
	STATE_HASH_TABLE_ERROR,
	STATE_FSAL_EACCESS,
	STATE_IS_A_DIRECTORY,
	STATE_FSAL_EPERM,
	STATE_NO_SPACE_LEFT,
	STATE_READ_ONLY_FS,
	STATE_IO_ERROR,
	STATE_ESTALE,
	STATE_FSAL_ERR_SEC,
	STATE_LOCKED,
	STATE_QUOTA_EXCEEDED,
	STATE_ASYNC_POST_ERROR,
	STATE_NOT_SUPPORTED,
	STATE_STATE_ERROR,
	STATE_FSAL_DELAY,
	STATE_NAME_TOO_LONG,
	STATE_LOCK_CONFLICT,
	STATE_LOCK_BLOCKED,
	STATE_LOCK_DEADLOCK,
	STATE_BAD_COOKIE,
	STATE_FILE_BIG,
	STATE_GRACE_PERIOD,
	STATE_CACHE_INODE_ERR,
	STATE_SIGNAL_ERROR,
	STATE_FILE_OPEN,
	STATE_MLINK,
	STATE_SERVERFAULT,
	STATE_TOOSMALL,
	STATE_XDEV,
	STATE_SHARE_DENIED,
	STATE_IN_GRACE,
	STATE_BADHANDLE,
	STATE_BAD_RANGE,
} state_status_t;

#define STATE_FSAL_ESTALE STATE_ESTALE

#endif				/* !GSH_STATUS_H */
