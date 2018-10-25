 /*
  * vim:noexpandtab:shiftwidth=8:tabstop=8:
  *
  * Copyright CEA/DAM/DIF  (2008)
  * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
  *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
  *
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU Lesser General Public License
  * as published by the Free Software Foundation; either version 3 of
  * the License, or (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  * Lesser General Public License for more details.
  *
  * You should have received a copy of the GNU Lesser General Public
  * License along with this library; if not, write to the Free
  * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  * Boston, MA 02110-1301 USA
  *
  * ---------------------------------------
  */

/**
 * @file    nfs4_op_create.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include "log.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_convert.h"
#include "nfs_file_handle.h"
#include "export_mgr.h"

/**
 * @brief NFS4_OP_CREATE, creates a non-regular entry
 *
 * This function implements the NFS4_OP_CREATE operation, which
 * creates a non-regular entry.
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, p. 363
 */

enum nfs_req_result nfs4_op_create(struct nfs_argop4 *op, compound_data_t *data,
				   struct nfs_resop4 *resp)
{
	CREATE4args * const arg_CREATE4 = &op->nfs_argop4_u.opcreate;
	CREATE4res * const res_CREATE4 = &resp->nfs_resop4_u.opcreate;

	struct fsal_obj_handle *obj_parent = NULL;
	struct fsal_obj_handle *obj_new = NULL;
	struct attrlist sattr;
	int convrc = 0;
	char *name = NULL;
	char *link_content = NULL;
	struct fsal_export *exp_hdl;
	fsal_status_t fsal_status;
	object_file_type_t type;

	memset(&sattr, 0, sizeof(sattr));

	resp->resop = NFS4_OP_CREATE;
	res_CREATE4->status = NFS4_OK;

	/* Do basic checks on a filehandle */
	res_CREATE4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);
	if (res_CREATE4->status != NFS4_OK)
		goto out;

	/* if quota support is active, then we should check is the FSAL allows
	 * inode creation or not */
	exp_hdl = op_ctx->fsal_export;

	fsal_status = exp_hdl->exp_ops.check_quota(exp_hdl,
						op_ctx->ctx_export->fullpath,
						FSAL_QUOTA_INODES);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_CREATE4->status = NFS4ERR_DQUOT;
		goto out;
	}

	/* Ask only for supported attributes */
	if (!nfs4_Fattr_Supported(&arg_CREATE4->createattrs)) {
		res_CREATE4->status = NFS4ERR_ATTRNOTSUPP;
		goto out;
	}

	/* Do not use READ attr, use WRITE attr */
	if (!nfs4_Fattr_Check_Access
	    (&arg_CREATE4->createattrs, FATTR4_ATTR_WRITE)) {
		res_CREATE4->status = NFS4ERR_INVAL;
		goto out;
	}

	/* This operation is used to create a non-regular file,
	 * this means: - a symbolic link
	 *             - a block device file
	 *             - a character device file
	 *             - a socket file
	 *             - a fifo
	 *             - a directory
	 *
	 * You can't use this operation to create a regular file,
	 * you have to use NFS4_OP_OPEN for this
	 */

	/* Validate and convert the UFT8 objname to a regular string */
	res_CREATE4->status = nfs4_utf8string2dynamic(&arg_CREATE4->objname,
						      UTF8_SCAN_ALL,
						      &name);

	if (res_CREATE4->status != NFS4_OK)
		goto out;

	/* Convert current FH into a obj, the current_obj
	   (assocated with the current FH will be used for this */
	obj_parent = data->current_obj;

	/* The currentFH must point to a directory
	 * (objects are always created within a directory)
	 */
	if (data->current_filetype != DIRECTORY) {
		res_CREATE4->status = NFS4ERR_NOTDIR;
		goto out;
	}

	res_CREATE4->CREATE4res_u.resok4.cinfo.before =
		fsal_get_changeid4(obj_parent);

	/* Convert the incoming fattr4 to a vattr structure,
	 * if such arguments are supplied
	 */
	if (arg_CREATE4->createattrs.attrmask.bitmap4_len != 0) {
		/* Arguments were supplied, extract them */
		convrc = nfs4_Fattr_To_FSAL_attr(&sattr,
						 &arg_CREATE4->createattrs,
						 data);

		if (convrc != NFS4_OK) {
			res_CREATE4->status = convrc;
			goto out;
		}
	}

	/* Create either a symbolic link or a directory */
	switch (arg_CREATE4->objtype.type) {
	case NF4LNK:
		/* Convert the name to link from into a regular string */
		type = SYMBOLIC_LINK;
		res_CREATE4->status = nfs4_utf8string2dynamic(
				&arg_CREATE4->objtype.createtype4_u.linkdata,
				UTF8_SCAN_SYMLINK,
				&link_content);

		if (res_CREATE4->status != NFS4_OK)
			goto out;
		break;

	case NF4DIR:
		/* Create a new directory */
		type = DIRECTORY;
		break;

	case NF4SOCK:
		/* Create a new socket file */
		type = SOCKET_FILE;
		break;

	case NF4FIFO:
		/* Create a new socket file */
		type = FIFO_FILE;
		break;

	case NF4CHR:
		/* Create a new socket file */
		type = CHARACTER_FILE;
		sattr.rawdev.major =
		    arg_CREATE4->objtype.createtype4_u.devdata.specdata1;
		sattr.rawdev.minor =
		    arg_CREATE4->objtype.createtype4_u.devdata.specdata2;
		sattr.valid_mask |= ATTR_RAWDEV;
		break;

	case NF4BLK:
		/* Create a new socket file */
		type = BLOCK_FILE;
		sattr.rawdev.major =
		    arg_CREATE4->objtype.createtype4_u.devdata.specdata1;
		sattr.rawdev.minor =
		    arg_CREATE4->objtype.createtype4_u.devdata.specdata2;
		sattr.valid_mask |= ATTR_RAWDEV;
		break;

	default:
		/* Should never happen, but return NFS4ERR_BADTYPE
		 *in this case
		 */
		res_CREATE4->status = NFS4ERR_BADTYPE;
		goto out;
	}			/* switch( arg_CREATE4.objtype.type ) */

	if (!(sattr.valid_mask & ATTR_MODE)) {
		/* Make sure mode is set. */
		if (type == DIRECTORY)
			sattr.mode = 0700;
		else
			sattr.mode = 0600;
		sattr.valid_mask |= ATTR_MODE;
	}

	fsal_status = fsal_create(obj_parent, name, type, &sattr, link_content,
				  &obj_new, NULL);

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&sattr);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_CREATE4->status = nfs4_Errno_status(fsal_status);
		goto out;
	}

	/* Building the new file handle to replace the current FH */
	if (!nfs4_FSALToFhandle(false, &data->currentFH, obj_new,
					op_ctx->ctx_export)) {
		res_CREATE4->status = NFS4ERR_SERVERFAULT;
		goto out;
	}

	/* Mark current_stateid as invalid */
	data->current_stateid_valid = false;

	/* Set the mode if requested */
	/* Use the same fattr mask for reply, if one attribute was not
	   settable, NFS4ERR_ATTRNOTSUPP was replyied */
	res_CREATE4->CREATE4res_u.resok4.attrset.bitmap4_len =
	    arg_CREATE4->createattrs.attrmask.bitmap4_len;

	if (arg_CREATE4->createattrs.attrmask.bitmap4_len != 0) {
		/* copy over bitmap */
		res_CREATE4->CREATE4res_u.resok4.attrset =
		    arg_CREATE4->createattrs.attrmask;
	}

	memset(&res_CREATE4->CREATE4res_u.resok4.cinfo.after,
	       0,
	       sizeof(changeid4));

	res_CREATE4->CREATE4res_u.resok4.cinfo.after =
		fsal_get_changeid4(obj_parent);

	/* Operation is supposed to be atomic .... */
	res_CREATE4->CREATE4res_u.resok4.cinfo.atomic = FALSE;

	LogFullDebug(COMPONENT_NFS_V4,
		     "CREATE CINFO before = %" PRIu64 "  after = %" PRIu64
		     "  atomic = %d",
		     res_CREATE4->CREATE4res_u.resok4.cinfo.before,
		     res_CREATE4->CREATE4res_u.resok4.cinfo.after,
		     res_CREATE4->CREATE4res_u.resok4.cinfo.atomic);

	/* @todo : BUGAZOMEU: fair ele free dans cette fonction */

	/* Keep the vnode entry for the file in the compound data */
	set_current_entry(data, obj_new);

	/* If you reach this point, then no error occured */
	res_CREATE4->status = NFS4_OK;

 out:

	if (obj_new) {
		/* Put our ref */
		obj_new->obj_ops->put_ref(obj_new);
	}

	gsh_free(name);
	gsh_free(link_content);

	return nfsstat4_to_nfs_req_result(res_CREATE4->status);
}				/* nfs4_op_create */

/**
 * @brief Free memory allocated for CREATE result
 *
 * This function frees any memory allocated for the result of the
 * NFS4_OP_CREATE operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_create_Free(nfs_resop4 *resp)
{
	/* Nothing to be done */
}
