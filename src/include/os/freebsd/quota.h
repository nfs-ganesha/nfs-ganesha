/*
 *   Copyright (C) Panasas, Inc. 2011
 *   Author(s): Sachin Bhamare <sbhamare@panasas.com>
 *
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later
 *   version.
 *
 *   This library can be distributed with a BSD license as well, just
 *   ask.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free
 *   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *   02111-1307 USA
 */

#ifndef QUOTA_FREEBSD_H
#define QUOTA_FREEBSD_H

#include <ufs/ufs/quota.h>

#define QUOTACTL(cmd, path, id, addr) \
	quotactl(path, (cmd), id, (void *)addr);

/*
 * kludge to account for differently named member variable
 * (dqb_curspace Vs dqb_curblocks) in struct dqblk on Linux and
 * FreeBSD platforms
 */
struct dqblk_os {
	u_int64_t dqb_bhardlimit;	/* absolute limit on disk blks alloc */
	u_int64_t dqb_bsoftlimit;	/* preferred limit on disk blks */
	u_int64_t dqb_curspace;	/* current block count */
	u_int64_t dqb_ihardlimit;	/* maximum # allocated inodes + 1 */
	u_int64_t dqb_isoftlimit;	/* preferred inode limit */
	u_int64_t dqb_curinodes;	/* current # allocated inodes */
	int64_t dqb_btime;	/* time limit for excessive disk use */
	int64_t dqb_itime;	/* time limit for excessive files */
};

#if __FreeBSD_cc_version >= 800001
#undef dqblk
#endif

#define dqblk dqblk_os

#endif /* QUOTA_FREEBSD_H */
