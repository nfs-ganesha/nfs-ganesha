/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Sreedhar Agraharam <Sreedhar.Agraharam@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

#ifndef EXPORT_METRICS_TYPES_H
#define EXPORT_METRICS_TYPES_H

#include <stdint.h>
#include "nfsv41.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FOREACH_NFS_STAT4(X)                 \
	X(NFS4_OK)                           \
	X(NFS4ERR_PERM)                      \
	X(NFS4ERR_NOENT)                     \
	X(NFS4ERR_IO)                        \
	X(NFS4ERR_NXIO)                      \
	X(NFS4ERR_ACCESS)                    \
	X(NFS4ERR_EXIST)                     \
	X(NFS4ERR_XDEV)                      \
	X(NFS4ERR_NOTDIR)                    \
	X(NFS4ERR_ISDIR)                     \
	X(NFS4ERR_INVAL)                     \
	X(NFS4ERR_FBIG)                      \
	X(NFS4ERR_NOSPC)                     \
	X(NFS4ERR_ROFS)                      \
	X(NFS4ERR_MLINK)                     \
	X(NFS4ERR_NAMETOOLONG)               \
	X(NFS4ERR_NOTEMPTY)                  \
	X(NFS4ERR_DQUOT)                     \
	X(NFS4ERR_STALE)                     \
	X(NFS4ERR_BADHANDLE)                 \
	X(NFS4ERR_BAD_COOKIE)                \
	X(NFS4ERR_NOTSUPP)                   \
	X(NFS4ERR_TOOSMALL)                  \
	X(NFS4ERR_SERVERFAULT)               \
	X(NFS4ERR_BADTYPE)                   \
	X(NFS4ERR_DELAY)                     \
	X(NFS4ERR_SAME)                      \
	X(NFS4ERR_DENIED)                    \
	X(NFS4ERR_EXPIRED)                   \
	X(NFS4ERR_LOCKED)                    \
	X(NFS4ERR_GRACE)                     \
	X(NFS4ERR_FHEXPIRED)                 \
	X(NFS4ERR_SHARE_DENIED)              \
	X(NFS4ERR_WRONGSEC)                  \
	X(NFS4ERR_CLID_INUSE)                \
	X(NFS4ERR_RESOURCE)                  \
	X(NFS4ERR_MOVED)                     \
	X(NFS4ERR_NOFILEHANDLE)              \
	X(NFS4ERR_MINOR_VERS_MISMATCH)       \
	X(NFS4ERR_STALE_CLIENTID)            \
	X(NFS4ERR_STALE_STATEID)             \
	X(NFS4ERR_OLD_STATEID)               \
	X(NFS4ERR_BAD_STATEID)               \
	X(NFS4ERR_BAD_SEQID)                 \
	X(NFS4ERR_NOT_SAME)                  \
	X(NFS4ERR_LOCK_RANGE)                \
	X(NFS4ERR_SYMLINK)                   \
	X(NFS4ERR_RESTOREFH)                 \
	X(NFS4ERR_LEASE_MOVED)               \
	X(NFS4ERR_ATTRNOTSUPP)               \
	X(NFS4ERR_NO_GRACE)                  \
	X(NFS4ERR_RECLAIM_BAD)               \
	X(NFS4ERR_RECLAIM_CONFLICT)          \
	X(NFS4ERR_BADXDR)                    \
	X(NFS4ERR_LOCKS_HELD)                \
	X(NFS4ERR_OPENMODE)                  \
	X(NFS4ERR_BADOWNER)                  \
	X(NFS4ERR_BADCHAR)                   \
	X(NFS4ERR_BADNAME)                   \
	X(NFS4ERR_BAD_RANGE)                 \
	X(NFS4ERR_LOCK_NOTSUPP)              \
	X(NFS4ERR_OP_ILLEGAL)                \
	X(NFS4ERR_DEADLOCK)                  \
	X(NFS4ERR_FILE_OPEN)                 \
	X(NFS4ERR_ADMIN_REVOKED)             \
	X(NFS4ERR_CB_PATH_DOWN)              \
	X(NFS4ERR_BADIOMODE)                 \
	X(NFS4ERR_BADLAYOUT)                 \
	X(NFS4ERR_BAD_SESSION_DIGEST)        \
	X(NFS4ERR_BADSESSION)                \
	X(NFS4ERR_BADSLOT)                   \
	X(NFS4ERR_COMPLETE_ALREADY)          \
	X(NFS4ERR_CONN_NOT_BOUND_TO_SESSION) \
	X(NFS4ERR_DELEG_ALREADY_WANTED)      \
	X(NFS4ERR_BACK_CHAN_BUSY)            \
	X(NFS4ERR_LAYOUTTRYLATER)            \
	X(NFS4ERR_LAYOUTUNAVAILABLE)         \
	X(NFS4ERR_NOMATCHING_LAYOUT)         \
	X(NFS4ERR_RECALLCONFLICT)            \
	X(NFS4ERR_UNKNOWN_LAYOUTTYPE)        \
	X(NFS4ERR_SEQ_MISORDERED)            \
	X(NFS4ERR_SEQUENCE_POS)              \
	X(NFS4ERR_REQ_TOO_BIG)               \
	X(NFS4ERR_REP_TOO_BIG)               \
	X(NFS4ERR_REP_TOO_BIG_TO_CACHE)      \
	X(NFS4ERR_RETRY_UNCACHED_REP)        \
	X(NFS4ERR_UNSAFE_COMPOUND)           \
	X(NFS4ERR_TOO_MANY_OPS)              \
	X(NFS4ERR_OP_NOT_IN_SESSION)         \
	X(NFS4ERR_HASH_ALG_UNSUPP)           \
	X(NFS4ERR_CLIENTID_BUSY)             \
	X(NFS4ERR_PNFS_IO_HOLE)              \
	X(NFS4ERR_SEQ_FALSE_RETRY)           \
	X(NFS4ERR_BAD_HIGH_SLOT)             \
	X(NFS4ERR_DEADSESSION)               \
	X(NFS4ERR_ENCR_ALG_UNSUPP)           \
	X(NFS4ERR_PNFS_NO_LAYOUT)            \
	X(NFS4ERR_NOT_ONLY_OP)               \
	X(NFS4ERR_WRONG_CRED)                \
	X(NFS4ERR_WRONG_TYPE)                \
	X(NFS4ERR_DIRDELEG_UNAVAIL)          \
	X(NFS4ERR_REJECT_DELEG)              \
	X(NFS4ERR_RETURNCONFLICT)            \
	X(NFS4ERR_DELEG_REVOKED)             \
	X(NFS4ERR_PARTNER_NOTSUPP)           \
	X(NFS4ERR_PARTNER_NO_AUTH)           \
	X(NFS4ERR_UNION_NOTSUPP)             \
	X(NFS4ERR_OFFLOAD_DENIED)            \
	X(NFS4ERR_WRONG_LFS)                 \
	X(NFS4ERR_BADLABEL)                  \
	X(NFS4ERR_OFFLOAD_NO_REQS)           \
	X(NFS4ERR_NOXATTR)                   \
	X(NFS4ERR_XATTR2BIG)                 \
	X(NFS4ERR_REPLAY)

enum nfsstat4_index {
	NFSSTAT4_INDEX_UNKNOWN_STATUS = 0,
#define DEFINE_INDEX(name) CONCAT(name, __INDEX),
	FOREACH_NFS_STAT4(DEFINE_INDEX)
#undef DEFINE_INDEX
		NFSSTAT4_INDEX_LAST,
};

struct metric_proto_op {
	uint64_t total;
	uint64_t errors[NFSSTAT4_INDEX_LAST];
	uint64_t dups;
};

#ifdef __cplusplus
}
#endif

#endif /* EXPORT_METRICS_TYPES_H */
