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
 * @file  nfs3_setattr.c
 * @brief Everything you need for NFSv3 SETATTR
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include "hashtable.h"
#include "log.h"
#include "gsh_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_convert.h"
#include "nfs_proto_tools.h"
#include "sal_functions.h"

/**
 * @brief The NFSPROC3_SETATTR
 *
 * Implements the NFSPROC3_SETATTR function.
 *
 * @param[in]  arg     NFS arguments union
 * @param[in]  req     SVC request related to this call
 * @param[out] res     Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable
 *
 */

int nfs3_setattr(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	struct attrlist setattr;
	struct fsal_obj_handle *obj = NULL;
	pre_op_attr pre_attr = {
		.attributes_follow = false
	};
	fsal_status_t fsal_status = {0, 0};
	int rc = NFS_REQ_OK;
	SETATTR3resfail *resfail = &res->res_setattr3.SETATTR3res_u.resfail;
	SETATTR3resok *resok = &res->res_setattr3.SETATTR3res_u.resok;

	memset(&setattr, 0, sizeof(setattr));

	if (isDebug(COMPONENT_NFSPROTO)) {
		char str[LEN_FH_STR];

		nfs_FhandleToStr(req->rq_msg.cb_vers,
				 &arg->arg_setattr3.object,
				 NULL,
				 str);

		LogDebug(COMPONENT_NFSPROTO,
			 "REQUEST PROCESSING: Calling nfs_Setattr handle: %s",
			 str);
	}

	/* to avoid setting it on each error case */
	resfail->obj_wcc.before.attributes_follow = FALSE;
	resfail->obj_wcc.after.attributes_follow = FALSE;

	obj = nfs3_FhandleToCache(&arg->arg_setattr3.object,
				    &res->res_setattr3.status,
				    &rc);

	if (obj == NULL) {
		/* Status and rc have been set by nfs3_FhandleToCache */
		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs3_FhandleToCache failed");
		goto out;
	}

	nfs_SetPreOpAttr(obj, &pre_attr);

	if (arg->arg_setattr3.guard.check) {
		/* This pack of lines implements the "guard check" setattr. This
		 * feature of nfsv3 is used to avoid several setattr to occur
		 * concurently on the same object, from different clients
		 */
		nfstime3 *obj_ctime =
			&arg->arg_setattr3.guard.sattrguard3_u.obj_ctime;
		nfstime3 *pre_ctime = &pre_attr.pre_op_attr_u.attributes.ctime;

		LogFullDebug(COMPONENT_NFSPROTO, "css=%d acs=%d csn=%d acn=%d",
			     obj_ctime->tv_sec, pre_ctime->tv_sec,
			     obj_ctime->tv_nsec, pre_ctime->tv_nsec);

		if (obj_ctime->tv_sec != pre_ctime->tv_sec ||
		    obj_ctime->tv_nsec != pre_ctime->tv_nsec) {

			res->res_setattr3.status = NFS3ERR_NOT_SYNC;
			rc = NFS_REQ_OK;
			LogFullDebug(COMPONENT_NFSPROTO,
				     "guard check failed");
			goto out;
		}
	}

	/* Conversion to FSAL attributes */
	if (!nfs3_Sattr_To_FSALattr(&setattr,
				    &arg->arg_setattr3.new_attributes)) {
		res->res_setattr3.status = NFS3ERR_INVAL;
		rc = NFS_REQ_OK;
		LogFullDebug(COMPONENT_NFSPROTO,
			     "nfs3_Sattr_To_FSALattr failed");
		goto out;
	}

	if (setattr.valid_mask != 0) {
		/* If owner or owner_group are set, and the credential was
		 * squashed, then we must squash the set owner and owner_group.
		 */
		squash_setattr(&setattr);

		/* Don't allow attribute change while we are in grace period.
		 * Required for delegation reclaims and may be needed for other
		 * reclaimable states as well. No NFS4ERR_GRACE in NFS v3, so
		 * send jukebox error.
		 */
		if (!nfs_get_grace_status(false)) {
			res->res_setattr3.status = NFS3ERR_JUKEBOX;
			rc = NFS_REQ_OK;
			LogFullDebug(COMPONENT_NFSPROTO,
				     "nfs_in_grace is true");
			goto out;
		}

		/* For now we don't look for states, so indicate bypass so
		 * we will get through an NLM_SHARE with deny.
		 */
		fsal_status = fsal_setattr(obj, true, NULL, &setattr);
		nfs_put_grace_status();

		if (FSAL_IS_ERROR(fsal_status)) {
			res->res_setattr3.status =
				nfs3_Errno_status(fsal_status);
			LogFullDebug(COMPONENT_NFSPROTO,
				     "fsal_setattr failed");
			goto out_fail;
		}
	}

	/* Set the NFS return */
	/* Build Weak Cache Coherency data */
	res->res_setattr3.status = NFS3_OK;
	if (arg->arg_setattr3.new_attributes.size.set_it
	    && !(setattr.valid_mask ^ ATTR_SIZE)) {
		resfail->obj_wcc.before.attributes_follow = FALSE;
		resfail->obj_wcc.after.attributes_follow = FALSE;
	} else {
		nfs_SetWccData(&pre_attr, obj, &resok->obj_wcc);
	}

	rc = NFS_REQ_OK;

 out:

	/* Release the attributes (may release an inherited ACL) */
	fsal_release_attrs(&setattr);

	/* return references */
	if (obj)
		obj->obj_ops->put_ref(obj);

	LogDebug(COMPONENT_NFSPROTO,
		 "Result %s%s",
		 nfsstat3_to_str(res->res_setattr3.status),
		 rc == NFS_REQ_DROP ? " Dropping response" : "");

	return rc;

 out_fail:

	nfs_SetWccData(&pre_attr, obj, &resfail->obj_wcc);

	if (nfs_RetryableError(fsal_status.major)) {
		/* Drop retryable request. */
		rc = NFS_REQ_DROP;
	}

	goto out;
}				/* nfs3_setattr */

/**
 * @brief Free the result structure allocated for nfs3_setattr.
 *
 * This function frees the result structure allocated for nfs3_setattr.
 *
 * @param[in,out] res Result structure
 */

void nfs3_setattr_free(nfs_res_t *res)
{
	/* Nothing to do here */
}
