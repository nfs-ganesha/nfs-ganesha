/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2018 Red Hat, Inc. and/or its affiliates.
 * Author: Jeff Layton <jlayton@redhat.com>
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
 */
#ifndef _RADOS_GRACE_H
#define _RADOS_GRACE_H

#include <stdbool.h>
#include <stdio.h>

#define DEFAULT_RADOS_GRACE_POOL		"nfs-ganesha"
#define DEFAULT_RADOS_GRACE_OID			"grace"

int rados_grace_create(rados_ioctx_t io_ctx, const char *oid);
int rados_grace_dump(rados_ioctx_t io_ctx, const char *oid, FILE *stream);
int rados_grace_epochs(rados_ioctx_t io_ctx, const char *oid,
			uint64_t *cur, uint64_t *rec);
int rados_grace_enforcing_toggle(rados_ioctx_t io_ctx, const char *oid,
		int nodes, const char * const *nodeids, uint64_t *pcur,
		uint64_t *prec, bool start);
int rados_grace_enforcing_check(rados_ioctx_t io_ctx, const char *oid,
				const char *nodeid);
int rados_grace_join_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
		const char * const *nodeids, uint64_t *pcur, uint64_t *prec,
		bool start);
int rados_grace_lift_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
		const char * const *nodeids, uint64_t *pcur, uint64_t *prec,
		bool remove);
int rados_grace_add(rados_ioctx_t io_ctx, const char *oid, int nodes,
		    const char * const *nodeids);
int rados_grace_member_bulk(rados_ioctx_t io_ctx, const char *oid, int nodes,
			    const char * const *nodeids);

static inline int
rados_grace_enforcing_on(rados_ioctx_t io_ctx, const char *oid,
			const char *nodeid, uint64_t *pcur, uint64_t *prec)
{
	const char *nodeids[1];

	nodeids[0] = nodeid;
	return rados_grace_enforcing_toggle(io_ctx, oid, 1, nodeids, pcur,
						prec, true);
}

static inline int
rados_grace_enforcing_off(rados_ioctx_t io_ctx, const char *oid,
			const char *nodeid, uint64_t *pcur, uint64_t *prec)
{
	const char *nodeids[1];

	nodeids[0] = nodeid;
	return rados_grace_enforcing_toggle(io_ctx, oid, 1, nodeids, pcur,
						prec, false);
}

static inline int
rados_grace_join(rados_ioctx_t io_ctx, const char *oid, const char *nodeid,
		 uint64_t *pcur, uint64_t *prec, bool start)
{
	const char *nodeids[1];

	nodeids[0] = nodeid;
	return rados_grace_join_bulk(io_ctx, oid, 1, nodeids, pcur, prec,
				     start);
}

static inline int
rados_grace_lift(rados_ioctx_t io_ctx, const char *oid, const char *nodeid,
		 uint64_t *pcur, uint64_t *prec)
{
	const char *nodeids[1];

	nodeids[0] = nodeid;
	return rados_grace_lift_bulk(io_ctx, oid, 1, nodeids, pcur, prec,
					false);
}

static inline int
rados_grace_member(rados_ioctx_t io_ctx, const char *oid,
		       const char *nodeid)
{
	const char *nodeids[1];

	nodeids[0] = nodeid;
	return rados_grace_member_bulk(io_ctx, oid, 1, nodeids);
}

#endif /* _RADOS_GRACE_H */
