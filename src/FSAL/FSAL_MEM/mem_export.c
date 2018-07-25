/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017-2018 Red Hat, Inc.
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

#ifdef __FreeBSD__
#include <sys/endian.h>

#define bswap_16(x)     bswap16((x))
#define bswap_64(x)     bswap64((x))
#endif

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
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

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

	retval = fsal_attach_export(fsal_hdl, &myself->export.exports);

	if (retval != 0) {
		/* seriously bad */
		LogMajor(COMPONENT_FSAL,
			 "Could not attach export");
		free_export_ops(&myself->export);
		gsh_free(myself);	/* elvis has left the building */

		return fsalstat(posix2fsal_error(retval), retval);
	}

	myself->export.fsal = fsal_hdl;
	myself->export.up_ops = up_ops;

	/* Save the export path. */
	myself->export_path = gsh_strdup(op_ctx->ctx_export->fullpath);
	op_ctx->fsal_export = &myself->export;

	/* Insert into exports list */
	glist_add_tail(&MEM.mem_exports, &myself->export_entry);

	LogDebug(COMPONENT_FSAL,
		 "Created exp %p - %s",
		 myself, myself->export_path);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}
