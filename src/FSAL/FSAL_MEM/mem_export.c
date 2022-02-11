// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017-2019 Red Hat, Inc.
 * Author: Daniel Gryniewicz  dang@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* export.c
 * MEM FSAL export object
 */

#include "config.h"

#include "fsal.h"
#include <misc/portable.h>	/* used for 'bswap*' */
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <os/mntent.h>
#include <os/quota.h>
#include <dlfcn.h>
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "mem_int.h"
#include "nfs_exports.h"
#include "nfs_core.h"
#include "export_mgr.h"
#include <misc/portable.h>

#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif
/* helpers to/from other MEM objects
 */

/* export object methods
 */

static void mem_release_export(struct fsal_export *exp_hdl)
{
	struct mem_fsal_export *myself;

	myself = container_of(exp_hdl, struct mem_fsal_export, export);

	if (myself->root_handle != NULL) {
		mem_clean_export(myself->root_handle);

		fsal_obj_handle_fini(&myself->root_handle->obj_handle);

		LogDebug(COMPONENT_FSAL,
			 "Releasing hdl=%p, name=%s",
			 myself->root_handle, myself->root_handle->m_name);

		PTHREAD_RWLOCK_wrlock(&myself->mfe_exp_lock);
		mem_free_handle(myself->root_handle);
		PTHREAD_RWLOCK_unlock(&myself->mfe_exp_lock);

		myself->root_handle = NULL;
	}

	fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
	free_export_ops(exp_hdl);

	glist_del(&myself->export_entry);

	gsh_free(myself->export_path);
	gsh_free(myself);
}

static fsal_status_t mem_get_dynamic_info(struct fsal_export *exp_hdl,
					  struct fsal_obj_handle *obj_hdl,
					  fsal_dynamicfsinfo_t *infop)
{
	infop->total_bytes = 0;
	infop->free_bytes = 0;
	infop->avail_bytes = 0;
	infop->total_files = 0;
	infop->free_files = 0;
	infop->avail_files = 0;
	infop->time_delta.tv_sec = 0;
	infop->time_delta.tv_nsec = FSAL_DEFAULT_TIME_DELTA_NSEC;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* extract a file handle from a buffer.
 * do verification checks and flag any and all suspicious bits.
 * Return an updated fh_desc into whatever was passed.  The most
 * common behavior, done here is to just reset the length.  There
 * is the option to also adjust the start pointer.
 */

static fsal_status_t mem_wire_to_host(struct fsal_export *exp_hdl,
				      fsal_digesttype_t in_type,
				      struct gsh_buffdesc *fh_desc,
				      int flags)
{
	size_t fh_min;
	uint64_t *hashkey;
	ushort *len;

	fh_min = 1;

	if (fh_desc->len < fh_min) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be >= %zu, got %zu",
			 fh_min, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}
	hashkey = (uint64_t *)fh_desc->addr;
	len = (ushort *)((char *)hashkey + sizeof(uint64_t));
	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		*len = bswap_16(*len);
		*hashkey = bswap_64(*hashkey);
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		*len = bswap_16(*len);
		*hashkey = bswap_64(*hashkey);
#endif
	}
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl               Export state_t will be associated with
 * @param[in] state_type            Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */

static struct state_t *mem_alloc_state(struct fsal_export *exp_hdl,
				       enum state_type state_type,
				       struct state_t *related_state)
{
	struct state_t *state;

	state = init_state(gsh_calloc(1, sizeof(struct state_t)
				      + sizeof(struct fsal_fd)),
			   exp_hdl, state_type, related_state);
#ifdef USE_LTTNG
	tracepoint(fsalmem, mem_alloc_state, __func__, __LINE__, state);
#endif
	return state;
}

/* mem_export_ops_init
 * overwrite vector entries with the methods that we support
 */

void mem_export_ops_init(struct export_ops *ops)
{
	ops->release = mem_release_export;
	ops->lookup_path = mem_lookup_path;
	ops->wire_to_host = mem_wire_to_host;
	ops->create_handle = mem_create_handle;
	ops->get_fs_dynamic_info = mem_get_dynamic_info;
	ops->alloc_state = mem_alloc_state;
}

const char *str_async_type(uint32_t async_type)
{
	switch (async_type) {
	case MEM_INLINE:
		return "INLINE";
	case MEM_RANDOM_OR_INLINE:
		return "RANDOM_OR_INLINE";
	case MEM_RANDOM:
		return "RANDOM";
	case MEM_FIXED:
		return "FIXED";
	}

	return "UNKNOWN";
}

static struct config_item_list async_types_conf[] = {
	CONFIG_LIST_TOK("inline",		MEM_INLINE),
	CONFIG_LIST_TOK("fixed",		MEM_FIXED),
	CONFIG_LIST_TOK("random",		MEM_RANDOM),
	CONFIG_LIST_TOK("random_or_inline",	MEM_RANDOM_OR_INLINE),
	CONFIG_LIST_EOL
};

static struct config_item mem_export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_ITEM_UI32("Async_Delay", 0, 1000, 0,
		       mem_fsal_export, async_delay),
	CONF_ITEM_TOKEN("Async_Type", MEM_INLINE, async_types_conf,
			mem_fsal_export, async_type),
	CONF_ITEM_UI32("Async_Stall_Delay", 0, 1000, 0,
		       mem_fsal_export, async_stall_delay),
	CONFIG_EOL
};

static struct config_block mem_export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.mem-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = mem_export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/* create_export
 * Create an export point and return a handle to it to be kept
 * in the export list.
 * First lookup the fsal, then create the export and then put the fsal back.
 * returns the export with one reference taken.
 */

fsal_status_t mem_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops)
{
	struct mem_fsal_export *myself;
	int retval = 0;
	pthread_rwlockattr_t attrs;
	fsal_status_t fsal_status = {0, 0};

	myself = gsh_calloc(1, sizeof(struct mem_fsal_export));

	glist_init(&myself->mfe_objs);
	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&myself->mfe_exp_lock, &attrs);
	pthread_rwlockattr_destroy(&attrs);
	fsal_export_init(&myself->export);
	mem_export_ops_init(&myself->export.exp_ops);

	retval = load_config_from_node(parse_node,
				       &mem_export_param_block,
				       myself,
				       true,
				       err_type);

	if (retval != 0) {
		fsal_status = posix2fsal_status(EINVAL);
		goto err_free;	/* seriously bad */
	}

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);

	if (retval != 0) {
		/* seriously bad */
		LogMajor(COMPONENT_FSAL,
			 "Could not attach export");
		fsal_status = posix2fsal_status(retval);
		goto err_free;	/* seriously bad */
	}

	myself->export.fsal = fsal_hdl;
	myself->export.up_ops = up_ops;

	/* Save the export path. */
	myself->export_path = gsh_strdup(CTX_FULLPATH(op_ctx));
	op_ctx->fsal_export = &myself->export;

	/* Insert into exports list */
	glist_add_tail(&MEM.mem_exports, &myself->export_entry);

	LogDebug(COMPONENT_FSAL,
		 "Created exp %p - %s",
		 myself, myself->export_path);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

err_free:
	free_export_ops(&myself->export);
	gsh_free(myself);	/* elvis has left the building */
	return fsal_status;
}

/**
 * @brief Update an existing export
 *
 * This will result in a temporary fsal_export being created, and built into
 * a stacked export.
 *
 * On entry, op_ctx has the original gsh_export and no fsal_export.
 *
 * The caller passes the original fsal_export, as well as the new super_export's
 * FSAL when there is a stacked export. This will allow the underlying export to
 * validate that the stacking has not changed.
 *
 * This function does not actually create a new fsal_export, the only purpose is
 * to validate and update the config.
 *
 * @param[in]     fsal_hdl         FSAL module
 * @param[in]     parse_node       opaque pointer to parse tree node for
 *                                 export options to be passed to
 *                                 load_config_from_node
 * @param[out]    err_type         config proocessing error reporting
 * @param[in]     original         The original export that is being updated
 * @param[in]     updated_super    The updated super_export's FSAL
 *
 * @return FSAL status.
 */

fsal_status_t mem_update_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				struct fsal_export *original,
				struct fsal_module *updated_super)
{
	struct mem_fsal_export myself;
	int retval = 0;
	struct mem_fsal_export *orig =
		container_of(original, struct mem_fsal_export, export);
	fsal_status_t status;

	/* Check for changes in stacking by calling default update_export. */
	status = update_export(fsal_hdl, parse_node, err_type,
			       original, updated_super);

	if (FSAL_IS_ERROR(status))
		return status;

	memset(&myself, 0, sizeof(myself));

	retval = load_config_from_node(parse_node,
				       &mem_export_param_block,
				       &myself,
				       true,
				       err_type);

	if (retval != 0) {
		return posix2fsal_status(EINVAL);
	}

	/* Update the async parameters */
	atomic_store_uint32_t(&orig->async_delay, myself.async_delay);
	atomic_store_uint32_t(&orig->async_stall_delay,
			      myself.async_stall_delay);
	atomic_store_uint32_t(&orig->async_type, myself.async_type);

	LogEvent(COMPONENT_FSAL,
		 "Updated FSAL_MEM aync parameters type=%s, delay=%"PRIu32
		 ", stall_delay=%"PRIu32,
		 str_async_type(myself.async_type),
		 myself.async_delay, myself.async_stall_delay);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
