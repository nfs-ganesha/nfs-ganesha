// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * Copyright (C) 2019 Skytechnology sp. z o.o.
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "fsal.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include "fsal_types.h"
#include "fsal_up.h"
#include "FSAL/fsal_commonlib.h"
#include "../fsal_private.h"
#include "nfs_exports.h"
#include "pnfs_utils.h"

#include "context_wrap.h"
#include "lzfs_internal.h"

static void lzfs_int_clear_fileinfo_cache(struct lzfs_fsal_export *lzfs_export,
					  int count)
{
	assert(lzfs_export->fileinfo_cache);

	for (int i = 0; i < count; ++i) {
		liz_fileinfo_entry_t *cache_handle;
		liz_fileinfo_t *file_handle;

		cache_handle = liz_fileinfo_cache_pop_expired(
						lzfs_export->fileinfo_cache);
		if (cache_handle == NULL) {
			break;
		}

		file_handle = liz_extract_fileinfo(cache_handle);
		liz_release(lzfs_export->lzfs_instance, file_handle);
		liz_fileinfo_entry_free(cache_handle);
	}
}

/*! \brief Clean up a DS handle
 *
 * \see fsal_api.h for more information
 */
static void lzfs_fsal_ds_handle_release(struct fsal_ds_handle *const ds_pub)
{
	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_ds_handle *lzfs_ds;

	lzfs_export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
				   struct lzfs_fsal_export, export);
	lzfs_ds = container_of(ds_pub, struct lzfs_fsal_ds_handle, ds);

	assert(lzfs_export->fileinfo_cache);

	if (lzfs_ds->cache_handle != NULL) {
		liz_fileinfo_cache_release(lzfs_export->fileinfo_cache,
					   lzfs_ds->cache_handle);
	}

	gsh_free(lzfs_ds);

	lzfs_int_clear_fileinfo_cache(lzfs_export, 5);
}

static nfsstat4 lzfs_int_openfile(struct lzfs_fsal_export *lzfs_export,
				  struct lzfs_fsal_ds_handle *lzfs_ds)
{
	assert(lzfs_export->fileinfo_cache);

	if (lzfs_ds->cache_handle != NULL) {
		return NFS4_OK;
	}

	lzfs_int_clear_fileinfo_cache(lzfs_export, 2);

	lzfs_ds->cache_handle = liz_fileinfo_cache_acquire(
					lzfs_export->fileinfo_cache,
					lzfs_ds->inode);
	if (lzfs_ds->cache_handle == NULL) {
		return NFS4ERR_IO;
	}

	liz_fileinfo_t *file_handle = liz_extract_fileinfo(
							lzfs_ds->cache_handle);

	if (file_handle != NULL) {
		return NFS4_OK;
	}

	file_handle = liz_cred_open(lzfs_export->lzfs_instance, NULL,
				    lzfs_ds->inode, O_RDWR);
	if (file_handle == NULL) {
		liz_fileinfo_cache_erase(lzfs_export->fileinfo_cache,
					 lzfs_ds->cache_handle);
		lzfs_ds->cache_handle = NULL;
		return NFS4ERR_IO;
	}

	liz_attach_fileinfo(lzfs_ds->cache_handle, file_handle);

	return NFS4_OK;
}

/*! \brief Read from a data-server handle.
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_ds_handle_read(struct fsal_ds_handle *const ds_hdl,
					 const stateid4 *stateid,
					 const offset4 offset,
					 const count4 requested_length,
					 void *const buffer,
					 count4 *const supplied_length,
					 bool *const end_of_file)
{
	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_ds_handle *lzfs_ds;
	liz_fileinfo_t *file_handle;
	ssize_t nb_read;
	nfsstat4 nfs_status;

	lzfs_export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
				   struct lzfs_fsal_export, export);
	lzfs_ds = container_of(ds_hdl, struct lzfs_fsal_ds_handle, ds);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32, lzfs_export->export.export_id,
		     lzfs_ds->inode, offset, requested_length);

	nfs_status = lzfs_int_openfile(lzfs_export, lzfs_ds);
	if (nfs_status != NFS4_OK) {
		return nfs_status;
	}

	file_handle = liz_extract_fileinfo(lzfs_ds->cache_handle);
	nb_read = liz_cred_read(lzfs_export->lzfs_instance, NULL, file_handle,
				offset, requested_length, buffer);

	if (nb_read < 0) {
		return lzfs_nfs4_last_err();
	}

	*supplied_length = nb_read;
	*end_of_file = (nb_read == 0);

	return NFS4_OK;
}

/*! \brief Write to a data-server handle.
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_ds_handle_write(struct fsal_ds_handle *const ds_hdl,
					 const stateid4 *stateid,
					 const offset4 offset,
					 const count4 write_length,
					 const void *buffer,
					 const stable_how4 stability_wanted,
					 count4 * const written_length,
					 verifier4 * const writeverf,
					 stable_how4 * const stability_got)
{
	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_ds_handle *lzfs_ds;
	liz_fileinfo_t *file_handle;
	ssize_t nb_write;
	nfsstat4 nfs_status;

	lzfs_export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
				   struct lzfs_fsal_export, export);
	lzfs_ds = container_of(ds_hdl, struct lzfs_fsal_ds_handle, ds);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32, lzfs_export->export.export_id,
		     lzfs_ds->inode, offset, write_length);

	nfs_status = lzfs_int_openfile(lzfs_export, lzfs_ds);
	if (nfs_status != NFS4_OK) {
		return nfs_status;
	}

	file_handle = liz_extract_fileinfo(lzfs_ds->cache_handle);
	nb_write = liz_cred_write(lzfs_export->lzfs_instance, NULL,
				  file_handle, offset, write_length, buffer);

	if (nb_write < 0) {
		return lzfs_nfs4_last_err();
	}

	int rc = 0;

	if (stability_wanted != UNSTABLE4) {
		rc = liz_cred_flush(lzfs_export->lzfs_instance,
				    NULL,
				    file_handle);
	}

	*written_length = nb_write;
	*stability_got = (rc < 0) ? UNSTABLE4 : stability_wanted;

	return NFS4_OK;
}

/*! \brief Commit a byte range to a DS handle.
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_ds_handle_commit(struct fsal_ds_handle *const ds_hdl,
					   const offset4 offset,
					   const count4 count,
					   verifier4 * const writeverf)
{
	struct lzfs_fsal_export *lzfs_export;
	struct lzfs_fsal_ds_handle *lzfs_ds;
	liz_fileinfo_t *file_handle;
	nfsstat4 nfs_status;
	int rc;

	memset(writeverf, 0, NFS4_VERIFIER_SIZE);

	lzfs_export = container_of(op_ctx->ctx_pnfs_ds->mds_fsal_export,
				   struct lzfs_fsal_export, export);
	lzfs_ds = container_of(ds_hdl, struct lzfs_fsal_ds_handle, ds);

	LogFullDebug(COMPONENT_FSAL,
		     "export=%" PRIu16 " inode=%" PRIu32 " offset=%" PRIu64
		     " size=%" PRIu32, lzfs_export->export.export_id,
		     lzfs_ds->inode, offset, count);

	nfs_status = lzfs_int_openfile(lzfs_export, lzfs_ds);
	if (nfs_status != NFS4_OK) {
		// If we failed here then there is no opened
		// LizardFS file descriptor, which implies that we don't need
		// to flush anything.
		return NFS4_OK;
	}

	file_handle = liz_extract_fileinfo(lzfs_ds->cache_handle);

	rc = liz_cred_flush(lzfs_export->lzfs_instance, NULL, file_handle);
	if (rc < 0) {
		LogMajor(COMPONENT_PNFS, "ds_commit() failed  '%s'",
			 liz_error_string(liz_last_err()));
		return NFS4ERR_INVAL;
	}

	return NFS4_OK;
}

/*! \brief Read plus from a data-server handle.
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_ds_read_plus(struct fsal_ds_handle *const ds_hdl,
				       const stateid4 *stateid,
				       const offset4 offset,
				       const count4 requested_length,
				       void *const buffer,
				       const count4 supplied_length,
				       bool *const end_of_file,
				       struct io_info *info)
{
	LogCrit(COMPONENT_PNFS, "Unimplemented DS read_plus!");
	return NFS4ERR_NOTSUPP;
}

/*! \brief Create a FSAL data server handle from a wire handle
 *
 * \see fsal_api.h for more information
 */
static nfsstat4 lzfs_fsal_make_ds_handle(struct fsal_pnfs_ds *const pds,
					 const struct gsh_buffdesc *const desc,
					 struct fsal_ds_handle **const handle,
					 int flags)
{
	struct lzfs_fsal_ds_wire *dsw = (struct lzfs_fsal_ds_wire *)desc->addr;
	struct lzfs_fsal_ds_handle *lzfs_ds;

	*handle = NULL;

	if (desc->len != sizeof(struct lzfs_fsal_ds_wire))
		return NFS4ERR_BADHANDLE;
	if (dsw->inode == 0)
		return NFS4ERR_BADHANDLE;

	lzfs_ds = gsh_calloc(1, sizeof(struct lzfs_fsal_ds_handle));

	*handle = &lzfs_ds->ds;

	if (flags & FH_FSAL_BIG_ENDIAN) {
#if (BYTE_ORDER != BIG_ENDIAN)
		lzfs_ds->inode = bswap_32(dsw->inode);
#else
		lzfs_ds->inode = dsw->inode;
#endif
	} else {
#if (BYTE_ORDER == BIG_ENDIAN)
		lzfs_ds->inode = bswap_32(dsw->inode);
#else
		lzfs_ds->inode = dsw->inode;
#endif
	}

	return NFS4_OK;
}

void lzfs_fsal_ds_handle_ops_init(struct fsal_pnfs_ds_ops *ops)
{
	memcpy(ops, &def_pnfs_ds_ops, sizeof(struct fsal_pnfs_ds_ops));
	ops->make_ds_handle = lzfs_fsal_make_ds_handle;
	ops->dsh_release = lzfs_fsal_ds_handle_release;
	ops->dsh_read = lzfs_fsal_ds_handle_read;
	ops->dsh_write = lzfs_fsal_ds_handle_write;
	ops->dsh_commit = lzfs_fsal_ds_handle_commit;
	ops->dsh_read_plus = lzfs_fsal_ds_read_plus;
}
