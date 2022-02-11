/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2012, The Linux Box Corporation
 * Copyright (c) 2012-2017 Red Hat, Inc. and/or its affiliates.
 * Contributor : Matt Benjamin <matt@linuxbox.com>
 *               William Allen Simpson <william.allen.simpson@gmail.com>
 *
 * Some portions Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 *
 * -------------
 */

#ifndef NFS_RPC_CALLBACK_H
#define NFS_RPC_CALLBACK_H

#include "config.h"
#include "log.h"
#include "nfs_core.h"

/**
 * @file nfs_rpc_callback.h
 * @author Matt Benjamin <matt@linuxbox.com>
 * @author Lee Dobryden <lee@linuxbox.com>
 * @brief RPC callback dispatch package
 *
 * This module implements APIs for submission, and dispatch of NFSv4.0
 * and NFSv4.1 format callbacks.
 */

/* Definition in sal_data.h */
struct state_refer;

/* XXX move? */
typedef struct nfs4_cb_tag {
	int32_t ix;
	char *val;
	int32_t len;
} nfs4_cb_tag_t;

/* CB compound tags */
#define NFS4_CB_TAG_DEFAULT 0

void cb_compound_init_v4(nfs4_compound_t *cbt, uint32_t n_ops,
			 uint32_t minorversion, uint32_t ident, char *tag,
			 uint32_t tag_len);

void cb_compound_add_op(nfs4_compound_t *cbt, nfs_cb_argop4 *src);

void cb_compound_free(nfs4_compound_t *cbt);

#define NFS_CB_FLAG_NONE 0x0000
#define NFS_RPC_FLAG_NONE 0x0000

enum nfs_cb_call_states {
	NFS_CB_CALL_DISPATCH,
	NFS_CB_CALL_FINISHED,
	NFS_CB_CALL_ABORTED,
};

rpc_call_t *alloc_rpc_call(void);
void free_rpc_call(rpc_call_t *call);

static inline nfs_cb_argop4 *alloc_cb_argop(uint32_t cnt)
{
	return gsh_calloc(cnt, sizeof(nfs_cb_argop4));
}

static inline nfs_cb_resop4 *alloc_cb_resop(uint32_t cnt)
{
	return gsh_calloc(cnt, sizeof(nfs_cb_resop4));
}

static inline void free_cb_argop(nfs_cb_argop4 *ptr)
{
	gsh_free(ptr);
}

static inline void free_cb_resop(nfs_cb_resop4 *ptr)
{
	gsh_free(ptr);
}

static inline bool get_cb_chan_down(struct nfs_client_id_t *clid)
{
	return clid->cid_cb.v40.cb_chan_down;
}

static inline void set_cb_chan_down(struct nfs_client_id_t *clid, bool down)
{
	clid->cid_cb.v40.cb_chan_down = down;
}

rpc_call_channel_t *nfs_rpc_get_chan(nfs_client_id_t *pclientid,
				     uint32_t flags);

void nfs_rpc_cb_pkginit(void);
void nfs_rpc_cb_pkgshutdown(void);
int nfs_rpc_create_chan_v40(nfs_client_id_t *pclientid, uint32_t flags);

int nfs_rpc_create_chan_v41(SVCXPRT *xprt, nfs41_session_t *session,
			    int num_sec_parms, callback_sec_parms4 *sec_parms);

#define NFS_RPC_CALL_NONE 0x0000

enum clnt_stat nfs_rpc_call(rpc_call_t *call, uint32_t flags);

int nfs_rpc_cb_single(nfs_client_id_t *clientid, nfs_cb_argop4 *op,
		       struct state_refer *refer,
		       void (*completion)(rpc_call_t *),
		       void *completion_arg);
void nfs41_release_single(rpc_call_t *call);
enum clnt_stat nfs_test_cb_chan(nfs_client_id_t *);

#endif /* !NFS_RPC_CALLBACK_H */
