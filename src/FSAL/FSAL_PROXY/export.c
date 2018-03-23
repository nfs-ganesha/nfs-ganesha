/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Max Matveev, 2012
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
 * 02110-1301 USA
 */

/* Export-related methods */

#include "config.h"

#include "fsal.h"
#include <pthread.h>
#include <sys/types.h>
#include "gsh_list.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "pxy_fsal_methods.h"
#include "nfs_exports.h"
#include "export_mgr.h"

static void pxy_release(struct fsal_export *exp_hdl)
{
	struct pxy_export *pxy_exp =
	    container_of(exp_hdl, struct pxy_export, exp);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(pxy_exp);
}

static attrmask_t pxy_get_supported_attrs(struct fsal_export *exp_hdl)
{
	return fsal_supported_attrs(&exp_hdl->fsal->fs_info);
}

void pxy_export_ops_init(struct export_ops *ops)
{
	ops->release = pxy_release;
	ops->lookup_path = pxy_lookup_path;
	ops->wire_to_host = pxy_wire_to_host;
	ops->create_handle = pxy_create_handle;
	ops->get_fs_dynamic_info = pxy_get_dynamic_info;
	ops->fs_supported_attrs = pxy_get_supported_attrs;
	ops->alloc_state = pxy_alloc_state;
	ops->free_state = pxy_free_state;
};

/* Here and not static because proxy.c needs this function
 * but we also need access to pxy_exp_ops - I'd rather
 * keep the later static then the former */
fsal_status_t pxy_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops)
{
	struct pxy_export *exp = gsh_calloc(1, sizeof(*exp));
	struct pxy_fsal_module *pxy =
	    container_of(fsal_hdl, struct pxy_fsal_module, module);

	fsal_export_init(&exp->exp);
	pxy_export_ops_init(&exp->exp.exp_ops);
	exp->info = &pxy->special;
	exp->exp.fsal = fsal_hdl;
	exp->exp.up_ops = up_ops;
	op_ctx->fsal_export = &exp->exp;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
