/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/* file.c
 * File I/O methods for NULL module
 */

#include "config.h"

#include <assert.h>
#include "fsal.h"
#include "FSAL/access_check.h"
#include "fsal_convert.h"
#include <unistd.h>
#include <fcntl.h>
#include "FSAL/fsal_commonlib.h"
#include "nullfs_methods.h"


/* nullfs_close
 * Close the file if it is still open.
 * Yes, we ignor lock status.  Closing a file in POSIX
 * releases all locks but that is state and cache inode's problem.
 */

fsal_status_t nullfs_close(struct fsal_obj_handle *obj_hdl)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->close(handle->sub_handle);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_open2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   enum fsal_create_mode createmode,
			   const char *name,
			   struct attrlist *attrs_in,
			   fsal_verifier_t verifier,
			   struct fsal_obj_handle **new_obj,
			   struct attrlist *attrs_out,
			   bool *caller_perm_check)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);
	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);
	struct fsal_obj_handle *sub_handle = NULL;

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->open2(handle->sub_handle, state,
						  openflags, createmode, name,
						  attrs_in, verifier,
						  &sub_handle, attrs_out,
						  caller_perm_check);
	op_ctx->fsal_export = &export->export;

	if (sub_handle) {
		/* wrap the subfsal handle in a nullfs handle. */
		return nullfs_alloc_and_check_handle(export, sub_handle,
						     obj_hdl->fs, new_obj,
						     status);
	}

	return status;
}

bool nullfs_check_verifier(struct fsal_obj_handle *obj_hdl,
			   fsal_verifier_t verifier)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	bool result =
		handle->sub_handle->obj_ops->check_verifier(handle->sub_handle,
							   verifier);
	op_ctx->fsal_export = &export->export;

	return result;
}

fsal_openflags_t nullfs_status2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_openflags_t result =
		handle->sub_handle->obj_ops->status2(handle->sub_handle,
						    state);
	op_ctx->fsal_export = &export->export;

	return result;
}

fsal_status_t nullfs_reopen2(struct fsal_obj_handle *obj_hdl,
			     struct state_t *state,
			     fsal_openflags_t openflags)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->reopen2(handle->sub_handle,
						    state, openflags);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_read2(struct fsal_obj_handle *obj_hdl,
			   bool bypass,
			   struct state_t *state,
			   uint64_t offset,
			   size_t buf_size,
			   void *buffer,
			   size_t *read_amount,
			   bool *eof,
			   struct io_info *info)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->read2(handle->sub_handle, bypass,
						  state, offset, buf_size,
						  buffer, read_amount, eof,
						  info);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_write2(struct fsal_obj_handle *obj_hdl,
			    bool bypass,
			    struct state_t *state,
			    uint64_t offset,
			    size_t buf_size,
			    void *buffer,
			    size_t *write_amount,
			    bool *fsal_stable,
			    struct io_info *info)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->write2(handle->sub_handle, bypass,
						  state, offset, buf_size,
						  buffer, write_amount,
						  fsal_stable, info);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_seek2(struct fsal_obj_handle *obj_hdl,
			   struct state_t *state,
			   struct io_info *info)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->seek2(handle->sub_handle, state,
						  info);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_io_advise2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state,
				struct io_hints *hints)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->io_advise2(handle->sub_handle,
						       state, hints);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_commit2(struct fsal_obj_handle *obj_hdl, off_t offset,
			     size_t len)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->commit2(handle->sub_handle, offset,
						    len);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_lock_op2(struct fsal_obj_handle *obj_hdl,
			      struct state_t *state,
			      void *p_owner,
			      fsal_lock_op_t lock_op,
			      fsal_lock_param_t *req_lock,
			      fsal_lock_param_t *conflicting_lock)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->lock_op2(handle->sub_handle, state,
						     p_owner, lock_op, req_lock,
						     conflicting_lock);
	op_ctx->fsal_export = &export->export;

	return status;
}

fsal_status_t nullfs_close2(struct fsal_obj_handle *obj_hdl,
			    struct state_t *state)
{
	struct nullfs_fsal_obj_handle *handle =
		container_of(obj_hdl, struct nullfs_fsal_obj_handle,
			     obj_handle);

	struct nullfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct nullfs_fsal_export,
			     export);

	/* calling subfsal method */
	op_ctx->fsal_export = export->export.sub_export;
	fsal_status_t status =
		handle->sub_handle->obj_ops->close2(handle->sub_handle, state);
	op_ctx->fsal_export = &export->export;

	return status;
}
