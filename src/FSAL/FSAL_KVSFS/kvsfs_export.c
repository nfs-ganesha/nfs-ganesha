/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * -------------
 */

/* export.c
 * KVSFS FSAL export object
 */

#include "config.h"

#include <libgen.h>	     /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include "fsal.h"
#include "fsal_api.h"
#include "fsal_types.h"
#include "fsal_pnfs.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_config.h"
#include "pnfs_utils.h"

#include "kvsfs_fsal_internal.h"
#include "kvsfs_methods.h"

/* export object methods
 */

static void kvsfs_export_release(struct fsal_export *exp_hdl)
{
	struct kvsfs_fsal_export *myself;

	myself = container_of(exp_hdl, struct kvsfs_fsal_export, export);

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	gsh_free(myself);		/* elvis has left the building */
}

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	/** @todo I'm not sure how this gets away without filling anything in.
	 */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t kvsfs_wire_to_host(struct fsal_export *exp_hdl,
					fsal_digesttype_t in_type,
					struct gsh_buffdesc *fh_desc,
					int flags)
{
	struct kvsfs_file_handle *hdl;
	size_t fh_size;

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	hdl = (struct kvsfs_file_handle *)fh_desc->addr;
	fh_size = kvsfs_sizeof_handle(hdl);
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %u",
			 (unsigned long)fh_size,
			 (unsigned int)fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	fh_desc->len = fh_size;	/* pass back the actual size */
	return fsalstat(ERR_FSAL_NO_ERROR, 0);

}

static attrmask_t kvsfs_supported_attrs(struct fsal_export *exp_hdl)
{
	attrmask_t supported_mask;

	supported_mask = fsal_supported_attrs(&exp_hdl->fsal->fs_info);

	supported_mask &= ~ATTR_ACL;

	return supported_mask;
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl	       Export state_t will be associated with
 * @param[in] state_type	    Type of state to allocate
 * @param[in] related_state	 Related state if appropriate
 *
 * @returns a state structure.
 */

static struct state_t *kvsfs_alloc_state(struct fsal_export *exp_hdl,
					 enum state_type state_type,
					 struct state_t *related_state)
{
	struct state_t *state;
	struct kvsfs_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct kvsfs_state_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct kvsfs_state_fd, state)->kvsfs_fd;

	memset(&my_fd->fd, 0, sizeof(kvsns_file_open_t));
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;

}

/**
 * @brief free a gpfs_state_fd structure
 *
 * @param[in] exp_hdl  Export state_t will be associated with
 * @param[in] state    Related state if appropriate
 *
 */
static void kvsfs_free_state(struct fsal_export *exp_hdl,
			    struct state_t *state)
{
	struct kvsfs_state_fd *state_fd = container_of(state,
						       struct kvsfs_state_fd,
						       state);
	struct kvsfs_fd *my_fd = &state_fd->kvsfs_fd;

	PTHREAD_RWLOCK_destroy(&my_fd->fdlock);
	gsh_free(state_fd);
}

/* kvsfs_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void kvsfs_export_ops_init(struct export_ops *ops)
{
	ops->release = kvsfs_export_release;
	ops->lookup_path = kvsfs_lookup_path;
	ops->wire_to_host = kvsfs_wire_to_host;
	ops->create_handle = kvsfs_create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supported_attrs = kvsfs_supported_attrs;
	ops->alloc_state = kvsfs_alloc_state;
	ops->free_state = kvsfs_free_state;

}

static int kvsfs_conf_pnfs_commit(void *node,
				  void *link_mem,
				  void *self_struct,
				  struct config_error_type *err_type)
{
	/* struct lustre_pnfs_param *lpp = self_struct; */

	/* Verifications/parameter checking to be added here */

	return 0;
}


static struct config_item ds_array_params[] = {
	CONF_MAND_IP_ADDR("DS_Addr", "127.0.0.1",
			  kvsfs_pnfs_ds_parameter, ipaddr),
	CONF_ITEM_UI16("DS_Port", 1024, UINT16_MAX, 2049,
		       kvsfs_pnfs_ds_parameter, ipport), /* default is nfs */
	CONFIG_EOL
};

static struct config_item pnfs_params[] = {
	CONF_MAND_UI32("Stripe_Unit", 8192, 1024*1024, 1024,
		       kvsfs_exp_pnfs_parameter, stripe_unit),
	CONF_ITEM_BOOL("pnfs_enabled", false,
		       kvsfs_exp_pnfs_parameter, pnfs_enabled),

	CONF_MAND_UI32("Nb_Dataserver", 1, 4, 1,
		       kvsfs_exp_pnfs_parameter, nb_ds),

	CONF_ITEM_BLOCK("DS1", ds_array_params,
			noop_conf_init, noop_conf_commit,
			kvsfs_exp_pnfs_parameter, ds_array[0]),

	CONF_ITEM_BLOCK("DS2", ds_array_params,
			noop_conf_init, noop_conf_commit,
			kvsfs_exp_pnfs_parameter, ds_array[1]),

	CONF_ITEM_BLOCK("DS3", ds_array_params,
			noop_conf_init, noop_conf_commit,
			kvsfs_exp_pnfs_parameter, ds_array[2]),

	CONF_ITEM_BLOCK("DS4", ds_array_params,
			noop_conf_init, noop_conf_commit,
			kvsfs_exp_pnfs_parameter, ds_array[3]),
	CONFIG_EOL

};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_STR("kvsns_config", 0, MAXPATHLEN, KVSNS_DEFAULT_CONFIG,
		      kvsfs_fsal_export, kvsns_config),
	CONF_ITEM_BLOCK("PNFS", pnfs_params,
			noop_conf_init, kvsfs_conf_pnfs_commit,
			kvsfs_fsal_export, pnfs_param),
	CONFIG_EOL
};


static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.kvsfs-export",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t kvsfs_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops)
{
	struct kvsfs_fsal_export *myself = NULL;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	int retval = 0;
	fsal_errors_t fsal_error = ERR_FSAL_INVAL;

	myself = gsh_calloc(1, sizeof(struct kvsfs_fsal_export));

	fsal_export_init(&myself->export);
	kvsfs_export_ops_init(&myself->export.exp_ops);
	myself->export.up_ops = up_ops;

	LogDebug(COMPONENT_FSAL, "kvsfs_create_export");

	retval = load_config_from_node(parse_node,
				       &export_param,
				       myself,
				       true,
				       err_type);
	if (retval != 0)
		goto errout;

	retval = kvsns_start(myself->kvsns_config);
	if (retval != 0) {
		LogMajor(COMPONENT_FSAL, "Can't start KVSNS API: %d (%s)",
			 retval, strerror(-retval));
		goto errout;
	} else
		LogEvent(COMPONENT_FSAL, "KVSNS API is running, config = %s",
			 myself->kvsns_config);

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);
	if (retval != 0)
		goto err_locked;	/* seriously bad */
	myself->export.fsal = fsal_hdl;

	op_ctx->fsal_export = &myself->export;

	myself->pnfs_ds_enabled =
	    myself->export.exp_ops.fs_supports(&myself->export,
					    fso_pnfs_ds_supported) &&
					    myself->pnfs_param.pnfs_enabled;
	myself->pnfs_mds_enabled =
	    myself->export.exp_ops.fs_supports(&myself->export,
					    fso_pnfs_mds_supported) &&
					    myself->pnfs_param.pnfs_enabled;

	if (myself->pnfs_ds_enabled) {
		struct fsal_pnfs_ds *pds = NULL;

		status = fsal_hdl->m_ops.create_fsal_pnfs_ds(fsal_hdl,
							     parse_node,
							     &pds);
		if (status.major != ERR_FSAL_NO_ERROR)
			goto err_locked;

		/* special case: server_id matches export_id */
		pds->id_servers = op_ctx->ctx_export->export_id;
		pds->mds_export = op_ctx->ctx_export;

		if (!pnfs_ds_insert(pds)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pds->id_servers);
			status.major = ERR_FSAL_EXIST;
			fsal_pnfs_ds_fini(pds);
			gsh_free(pds);
			goto err_locked;
		}

		LogInfo(COMPONENT_FSAL,
			"kvsfs_fsal_create: pnfs DS was enabled");
	}

	if (myself->pnfs_mds_enabled) {
		LogInfo(COMPONENT_FSAL,
			"kvsfs_fsal_create: pnfs MDS was enabled");
		export_ops_pnfs(&myself->export.exp_ops);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err_locked:
	if (myself->export.fsal != NULL)
		fsal_detach_export(fsal_hdl, &myself->export.exports);
errout:
	/* elvis has left the building */
	gsh_free(myself);

	return fsalstat(fsal_error, retval);

}
