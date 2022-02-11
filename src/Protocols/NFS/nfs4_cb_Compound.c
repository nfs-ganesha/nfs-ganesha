// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 * Portions Copyright (C) 2012, The Linux Box Corporation
 * Contributor : Matt Benjamin <matt@linuxbox.com>
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
 * ---------------------------------------
 */

/**
 * \file    nfs4_cb_Compound.c
 * \brief   Routines used for managing the NFS4/CB COMPOUND functions.
 *
 * Routines used for managing the NFS4/CB COMPOUND functions.
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_rpc_callback.h"

static const nfs4_cb_tag_t cbtagtab4[] = {
	{NFS4_CB_TAG_DEFAULT, "Ganesha CB Compound", 19},
};

/* Some CITI-inspired compound helper ideas */

void cb_compound_init_v4(nfs4_compound_t *cbt, uint32_t n_ops,
			 uint32_t minorversion, uint32_t ident, char *tag,
			 uint32_t tag_len)
{
	/* args */
	memset(cbt, 0, sizeof(nfs4_compound_t));	/* XDRS */

	cbt->v_u.v4.args.minorversion = minorversion;
	cbt->v_u.v4.args.callback_ident = ident;
	cbt->v_u.v4.args.argarray.argarray_val = alloc_cb_argop(n_ops);
	cbt->v_u.v4.args.argarray.argarray_len = 0; /* not n_ops, see below */

	if (tag) {
		/* sender must ensure tag is safe to queue */
		cbt->v_u.v4.args.tag.utf8string_val = tag;
		cbt->v_u.v4.args.tag.utf8string_len = tag_len;
	} else {
		cbt->v_u.v4.args.tag.utf8string_val =
		    cbtagtab4[NFS4_CB_TAG_DEFAULT].val;
		cbt->v_u.v4.args.tag.utf8string_len =
		    cbtagtab4[NFS4_CB_TAG_DEFAULT].len;
	}

	cbt->v_u.v4.res.resarray.resarray_val = alloc_cb_resop(n_ops);
	cbt->v_u.v4.res.resarray.resarray_len = 0;

}

void cb_compound_add_op(nfs4_compound_t *cbt, nfs_cb_argop4 *src)
{
	/* old value */
	uint32_t ix = (cbt->v_u.v4.args.argarray.argarray_len)++;
	nfs_cb_argop4 *dst = cbt->v_u.v4.args.argarray.argarray_val + ix;
	*dst = *src;
	/* nothing to do for (zero) val region */
	cbt->v_u.v4.res.resarray.resarray_len++;
}

void cb_compound_free(nfs4_compound_t *cbt)
{
	free_cb_argop(cbt->v_u.v4.args.argarray.argarray_val);
	free_cb_resop(cbt->v_u.v4.res.resarray.resarray_val);
}
