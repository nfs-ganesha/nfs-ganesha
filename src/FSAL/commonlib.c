// SPDX-License-Identifier: LGPL-3.0-or-later
/*
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

/**
 * @addtogroup FSAL
 * @{
 */

/**
 * @file fsal_commonlib.c
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Common functions for and private to FSAL modules.
 *
 * The prime requirement for functions to be here is that they operate only
 * on the public part of the FSAL api and are therefore sharable by all fsal
 * implementations.
 */
#include "config.h"

#include <misc/queue.h> /* avoid conflicts with sys/queue.h */
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <os/quota.h>

#include "common_utils.h"
#include "gsh_config.h"
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/access_check.h"
#include "fsal_private.h"
#include "fsal_convert.h"
#include "nfs4_acls.h"
#include "sal_data.h"
#include "nfs_init.h"
#include "mdcache.h"
#include "nfs_proto_tools.h"
#include "idmapper.h"
#include "pnfs_utils.h"

/* fsal_attach_export
 * called from the FSAL's create_export method with a reference on the fsal.
 */

int fsal_attach_export(struct fsal_module *fsal_hdl,
		       struct glist_head *obj_link)
{
	int retval = 0;

	if (atomic_fetch_int32_t(&fsal_hdl->refcount) > 0) {
		glist_add(&fsal_hdl->exports, obj_link);
	} else {
		LogCrit(COMPONENT_CONFIG,
			"Attaching export with out holding a reference!. hdl= = 0x%p",
			fsal_hdl);
		retval = EINVAL;
	}
	return retval;
}

/* fsal_detach_export
 * called by an export when it is releasing itself.
 * does not require a reference to be taken.  The list has
 * kept the fsal "busy".
 */

void fsal_detach_export(struct fsal_module *fsal_hdl,
			struct glist_head *obj_link)
{
	PTHREAD_RWLOCK_wrlock(&fsal_hdl->lock);
	glist_del(obj_link);
	PTHREAD_RWLOCK_unlock(&fsal_hdl->lock);
}

/**
 * @brief Initialize export ops vectors
 *
 * @param[in] exp Export handle
 *
 */

void fsal_export_init(struct fsal_export *exp)
{
	memcpy(&exp->exp_ops, &def_export_ops, sizeof(struct export_ops));
	exp->export_id = op_ctx->ctx_export->export_id;
	exp->owning_export = op_ctx->ctx_export;
	glist_init(&exp->filesystems);
}

/**
 * @brief Stack an export on top of another
 *
 * Set up export stacking for stackable FSALs
 *
 * @param[in] sub_export	Export being stacked on
 * @param[in] super_export	Export stacking on top
 * @return Return description
 */
void fsal_export_stack(struct fsal_export *sub_export,
		       struct fsal_export *super_export)
{
	sub_export->super_export = super_export;
	super_export->sub_export = sub_export;
}

/**
 * @brief Free export ops vectors
 *
 * Free the memory allocated by init_export_ops. Poison pointers.
 *
 * @param[in] exp_hdl Export handle
 *
 */

void free_export_ops(struct fsal_export *exp_hdl)
{
	memset(&exp_hdl->exp_ops, 0, sizeof(exp_hdl->exp_ops));	/* poison */
}

/* fsal_export to fsal_obj_handle helpers
 */

void fsal_default_obj_ops_init(struct fsal_obj_ops *obj_ops)
{
	*obj_ops = def_handle_ops;
}

void fsal_obj_handle_init(struct fsal_obj_handle *obj, struct fsal_export *exp,
			  object_file_type_t type)
{
	pthread_rwlockattr_t attrs;

	obj->fsal = exp->fsal;
	obj->type = type;
	pthread_rwlockattr_init(&attrs);
#ifdef GLIBC
	pthread_rwlockattr_setkind_np(
		&attrs,
		PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#endif
	PTHREAD_RWLOCK_init(&obj->obj_lock, &attrs);

	PTHREAD_RWLOCK_wrlock(&obj->fsal->lock);
	glist_add(&obj->fsal->handles, &obj->handles);
	PTHREAD_RWLOCK_unlock(&obj->fsal->lock);

	pthread_rwlockattr_destroy(&attrs);
}

void fsal_obj_handle_fini(struct fsal_obj_handle *obj)
{
	PTHREAD_RWLOCK_wrlock(&obj->fsal->lock);
	glist_del(&obj->handles);
	PTHREAD_RWLOCK_unlock(&obj->fsal->lock);
	PTHREAD_RWLOCK_destroy(&obj->obj_lock);
	memset(&obj->obj_ops, 0, sizeof(obj->obj_ops));	/* poison myself */
	obj->fsal = NULL;
}

/* fsal_module to fsal_pnfs_ds helpers
 */

void fsal_pnfs_ds_init(struct fsal_pnfs_ds *pds, struct fsal_module *fsal)
{
	pds->ds_refcount = 1;	/* we start out with a reference */
	fsal->m_ops.fsal_pnfs_ds_ops(&pds->s_ops);
	pds->fsal = fsal;
	fsal_get(fsal); /* Take a reference for the FSAL */
}

void fsal_pnfs_ds_fini(struct fsal_pnfs_ds *pds)
{
	PTHREAD_RWLOCK_wrlock(&pds->fsal->lock);
	glist_del(&pds->server);
	PTHREAD_RWLOCK_unlock(&pds->fsal->lock);

	memset(&pds->s_ops, 0, sizeof(pds->s_ops));	/* poison myself */
	if (pds->fsal != NULL) {
		fsal_put(pds->fsal);
		pds->fsal = NULL;
	}
}

/**
 * @brief FSAL error code to error message
 *
 * @param[in] fsal_err Error code
 *
 * @return Error message, empty string if not found.
 */

const char *msg_fsal_err(fsal_errors_t fsal_err)
{
	switch (fsal_err) {
	case ERR_FSAL_NO_ERROR:
		return "No error";
	case ERR_FSAL_PERM:
		return "Forbidden action";
	case ERR_FSAL_NOENT:
		return "No such file or directory";
	case ERR_FSAL_IO:
		return "I/O error";
	case ERR_FSAL_NXIO:
		return "No such device or address";
	case ERR_FSAL_NOMEM:
		return "Not enough memory";
	case ERR_FSAL_ACCESS:
		return "Permission denied";
	case ERR_FSAL_FAULT:
		return "Bad address";
	case ERR_FSAL_STILL_IN_USE:
		return "Device or resource busy";
	case ERR_FSAL_EXIST:
		return "This object already exists";
	case ERR_FSAL_XDEV:
		return "This operation can't cross filesystems";
	case ERR_FSAL_NOTDIR:
		return "This object is not a directory";
	case ERR_FSAL_ISDIR:
		return "Directory used in a nondirectory operation";
	case ERR_FSAL_INVAL:
		return "Invalid object type";
	case ERR_FSAL_FBIG:
		return "File exceeds max file size";
	case ERR_FSAL_NOSPC:
		return "No space left on filesystem";
	case ERR_FSAL_ROFS:
		return "Read-only filesystem";
	case ERR_FSAL_MLINK:
		return "Too many hard links";
	case ERR_FSAL_DQUOT:
		return "Quota exceeded";
	case ERR_FSAL_NAMETOOLONG:
		return "Max name length exceeded";
	case ERR_FSAL_NOTEMPTY:
		return "The directory is not empty";
	case ERR_FSAL_STALE:
		return "The file no longer exists";
	case ERR_FSAL_BADHANDLE:
		return "Illegal filehandle";
	case ERR_FSAL_BADCOOKIE:
		return "Invalid cookie";
	case ERR_FSAL_NOTSUPP:
		return "Operation not supported";
	case ERR_FSAL_TOOSMALL:
		return "Output buffer too small";
	case ERR_FSAL_SERVERFAULT:
		return "Undefined server error";
	case ERR_FSAL_BADTYPE:
		return "Invalid type for create operation";
	case ERR_FSAL_DELAY:
		return "File busy, retry";
	case ERR_FSAL_FHEXPIRED:
		return "Filehandle expired";
	case ERR_FSAL_SYMLINK:
		return "This is a symbolic link, should be file/directory";
	case ERR_FSAL_ATTRNOTSUPP:
		return "Attribute not supported";
	case ERR_FSAL_NOT_INIT:
		return "Filesystem not initialized";
	case ERR_FSAL_ALREADY_INIT:
		return "Filesystem already initialised";
	case ERR_FSAL_BAD_INIT:
		return "Filesystem initialisation error";
	case ERR_FSAL_SEC:
		return "Security context error";
	case ERR_FSAL_NO_QUOTA:
		return "No Quota available";
	case ERR_FSAL_NOT_OPENED:
		return "File/directory not opened";
	case ERR_FSAL_DEADLOCK:
		return "Deadlock";
	case ERR_FSAL_OVERFLOW:
		return "Overflow";
	case ERR_FSAL_INTERRUPT:
		return "Operation Interrupted";
	case ERR_FSAL_BLOCKED:
		return "Lock Blocked";
	case ERR_FSAL_SHARE_DENIED:
		return "Share Denied";
	case ERR_FSAL_LOCKED:
		return "Locked";
	case ERR_FSAL_TIMEOUT:
		return "Timeout";
	case ERR_FSAL_FILE_OPEN:
		return "File Open";
	case ERR_FSAL_UNION_NOTSUPP:
		return "Union Not Supported";
	case ERR_FSAL_IN_GRACE:
		return "Server in Grace";
	case ERR_FSAL_NO_DATA:
		return "No Data";
	case ERR_FSAL_NO_ACE:
		return "No matching ACE";
	case ERR_FSAL_BAD_RANGE:
		return "Lock not in allowable range";
	case ERR_FSAL_CROSS_JUNCTION:
		return "Crossed Junction";
	case ERR_FSAL_BADNAME:
		return "Invalid Name";
	case ERR_FSAL_NOXATTR:
		return "No such xattr";
	case ERR_FSAL_XATTR2BIG:
		return "Xattr too big";
	}

	return "Unknown FSAL error";
}

const char *fsal_dir_result_str(enum fsal_dir_result result)
{
	switch (result) {
	case DIR_CONTINUE:
		return "DIR_CONTINUE";

	case DIR_READAHEAD:
		return "DIR_READAHEAD";

	case DIR_TERMINATE:
		return "DIR_TERMINATE";
	}

	return "<unknown>";
}

/**
 * @brief Dump and fsal_staticfsinfo_t to a log
 *
 * This is used for debugging
 *
 * @param[in] info The info to dump
 */
void display_fsinfo(struct fsal_module *fsal)
{
	LogDebug(COMPONENT_FSAL, "FileSystem info for FSAL %s {", fsal->name);
	LogDebug(COMPONENT_FSAL, "  maxfilesize  = %" PRIX64 "    ",
		 (uint64_t) fsal->fs_info.maxfilesize);
	LogDebug(COMPONENT_FSAL, "  maxlink  = %" PRIu32,
		fsal->fs_info.maxlink);
	LogDebug(COMPONENT_FSAL, "  maxnamelen  = %" PRIu32,
		fsal->fs_info.maxnamelen);
	LogDebug(COMPONENT_FSAL, "  maxpathlen  = %" PRIu32,
		fsal->fs_info.maxpathlen);
	LogDebug(COMPONENT_FSAL, "  no_trunc  = %d ",
		fsal->fs_info.no_trunc);
	LogDebug(COMPONENT_FSAL, "  chown_restricted  = %d ",
		 fsal->fs_info.chown_restricted);
	LogDebug(COMPONENT_FSAL, "  case_insensitive  = %d ",
		 fsal->fs_info.case_insensitive);
	LogDebug(COMPONENT_FSAL, "  case_preserving  = %d ",
		 fsal->fs_info.case_preserving);
	LogDebug(COMPONENT_FSAL, "  link_support  = %d  ",
		fsal->fs_info.link_support);
	LogDebug(COMPONENT_FSAL, "  symlink_support  = %d  ",
		 fsal->fs_info.symlink_support);
	LogDebug(COMPONENT_FSAL, "  lock_support  = %d  ",
		fsal->fs_info.lock_support);
	LogDebug(COMPONENT_FSAL, "  lock_support_async_block  = %d  ",
		 fsal->fs_info.lock_support_async_block);
	LogDebug(COMPONENT_FSAL, "  named_attr  = %d  ",
		fsal->fs_info.named_attr);
	LogDebug(COMPONENT_FSAL, "  unique_handles  = %d  ",
		 fsal->fs_info.unique_handles);
	LogDebug(COMPONENT_FSAL, "  acl_support  = %hu  ",
		fsal->fs_info.acl_support);
	LogDebug(COMPONENT_FSAL, "  cansettime  = %d  ",
		fsal->fs_info.cansettime);
	LogDebug(COMPONENT_FSAL, "  homogenous  = %d  ",
		fsal->fs_info.homogenous);
	LogDebug(COMPONENT_FSAL, "  supported_attrs  = %" PRIX64,
		 fsal->fs_info.supported_attrs);
	LogDebug(COMPONENT_FSAL, "  maxread  = %" PRIu64,
		fsal->fs_info.maxread);
	LogDebug(COMPONENT_FSAL, "  maxwrite  = %" PRIu64,
		fsal->fs_info.maxwrite);
	LogDebug(COMPONENT_FSAL, "  umask  = %X ",
		fsal->fs_info.umask);
	LogDebug(COMPONENT_FSAL, "  auth_exportpath_xdev  = %d  ",
		 fsal->fs_info.auth_exportpath_xdev);
	LogDebug(COMPONENT_FSAL, "  delegations = %d  ",
		 fsal->fs_info.delegations);
	LogDebug(COMPONENT_FSAL, "  pnfs_mds = %d  ",
		 fsal->fs_info.pnfs_mds);
	LogDebug(COMPONENT_FSAL, "  pnfs_ds = %d  ",
		 fsal->fs_info.pnfs_ds);
	LogDebug(COMPONENT_FSAL, "  fsal_trace = %d  ",
		 fsal->fs_info.fsal_trace);
	LogDebug(COMPONENT_FSAL, "  fsal_grace = %d  ",
		 fsal->fs_info.fsal_grace);
	LogDebug(COMPONENT_FSAL, "  expire_time_parent = %d  ",
		 fsal->fs_info.expire_time_parent);
	LogDebug(COMPONENT_FSAL, "  xattr_support = %d  ",
		 fsal->fs_info.xattr_support);
	LogDebug(COMPONENT_FSAL, "}");
}

int display_attrlist(struct display_buffer *dspbuf,
		     struct fsal_attrlist *attr, bool is_obj)
{
	int b_left = display_start(dspbuf);

	if (attr->request_mask == 0 && attr->valid_mask == 0 &&
	    attr->supported == 0)
		return display_cat(dspbuf, "No attributes");

	if (b_left > 0 && attr->request_mask != 0)
		b_left = display_printf(dspbuf, "Request Mask=%08x ",
					(unsigned int) attr->request_mask);

	if (b_left > 0 && attr->valid_mask != 0)
		b_left = display_printf(dspbuf, "Valid Mask=%08x ",
					(unsigned int) attr->valid_mask);

	if (b_left > 0 && attr->supported != 0)
		b_left = display_printf(dspbuf, "Supported Mask=%08x ",
					(unsigned int) attr->supported);

	if (b_left > 0 && is_obj)
		b_left = display_printf(dspbuf, "%s",
					object_file_type_to_str(attr->type));

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_NUMLINKS))
		b_left = display_printf(dspbuf, " numlinks=0x%"PRIx32,
					attr->numlinks);

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_SIZE))
		b_left = display_printf(dspbuf, " size=0x%"PRIx64,
					attr->filesize);

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_MODE))
		b_left = display_printf(dspbuf, " mode=0%"PRIo32,
					attr->mode);

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_OWNER))
		b_left = display_printf(dspbuf, " owner=0x%"PRIx64,
					attr->owner);

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_GROUP))
		b_left = display_printf(dspbuf, " group=0x%"PRIx64,
					attr->group);

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME_SERVER))
		b_left = display_cat(dspbuf, " atime=SERVER");

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME_SERVER))
		b_left = display_cat(dspbuf, " mtime=SERVER");

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_ATIME)) {
		b_left = display_cat(dspbuf, " atime=");
		if (b_left > 0)
			b_left = display_timespec(dspbuf, &attr->atime);
	}

	if (b_left > 0 && FSAL_TEST_MASK(attr->valid_mask, ATTR_MTIME)) {
		b_left = display_cat(dspbuf, " mtime=");
		if (b_left > 0)
			b_left = display_timespec(dspbuf, &attr->mtime);
	}

	return b_left;
}

void log_attrlist(log_components_t component, log_levels_t level,
		  const char *reason, struct fsal_attrlist *attr, bool is_obj,
		  char *file, int line, char *function)
{
	char str[LOG_BUFF_LEN] = "\0";
	struct display_buffer dspbuf = {sizeof(str), str, str};

	(void) display_attrlist(&dspbuf, attr, is_obj);

	DisplayLogComponentLevel(component, file, line, function, level,
		"%s %s attributes %s",
		reason,
		is_obj ? "obj" : "set",
		str);
}

int encode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type)
{
	uint32_t u32;

	if (sizeof_fsid(fsid_type) > max)
		return -1;

	/* Pack fsid into the bytes */
	switch (fsid_type) {
	case FSID_NO_TYPE:
		break;

	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		memcpy(buf,
		       &fsid->major,
		       sizeof(fsid->major));
		break;

	case FSID_TWO_UINT64:
		memcpy(buf,
		       fsid,
		       sizeof(*fsid));
		break;

	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		u32 = fsid->major;
		memcpy(buf,
		       &u32,
		       sizeof(u32));
		u32 = fsid->minor;
		memcpy(buf + sizeof(u32),
		       &u32,
		       sizeof(u32));
	}

	return sizeof_fsid(fsid_type);
}

int decode_fsid(char *buf,
		int max,
		struct fsal_fsid__ *fsid,
		enum fsid_type fsid_type)
{
	uint32_t u32;

	if (sizeof_fsid(fsid_type) > max)
		return -1;

	switch (fsid_type) {
	case FSID_NO_TYPE:
		memset(fsid, 0, sizeof(*fsid));
		break;

	case FSID_ONE_UINT64:
	case FSID_MAJOR_64:
		memcpy(&fsid->major,
		       buf,
		       sizeof(fsid->major));
		fsid->minor = 0;
		break;

	case FSID_TWO_UINT64:
		memcpy(fsid,
		       buf,
		       sizeof(*fsid));
		break;

	case FSID_TWO_UINT32:
	case FSID_DEVICE:
		memcpy(&u32,
		       buf,
		       sizeof(u32));
		fsid->major = u32;
		memcpy(&u32,
		       buf + sizeof(u32),
		       sizeof(u32));
		fsid->minor = u32;
		break;
	}

	return sizeof_fsid(fsid_type);
}


static inline bool is_dup_ace(fsal_ace_t *ace, fsal_aceflag_t inherit)
{
	if (!IS_FSAL_ACE_INHERIT(*ace))
		return false;
	if (inherit != FSAL_ACE_FLAG_DIR_INHERIT)
		/* Only dup on directories */
		return false;
	if (IS_FSAL_ACE_NO_PROPAGATE(*ace))
		return false;
	if (IS_FSAL_ACE_FILE_INHERIT(*ace) && !IS_FSAL_ACE_DIR_INHERIT(*ace))
		return false;
	if (!IS_FSAL_ACE_PERM(*ace))
		return false;

	return true;
}

static fsal_errors_t dup_ace(fsal_ace_t *sace, fsal_ace_t *dace)
{
	*dace = *sace;

	GET_FSAL_ACE_FLAG(*sace) |= FSAL_ACE_FLAG_INHERIT_ONLY;

	GET_FSAL_ACE_FLAG(*dace) &= ~(FSAL_ACE_FLAG_INHERIT |
				      FSAL_ACE_FLAG_NO_PROPAGATE);

	return ERR_FSAL_NO_ERROR;
}

fsal_errors_t fsal_inherit_acls(struct fsal_attrlist *attrs, fsal_acl_t *sacl,
				fsal_aceflag_t inherit)
{
	int naces;
	fsal_ace_t *sace, *dace;

	if (!sacl || !sacl->aces || sacl->naces == 0)
		return ERR_FSAL_NO_ERROR;

	if (attrs->acl && attrs->acl->aces && attrs->acl->naces > 0)
		return ERR_FSAL_EXIST;

	naces = 0;
	for (sace = sacl->aces; sace < sacl->aces + sacl->naces; sace++) {
		if (IS_FSAL_ACE_FLAG(*sace, inherit))
			naces++;
		if (is_dup_ace(sace, inherit))
			naces++;
	}

	if (naces == 0)
		return ERR_FSAL_NO_ERROR;

	if (attrs->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrs->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);
	}

	attrs->acl = nfs4_acl_alloc();
	attrs->acl->aces = (fsal_ace_t *) nfs4_ace_alloc(naces);
	dace = attrs->acl->aces;

	for (sace = sacl->aces; sace < sacl->aces + sacl->naces; sace++) {
		if (IS_FSAL_ACE_FLAG(*sace, inherit)) {
			*dace = *sace;
			if (IS_FSAL_ACE_NO_PROPAGATE(*dace))
				GET_FSAL_ACE_FLAG(*dace) &=
					~(FSAL_ACE_FLAG_INHERIT |
					  FSAL_ACE_FLAG_NO_PROPAGATE);
			else if (inherit == FSAL_ACE_FLAG_DIR_INHERIT &&
				 IS_FSAL_ACE_FILE_INHERIT(*dace) &&
				 !IS_FSAL_ACE_DIR_INHERIT(*dace))
				GET_FSAL_ACE_FLAG(*dace) |=
					FSAL_ACE_FLAG_NO_PROPAGATE;
			else if (is_dup_ace(dace, inherit)) {
				dup_ace(dace, dace + 1);
				dace++;
			}
			dace++;
		}
	}
	attrs->acl->naces = naces;
	FSAL_SET_MASK(attrs->valid_mask, ATTR_ACL);

	return ERR_FSAL_NO_ERROR;
}

fsal_status_t fsal_remove_access(struct fsal_obj_handle *dir_hdl,
				 struct fsal_obj_handle *rem_hdl,
				 bool isdir)
{
	fsal_status_t fsal_status = { 0, 0 };
	fsal_status_t del_status = { 0, 0 };

	/* draft-ietf-nfsv4-acls section 12 */
	/* If no execute on dir, deny */
	fsal_status = dir_hdl->obj_ops->test_access(
				dir_hdl,
				FSAL_MODE_MASK_SET(FSAL_X_OK) |
				FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE),
				NULL, NULL, false);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogFullDebug(COMPONENT_FSAL,
			 "Could not delete: No execute permession on parent: %s",
			 msg_fsal_err(fsal_status.major));
		return fsal_status;
	}

	/* We can delete if we have *either* ACE_PERM_DELETE or
	 * ACE_PERM_DELETE_CHILD.  7530 - 6.2.1.3.2 */
	del_status = rem_hdl->obj_ops->test_access(
				rem_hdl,
				FSAL_MODE_MASK_SET(FSAL_W_OK) |
				FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE) |
				FSAL_ACE4_REQ_FLAG,
				NULL, NULL, false);
	fsal_status = dir_hdl->obj_ops->test_access(
				dir_hdl,
				FSAL_MODE_MASK_SET(FSAL_W_OK) |
				FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD) |
				FSAL_ACE4_REQ_FLAG,
				NULL, NULL, false);
	if (FSAL_IS_ERROR(fsal_status) && FSAL_IS_ERROR(del_status)) {
		/* Neither was explicitly allowed */
		if (fsal_status.major != ERR_FSAL_NO_ACE) {
			/* Explicitly denied */
			LogFullDebug(COMPONENT_FSAL,
				 "Could not delete (DELETE_CHILD) %s",
				 msg_fsal_err(fsal_status.major));
			return fsal_status;
		}
		if (del_status.major != ERR_FSAL_NO_ACE) {
			/* Explicitly denied */
			LogFullDebug(COMPONENT_FSAL,
				 "Could not delete (DELETE) %s",
				 msg_fsal_err(del_status.major));
			return del_status;
		}

		/* Neither ACE_PERM_DELETE nor ACE_PERM_DELETE_CHILD are set.
		 * Check for ADD_FILE in parent */
		fsal_status = dir_hdl->obj_ops->test_access(
				dir_hdl,
				FSAL_MODE_MASK_SET(FSAL_W_OK) |
				FSAL_ACE4_MASK_SET(isdir ?
					   FSAL_ACE_PERM_ADD_SUBDIRECTORY
					   : FSAL_ACE_PERM_ADD_FILE),
				NULL, NULL, false);

		if (FSAL_IS_ERROR(fsal_status)) {
			LogFullDebug(COMPONENT_FSAL,
				 "Could not delete (ADD_CHILD) %s",
				 msg_fsal_err(fsal_status.major));
			return fsal_status;
		}
		/* Allowed; fall through */
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t fsal_rename_access(struct fsal_obj_handle *src_dir_hdl,
				 struct fsal_obj_handle *src_obj_hdl,
				 struct fsal_obj_handle *dst_dir_hdl,
				 struct fsal_obj_handle *dst_obj_hdl,
				 bool isdir)
{
	fsal_status_t status = {0, 0};
	fsal_accessflags_t access_type;

	status = fsal_remove_access(src_dir_hdl, src_obj_hdl, isdir);
	if (FSAL_IS_ERROR(status))
		return status;

	if (dst_obj_hdl) {
		status = fsal_remove_access(dst_dir_hdl, dst_obj_hdl, isdir);
		if (FSAL_IS_ERROR(status))
			return status;
	}

	access_type = FSAL_MODE_MASK_SET(FSAL_W_OK);
	if (isdir)
		access_type |=
			FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_SUBDIRECTORY);
	else
		access_type |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE);
	status = dst_dir_hdl->obj_ops->test_access(dst_dir_hdl, access_type,
						  NULL, NULL, false);
	if (FSAL_IS_ERROR(status))
		return status;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
fsal_mode_set_ace(fsal_ace_t *deny, fsal_ace_t *allow, uint32_t mode)
{
	GET_FSAL_ACE_TYPE(*allow) = FSAL_ACE_TYPE_ALLOW;
	GET_FSAL_ACE_TYPE(*deny) = FSAL_ACE_TYPE_DENY;

	if (mode & S_IRUSR)
		GET_FSAL_ACE_PERM(*allow) |= FSAL_ACE_PERM_READ_DATA;
	else
		GET_FSAL_ACE_PERM(*deny) |= FSAL_ACE_PERM_READ_DATA;
	if (mode & S_IWUSR)
		GET_FSAL_ACE_PERM(*allow) |=
			FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA;
	else
		GET_FSAL_ACE_PERM(*deny) |=
			FSAL_ACE_PERM_WRITE_DATA | FSAL_ACE_PERM_APPEND_DATA;
	if (mode & S_IXUSR)
		GET_FSAL_ACE_PERM(*allow) |= FSAL_ACE_PERM_EXECUTE;
	else
		GET_FSAL_ACE_PERM(*deny) |= FSAL_ACE_PERM_EXECUTE;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
fsal_mode_gen_set(fsal_ace_t *ace, uint32_t mode)
{
	fsal_ace_t *allow, *deny;

	/* @OWNER */
	deny = ace;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_OWNER;
	GET_FSAL_ACE_IFLAG(*allow) |= (FSAL_ACE_IFLAG_MODE_GEN |
				       FSAL_ACE_IFLAG_SPECIAL_ID);
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_OWNER;
	GET_FSAL_ACE_IFLAG(*deny) |= (FSAL_ACE_IFLAG_MODE_GEN |
				      FSAL_ACE_IFLAG_SPECIAL_ID);
	fsal_mode_set_ace(deny, allow, mode & S_IRWXU);
	/* @GROUP */
	deny += 2;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_GROUP;
	GET_FSAL_ACE_IFLAG(*allow) |= (FSAL_ACE_IFLAG_MODE_GEN |
				       FSAL_ACE_IFLAG_SPECIAL_ID);
	GET_FSAL_ACE_FLAG(*allow) = FSAL_ACE_FLAG_GROUP_ID;
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_GROUP;
	GET_FSAL_ACE_IFLAG(*deny) |= (FSAL_ACE_IFLAG_MODE_GEN |
				      FSAL_ACE_IFLAG_SPECIAL_ID);
	GET_FSAL_ACE_FLAG(*deny) = FSAL_ACE_FLAG_GROUP_ID;
	fsal_mode_set_ace(deny, allow, (mode & S_IRWXG) << 3);
	/* @EVERYONE */
	deny += 2;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_EVERYONE;
	GET_FSAL_ACE_IFLAG(*allow) |= (FSAL_ACE_IFLAG_MODE_GEN |
				       FSAL_ACE_IFLAG_SPECIAL_ID);
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_EVERYONE;
	GET_FSAL_ACE_IFLAG(*deny) |= (FSAL_ACE_IFLAG_MODE_GEN |
				      FSAL_ACE_IFLAG_SPECIAL_ID);
	fsal_mode_set_ace(deny, allow, (mode & S_IRWXO) << 6);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
fsal_mode_gen_acl(struct fsal_attrlist *attrs)
{
	if (attrs->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrs->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);
	}

	attrs->acl = nfs4_acl_alloc();
	attrs->acl->naces = 6;
	attrs->acl->aces = (fsal_ace_t *) nfs4_ace_alloc(attrs->acl->naces);

	fsal_mode_gen_set(attrs->acl->aces, attrs->mode);

	FSAL_SET_MASK(attrs->valid_mask, ATTR_ACL);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t fsal_mode_to_acl(struct fsal_attrlist *attrs, fsal_acl_t *sacl)
{
	int naces;
	fsal_ace_t *sace, *dace;

	if (!FSAL_TEST_MASK(attrs->valid_mask, ATTR_MODE))
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (!sacl || sacl->naces == 0)
		return fsal_mode_gen_acl(attrs);

	naces = 0;
	for (sace = sacl->aces; sace < sacl->aces + sacl->naces; sace++) {
		if (IS_FSAL_ACE_MODE_GEN(*sace)) {
			/* Don't copy mode geneated ACEs; will be re-created */
			continue;
		}

		naces++;
		if (IS_FSAL_ACE_INHERIT_ONLY(*sace))
			continue;
		if (!IS_FSAL_ACE_PERM(*sace))
			continue;
		if (IS_FSAL_ACE_INHERIT(*sace)) {
			/* Dup this ACE */
			naces++;
		}
		/* XXX dang dup for non-special case */
	}

	if (naces == 0) {
		/* Only mode generate aces */
		return fsal_mode_gen_acl(attrs);
	}

	/* Space for generated ACEs at the end */
	naces += 6;

	if (attrs->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrs->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);
	}

	attrs->acl = nfs4_acl_alloc();
	attrs->acl->aces = (fsal_ace_t *) nfs4_ace_alloc(naces);
	attrs->acl->naces = 0;
	dace = attrs->acl->aces;

	for (sace = sacl->aces; sace < sacl->aces + sacl->naces;
	     sace++, dace++) {
		if (IS_FSAL_ACE_MODE_GEN(*sace))
			continue;

		*dace = *sace;
		attrs->acl->naces++;

		if (IS_FSAL_ACE_INHERIT_ONLY(*dace) ||
		    (!IS_FSAL_ACE_PERM(*dace)))
			continue;

		if (IS_FSAL_ACE_INHERIT(*dace)) {
			/* Need to duplicate */
			GET_FSAL_ACE_FLAG(*dace) |= FSAL_ACE_FLAG_INHERIT_ONLY;
			dace++;
			*dace = *sace;
			attrs->acl->naces++;
			GET_FSAL_ACE_FLAG(*dace) &= ~(FSAL_ACE_FLAG_INHERIT);
		}

		if (IS_FSAL_ACE_SPECIAL(*dace)) {
			GET_FSAL_ACE_PERM(*dace) &=
				~(FSAL_ACE_PERM_READ_DATA |
				  FSAL_ACE_PERM_LIST_DIR |
				  FSAL_ACE_PERM_WRITE_DATA |
				  FSAL_ACE_PERM_ADD_FILE |
				  FSAL_ACE_PERM_APPEND_DATA |
				  FSAL_ACE_PERM_ADD_SUBDIRECTORY |
				  FSAL_ACE_PERM_EXECUTE);
		} else {
			/* Do non-special stuff */
		}
	}

	if (naces - attrs->acl->naces != 6) {
		LogDebug(COMPONENT_FSAL, "Bad naces: %d not %d",
			 attrs->acl->naces, naces - 6);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	fsal_mode_gen_set(dace, attrs->mode);

	attrs->acl->naces = naces;
	FSAL_SET_MASK(attrs->valid_mask, ATTR_ACL);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* fsal_acl_to_mode helpers
 */

static uint32_t ace_modes[3][3] = {
	{ /* owner */
		S_IRUSR, S_IWUSR, S_IXUSR
	},
	{ /* group */
		S_IRGRP, S_IWGRP, S_IXGRP
	},
	{ /* everyone */
		S_IRUSR | S_IRGRP | S_IROTH,
		S_IWUSR | S_IWGRP | S_IWOTH,
		S_IXUSR | S_IXGRP | S_IXOTH,
	}
};

static inline void set_mode(struct fsal_attrlist *attrs, uint32_t mode,
			    bool allow)
{
	if (allow)
		attrs->mode |= mode;
	else
		attrs->mode &= ~(mode);
}

fsal_status_t fsal_acl_to_mode(struct fsal_attrlist *attrs)
{
	fsal_ace_t *ace = NULL;
	uint32_t *modes;

	if (!FSAL_TEST_MASK(attrs->valid_mask, ATTR_ACL))
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	if (!attrs->acl || attrs->acl->naces == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	for (ace = attrs->acl->aces; ace < attrs->acl->aces + attrs->acl->naces;
	     ace++) {
		if (IS_FSAL_ACE_SPECIAL_OWNER(*ace))
			modes = ace_modes[0];
		else if (IS_FSAL_ACE_SPECIAL_GROUP(*ace))
			modes = ace_modes[1];
		else if (IS_FSAL_ACE_SPECIAL_EVERYONE(*ace))
			modes = ace_modes[2];
		else
			continue;

		if (IS_FSAL_ACE_READ_DATA(*ace))
			set_mode(attrs, modes[0], IS_FSAL_ACE_ALLOW(*ace));
		if (IS_FSAL_ACE_WRITE_DATA(*ace) ||
		    IS_FSAL_ACE_APPEND_DATA(*ace))
			set_mode(attrs, modes[1], IS_FSAL_ACE_ALLOW(*ace));
		if (IS_FSAL_ACE_EXECUTE(*ace))
			set_mode(attrs, modes[2], IS_FSAL_ACE_ALLOW(*ace));

	}

	FSAL_SET_MASK(attrs->valid_mask, ATTR_MODE);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set up a common verifier using atime and mtime.
 *
 * @param[in] attrs        Attributes for the file
 * @param[in] verifier     Verifier to use for exclusive create
 * @param[in] trunc_verif  Use onlu 31 bits of each half of the verifier
 *
 * @retval true if verifier matches
 */

void set_common_verifier(struct fsal_attrlist *attrs,
			 fsal_verifier_t verifier,
			 bool trunc_verif)
{
	uint32_t verf_hi = 0, verf_lo = 0;

	memcpy(&verf_hi,
	       verifier,
	       sizeof(uint32_t));
	memcpy(&verf_lo,
	       verifier + sizeof(uint32_t),
	       sizeof(uint32_t));

	LogFullDebug(COMPONENT_FSAL,
		     "Passed verifier %"PRIx32" %"PRIx32,
		     verf_hi, verf_lo);

	if (trunc_verif) {
		verf_hi &= INT32_MAX;
		verf_lo &= INT32_MAX;
	}

	if (isDebug(COMPONENT_FSAL) &&
	    (FSAL_TEST_MASK(attrs->valid_mask, ATTR_ATIME) ||
	    (FSAL_TEST_MASK(attrs->valid_mask, ATTR_MTIME)))) {
		LogWarn(COMPONENT_FSAL,
			"atime or mtime was already set in attributes%"
			PRIx32" %"PRIx32,
			(uint32_t) attrs->atime.tv_sec,
			(uint32_t) attrs->mtime.tv_sec);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Setting verifier atime %"PRIx32" mtime %"PRIx32,
		     verf_hi, verf_lo);

	attrs->atime.tv_sec = verf_hi;
	attrs->atime.tv_nsec = 0;
	attrs->mtime.tv_sec = verf_lo;
	attrs->mtime.tv_nsec = 0;

	FSAL_SET_MASK(attrs->valid_mask, ATTR_ATIME | ATTR_MTIME);
}

/**
 * @brief Update the ref counter of share state
 *
 * The caller is responsible for protecting the share.
 *
 * @param[in] share         Share to update
 * @param[in] old_openflags Previous access/deny mode
 * @param[in] new_openflags Current access/deny mode
 */

void update_share_counters(struct fsal_share *share,
			   fsal_openflags_t old_openflags,
			   fsal_openflags_t new_openflags)
{
	int access_read_inc =
		((int)(new_openflags & FSAL_O_READ) != 0) -
		((int)(old_openflags & FSAL_O_READ) != 0);

	int access_write_inc =
		((int)(new_openflags & FSAL_O_WRITE) != 0) -
		((int)(old_openflags & FSAL_O_WRITE) != 0);

	int deny_read_inc =
		((int)(new_openflags & FSAL_O_DENY_READ) != 0) -
		((int)(old_openflags & FSAL_O_DENY_READ) != 0);

	/* Combine both FSAL_O_DENY_WRITE and FSAL_O_DENY_WRITE_MAND */
	int deny_write_inc =
		((int)(new_openflags & FSAL_O_DENY_WRITE) != 0) -
		((int)(old_openflags & FSAL_O_DENY_WRITE) != 0) +
		((int)(new_openflags & FSAL_O_DENY_WRITE_MAND) != 0) -
		((int)(old_openflags & FSAL_O_DENY_WRITE_MAND) != 0);

	int deny_write_mand_inc =
		((int)(new_openflags & FSAL_O_DENY_WRITE_MAND) != 0) -
		((int)(old_openflags & FSAL_O_DENY_WRITE_MAND) != 0);

	share->share_access_read += access_read_inc;
	share->share_access_write += access_write_inc;
	share->share_deny_read += deny_read_inc;
	share->share_deny_write += deny_write_inc;
	share->share_deny_write_mand += deny_write_mand_inc;

	LogFullDebug(COMPONENT_FSAL,
		     "share counter: access_read %u, access_write %u, deny_read %u, deny_write %u, deny_write_v4 %u",
		     share->share_access_read,
		     share->share_access_write,
		     share->share_deny_read,
		     share->share_deny_write,
		     share->share_deny_write_mand);
}

/**
 * @brief Check for share conflict
 *
 * The caller is responsible for protecting the share.
 *
 * This function is NOT called if the caller holds a share reservation covering
 * the requested access.
 *
 * @param[in] share        File to query
 * @param[in] openflags    Desired access and deny mode
 * @param[in] bypass       Bypasses share_deny_read and share_deny_write but
 *                         not share_deny_write_mand
 *
 * @retval ERR_FSAL_SHARE_DENIED - a conflict occurred.
 *
 */

fsal_status_t check_share_conflict(struct fsal_share *share,
				   fsal_openflags_t openflags,
				   bool bypass)
{
	char *cause = "";

	if ((openflags & FSAL_O_READ) != 0
	    && share->share_deny_read > 0
	    && !bypass) {
		cause = "access read denied by existing deny read";
		goto out_conflict;
	}

	if ((openflags & FSAL_O_WRITE) != 0
	    && (share->share_deny_write_mand > 0 ||
		(!bypass && share->share_deny_write > 0))) {
		cause = "access write denied by existing deny write";
		goto out_conflict;
	}

	if ((openflags & FSAL_O_DENY_READ) != 0
	    && share->share_access_read > 0) {
		cause = "deny read denied by existing access read";
		goto out_conflict;
	}

	if (((openflags & FSAL_O_DENY_WRITE) != 0 ||
	     (openflags & FSAL_O_DENY_WRITE_MAND) != 0)
	    && share->share_access_write > 0) {
		cause = "deny write denied by existing access write";
		goto out_conflict;
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 out_conflict:

	LogDebugAlt(COMPONENT_STATE, COMPONENT_FSAL,
		    "Share conflict detected: %s openflags=%d bypass=%s",
		    cause, (int) openflags,
		    bypass ? "yes" : "no");

	LogFullDebugAlt(COMPONENT_STATE, COMPONENT_FSAL,
			"share->share_deny_read=%d share->share_deny_write=%d share->share_access_read=%d share->share_access_write=%d",
			share->share_deny_read, share->share_deny_write,
			share->share_access_read, share->share_access_write);

	return fsalstat(ERR_FSAL_SHARE_DENIED, 0);
}

/**
 * @brief Check two shares for conflict and merge.
 *
 * The caller is responsible for protecting the share.
 *
 * When two object handles are merged that both contain shares, we must
 * check if the duplicate has a share conflict with the original. If
 * so, we will return ERR_FSAL_SHARE_DENIED.
 *
 * NOTE: dupe_share should belong to a fsal_obj_handle that has just been
 *       created and is not accessible. As such, each of the share counters
 *       MUST be 0 or 1, and it MUST be ok to access them without holding the
 *       obj_lock.
 *
 * @param[in] orig_hdl     fsal_obj_handle the orig_share belongs to
 * @param[in] orig_share   Original share
 * @param[in] dupe_share   Duplicate share
 *
 * @retval ERR_FSAL_SHARE_DENIED - a conflict occurred.
 *
 */

fsal_status_t merge_share(struct fsal_obj_handle *orig_hdl,
			  struct fsal_share *orig_share,
			  struct fsal_share *dupe_share)
{
	fsal_status_t status = {ERR_FSAL_SHARE_DENIED, 0};

	/* Check if dupe_share represents no share reservation at all, if
	 * so, we can trivially exit. There's nothing to do and we don't
	 * need the obj_lock.
	 */
	if (dupe_share->share_deny_read == 0 &&
	    dupe_share->share_deny_write == 0 &&
	    dupe_share->share_deny_write_mand == 0 &&
	    dupe_share->share_access_read == 0 &&
	    dupe_share->share_access_write == 0) {
		/* No conflict and no update, just return success. */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	PTHREAD_RWLOCK_wrlock(&orig_hdl->obj_lock);

	if (dupe_share->share_access_read > 0 &&
	    orig_share->share_deny_read > 0) {
		LogDebug(COMPONENT_STATE,
			 "Share conflict detected: access read denied by existing deny read");
		goto out_conflict;
	}

	if (dupe_share->share_deny_read > 0 &&
	    orig_share->share_access_read > 0) {
		LogDebug(COMPONENT_STATE,
			 "Share conflict detected: deny read denied by existing access read");
		goto out_conflict;
	}

	/* When checking deny write, we ONLY need to look at share_deny_write
	 * since it counts BOTH FSAL_O_DENY_WRITE and FSAL_O_DENY_WRITE_MAND.
	 */
	if (dupe_share->share_access_write > 0 &&
	    orig_share->share_deny_write > 0) {
		LogDebug(COMPONENT_STATE,
			 "Share conflict detected: access write denied by existing deny write");
		goto out_conflict;
	}

	if (dupe_share->share_deny_write > 0 &&
	    orig_share->share_access_write > 0) {
		LogDebug(COMPONENT_STATE,
			 "Share conflict detected: deny write denied by existing access write");
		goto out_conflict;
	}

	/* Now that we are ok, merge the share counters in the original */
	orig_share->share_access_read += dupe_share->share_access_read;
	orig_share->share_access_write += dupe_share->share_access_write;
	orig_share->share_deny_read += dupe_share->share_deny_read;
	orig_share->share_deny_write += dupe_share->share_deny_write;
	orig_share->share_deny_write_mand += dupe_share->share_deny_write_mand;

	status = fsalstat(ERR_FSAL_NO_ERROR, 0);

 out_conflict:

	PTHREAD_RWLOCK_unlock(&orig_hdl->obj_lock);

	return status;
}

/**
 * @brief Reopen the fd associated with the object handle.
 *
 * This function assures that the fd is open in the mode requested. If
 * the fd was already open, it closes it and reopens with the OR of the
 * requested modes.
 *
 * This function will return with the object handle lock held for read
 * if successful, except in the case where a temporary file descriptor is
 * in use because of a conflict with another thread. By not holding the
 * lock in that case, it may allow yet a third thread to open the global
 * file descriptor in a usable mode reducing the use of temporary file
 * descriptors.
 *
 * On calling, out_fd must point to a temporary fd. On return, out_fd
 * will either still point to the temporary fd, which has now been opened
 * and must be closed when done, or it will point to the object handle's
 * global fd, which should be left open.
 *
 * Optionally, out_fd can be NULL in which case a file is not actually
 * opened, in this case, all that actually happens is the share reservation
 * check (which may result in the lock being held).
 *
 * If openflags is FSAL_O_ANY, the caller will utilize the global file
 * descriptor if it is open, otherwise it will use a temporary file descriptor.
 * The primary use of this is to not open long lasting global file descriptors
 * for getattr and setattr calls. The other users of FSAL_O_ANY are NFSv3 LOCKT
 * for which this behavior is also desireable and NFSv3 UNLOCK where there
 * SHOULD be an open file descriptor attached to state, but if not, a temporary
 * file descriptor will work fine (and the resulting unlock won't do anything
 * since we just opened the temporary file descriptor).
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  check_share Indicates we must check for share conflict
 * @param[in]  bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in] bypass       If check_share is true, indicates to bypass
 *                         share_deny_read and share_deny_write but
 *                         not share_deny_write_mand
 * @param[in]  openflags   Mode for open
 * @param[in]  my_fd       The file descriptor associated with the object
 * @param[in]  share       The fsal_share associated with the object
 * @param[in]  open_func   Function to open a file descriptor
 * @param[in]  close_func  Function to close a file descriptor. This will only
 *                         ever be called when there is already a rwlock held on
 *                         the object handle.
 * @param[in,out] out_fd   File descriptor that is to be used
 * @param[out] has_lock    Indicates that obj_hdl->lock is held read
 * @param[out] closefd     Indicates that file descriptor must be closed
 *
 * @return FSAL status.
 */

fsal_status_t fsal_reopen_obj(struct fsal_obj_handle *obj_hdl,
			      bool check_share,
			      bool bypass,
			      fsal_openflags_t openflags,
			      struct fsal_fd *my_fd,
			      struct fsal_share *share,
			      fsal_open_func open_func,
			      fsal_close_func close_func,
			      struct fsal_fd **out_fd,
			      bool *has_lock,
			      bool *closefd)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	bool retried = false;
	fsal_openflags_t try_openflags;
	int rc;

	*closefd = false;

	/* Take read lock on object to protect file descriptor.
	 * We only take a read lock because we are not changing the
	 * state of the file descriptor.
	 */
	PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

	if (check_share) {
		/* Note we will check again if we drop and re-acquire the lock
		 * just to be on the safe side.
		 */
		status = check_share_conflict(share, openflags, bypass);

		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			LogDebug(COMPONENT_FSAL,
				 "check_share_conflict failed with %s",
				 msg_fsal_err(status.major));
			*has_lock = false;
			return status;
		}
	}

	if (out_fd == NULL) {
		/* We are just checking share reservation if at all.
		 * There is no need to proceed, we either passed the
		 * share check, or didn't need it. In either case, there
		 * is no need to open a file.
		 */
		*has_lock = true;
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

again:

	LogFullDebug(COMPONENT_FSAL,
		     "Open mode = %x, desired mode = %x",
		     (int) my_fd->openflags,
		     (int) openflags);

	if (not_open_usable(my_fd->openflags, openflags)) {

		/* Drop the read lock */
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		if (openflags == FSAL_O_ANY) {
			/* If caller is looking for any open descriptor, don't
			 * bother trying to open global file descriptor if it
			 * isn't already open, just go ahead an open a temporary
			 * file descriptor.
			 */
			LogDebug(COMPONENT_FSAL,
				 "Open in FSAL_O_ANY mode failed, just open temporary file descriptor.");

			/* Although the global file descriptor isn't "busy" (we
			 * can't acquire a write lock, re-use of EBUSY in this
			 * case simplifies the code below.
			 */
			rc = EBUSY;
		} else if (retried) {
			/* Since we drop write lock for 'obj_hdl->obj_lock'
			 * and acquire read lock for 'obj_hdl->obj_lock' after
			 * opening the global file descriptor, some other
			 * thread could have closed the file causing
			 * verification of 'openflags' to fail.
			 *
			 * We will now attempt to just provide a temporary
			 * file descriptor. EBUSY is sort of true...
			 */
			LogDebug(COMPONENT_FSAL,
				 "Retry failed.");
			rc = EBUSY;
		} else {
			/* Switch to write lock on object to protect file
			 * descriptor.
			 * By using trylock, we don't block if another thread
			 * is using the file descriptor right now. In that
			 * case, we just open a temporary file descriptor.
			 *
			 * This prevents us from blocking for the duration of
			 * an I/O request.
			 */
			rc = pthread_rwlock_trywrlock(&obj_hdl->obj_lock);
		}

		if (rc == EBUSY) {
			/* Someone else is using the file descriptor or it
			 * isn't open at all and the caller is looking for
			 * any mode of open so a temporary file descriptor will
			 * work fine.
			 *
			 * Just provide a temporary file descriptor.
			 * We still take a read lock so we can protect the
			 * share reservation for the duration of the caller's
			 * operation if we needed to check.
			 */
			if (check_share) {
				PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);

				status = check_share_conflict(share,
							      openflags,
							      bypass);

				if (FSAL_IS_ERROR(status)) {
					PTHREAD_RWLOCK_unlock(
							&obj_hdl->obj_lock);
					LogDebug(COMPONENT_FSAL,
						 "check_share_conflict failed with %s",
						 msg_fsal_err(status.major));
					*has_lock = false;
					return status;
				}
			}

			status = open_func(obj_hdl, openflags, *out_fd);

			if (FSAL_IS_ERROR(status)) {
				if (check_share)
					PTHREAD_RWLOCK_unlock(
							&obj_hdl->obj_lock);
				*has_lock = false;
				return status;
			}

			/* Return the temp fd, with the lock only held if
			 * share reservations were checked.
			 */
			*closefd = true;
			*has_lock = check_share;

			return fsalstat(ERR_FSAL_NO_ERROR, 0);

		} else if (rc != 0) {
			LogCrit(COMPONENT_RW_LOCK,
				"Error %d, write locking %p", rc, obj_hdl);
			abort();
		}

		if (check_share) {
			status = check_share_conflict(share, openflags, bypass);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				LogDebug(COMPONENT_FSAL,
					 "check_share_conflict failed with %s",
					 msg_fsal_err(status.major));
				*has_lock = false;
				return status;
			}
		}

		LogFullDebug(COMPONENT_FSAL,
			     "Open mode = %x, desired mode = %x",
			     (int) my_fd->openflags,
			     (int) openflags);

		if (not_open_usable(my_fd->openflags, openflags)) {
			if (my_fd->openflags != FSAL_O_CLOSED) {
				ssize_t count;

				/* Add desired mode to existing mode. */
				try_openflags = openflags | my_fd->openflags;

				/* Now close the already open descriptor. */
				status = close_func(obj_hdl, my_fd);

				if (FSAL_IS_ERROR(status)) {
					PTHREAD_RWLOCK_unlock(
							&obj_hdl->obj_lock);
					LogDebug(COMPONENT_FSAL,
						 "close_func failed with %s",
						 msg_fsal_err(status.major));
					*has_lock = false;
					return status;
				}
				count = atomic_dec_size_t(&open_fd_count);
				if (count < 0) {
					LogCrit(COMPONENT_FSAL,
					    "open_fd_count is negative: %zd",
					    count);
				}
			} else if (openflags == FSAL_O_ANY) {
				try_openflags = FSAL_O_READ;
			} else {
				try_openflags = openflags;
			}

			LogFullDebug(COMPONENT_FSAL,
				     "try_openflags = %x",
				     try_openflags);

			if (!mdcache_lru_fds_available()) {
				PTHREAD_RWLOCK_unlock(
						&obj_hdl->obj_lock);
				*has_lock = false;
				/* This seems the best idea, let the
				 * client try again later after the reap.
				 */
				return fsalstat(ERR_FSAL_DELAY, 0);
			}

			/* Actually open the file */
			status = open_func(obj_hdl, try_openflags, my_fd);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				LogDebug(COMPONENT_FSAL,
					 "open_func failed with %s",
					 msg_fsal_err(status.major));
				*has_lock = false;
				return status;
			}

			(void) atomic_inc_size_t(&open_fd_count);
		}

		/* Ok, now we should be in the correct mode.
		 * Switch back to read lock and try again.
		 * We don't want to hold the write lock because that would
		 * block other users of the file descriptor.
		 * Since we dropped the lock, we need to verify mode is still'
		 * good after we re-aquire the read lock, thus the retry.
		 */
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		PTHREAD_RWLOCK_rdlock(&obj_hdl->obj_lock);
		retried = true;

		if (check_share) {
			status = check_share_conflict(share, openflags, bypass);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				LogDebug(COMPONENT_FSAL,
					 "check_share_conflict failed with %s",
					 msg_fsal_err(status.major));
				*has_lock = false;
				return status;
			}
		}
		goto again;
	}

	/* Return the global fd, with the lock held. */
	*out_fd = my_fd;
	*has_lock = true;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Find a useable file descriptor for a regular file.
 *
 * This function specifically does NOT return with the obj_handle's lock
 * held if the fd associated with a state_t is being used. These fds are
 * considered totally separate from the global fd and don't need protection
 * and should not interfere with other operations on the object.
 *
 * Optionally, out_fd can be NULL in which case a file is not actually
 * opened, in this case, all that actually happens is the share reservation
 * check (which may result in the lock being held).
 *
 * Note that FSAL_O_ANY may be passed on to fsal_reopen_obj, see the
 * documentation of that function for the implications.
 *
 * @param[in,out] out_fd         File descriptor that is to be used
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     obj_fd         The file descriptor associated with the object
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in]     state          state_t to use for this operation
 * @param[in]     openflags      Mode for open
 * @param[in]     open_func      Function to open a file descriptor
 * @param[in]     close_func     Function to close a file descriptor. This will
 *                               only ever be called when there is already a
 *                               rwlock held on the object handle.
 * @param[out]    has_lock       Indicates that obj_hdl->obj_lock is held read
 * @param[out]    closefd        Indicates that file descriptor must be closed
 * @param[in]     open_for_locks Indicates file is open for locks
 * @param[out]    reusing_open_state_fd Indicates whether already opened fd
 *					can be reused
 *
 * @return FSAL status.
 */

fsal_status_t fsal_find_fd(struct fsal_fd **out_fd,
			   struct fsal_obj_handle *obj_hdl,
			   struct fsal_fd *obj_fd,
			   struct fsal_share *share,
			   bool bypass,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   fsal_open_func open_func,
			   fsal_close_func close_func,
			   bool *has_lock,
			   bool *closefd,
			   bool open_for_locks,
			   bool *reusing_open_state_fd)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	struct fsal_fd *state_fd;

	if (state == NULL)
		goto global;

	/* Check if we can use the fd in the state */
	state_fd = (struct fsal_fd *) (state + 1);

	LogFullDebug(COMPONENT_FSAL,
		     "state_fd->openflags = %d openflags = %d%s",
		     state_fd->openflags, openflags,
		     open_for_locks ? " Open For Locks" : "");

	if (open_correct(state_fd->openflags, openflags)) {
		/* It was valid, return it.
		 * Since we found a valid fd in the state, no need to
		 * check deny modes.
		 */
		LogFullDebug(COMPONENT_FSAL, "Use state_fd %p", state_fd);
		if (out_fd)
			*out_fd = state_fd;
		*has_lock = false;
		return status;
	}

	if (open_for_locks) {
		if (state_fd->openflags != FSAL_O_CLOSED) {
			LogCrit(COMPONENT_FSAL,
				"Conflicting open, can not re-open fd with locks");
			return fsalstat(posix2fsal_error(EINVAL), EINVAL);
		}

		/* This is being opened for locks, we will not be able to
		 * re-open so open for read/write. If that fails permission
		 * check and openstate is available, retry with that state's
		 * access mode.
		 */
		openflags = FSAL_O_RDWR;

		status = open_func(obj_hdl, openflags, state_fd);

		if (status.major == ERR_FSAL_ACCESS &&
		   (state->state_type == STATE_TYPE_LOCK ||
		    state->state_type == STATE_TYPE_NLM_LOCK) &&
		    state->state_data.lock.openstate != NULL) {
			/* Got an EACCESS and openstate is available, try
			 * again with it's openflags.
			 */
			struct fsal_fd *related_fd = (struct fsal_fd *)
					(state->state_data.lock.openstate + 1);

			openflags = related_fd->openflags & FSAL_O_RDWR;

			status = open_func(obj_hdl, openflags, state_fd);
		}

		if (FSAL_IS_ERROR(status)) {
			LogCrit(COMPONENT_FSAL,
				"Open for locking failed for access %s",
				openflags == FSAL_O_RDWR ? "Read/Write"
				: openflags == FSAL_O_READ ? "Read"
				: "Write");
		} else {
			LogFullDebug(COMPONENT_FSAL,
				     "Opened state_fd %p", state_fd);
			*out_fd = state_fd;
		}

		*has_lock = false;
		return status;
	}

	/* Check if there is a related state, in which case, can we use it's
	 * fd (this will support FSALs that have an open file per open state
	 * but don't bother with opening a separate file for the lock state).
	 */
	if ((state->state_type == STATE_TYPE_LOCK ||
	     state->state_type == STATE_TYPE_NLM_LOCK) &&
	    state->state_data.lock.openstate != NULL) {
		struct fsal_fd *related_fd = (struct fsal_fd *)
				(state->state_data.lock.openstate + 1);

		LogFullDebug(COMPONENT_FSAL,
			     "related_fd->openflags = %d openflags = %d",
			     related_fd->openflags, openflags);

		if (open_correct(related_fd->openflags, openflags)) {
			/* It was valid, return it.
			 * Since we found a valid fd in the open state, no
			 * need to check deny modes.
			 */
			LogFullDebug(COMPONENT_FSAL,
				     "Use related_fd %p", related_fd);
			if (out_fd) {
				*out_fd = related_fd;
				/* The associated open state has an open fd,
				 * however some FSALs can not use it and must
				 * need to dup the fd into the lock state
				 * instead. So to signal this to the caller
				 * function the following flag
				 */
				*reusing_open_state_fd = true;
			}

			*has_lock = false;
			return status;
		}
	}

 global:

	/* No useable state_t so return the global file descriptor. */
	LogFullDebug(COMPONENT_FSAL,
		     "Use global fd openflags = %x",
		     openflags);

	/* Make sure global is open as necessary otherwise return a
	 * temporary file descriptor. Check share reservation if not
	 * opening FSAL_O_ANY.
	 */
	return fsal_reopen_obj(obj_hdl, openflags != FSAL_O_ANY, bypass,
			       openflags, obj_fd, share, open_func, close_func,
			       out_fd, has_lock, closefd);
}

/**
 * @brief Check the exclusive create verifier for a file.
 *
 * The default behavior is to check verifier against atime and mtime.
 *
 * @param[in] st           POSIX attributes for the file (from stat)
 * @param[in] verifier     Verifier to use for exclusive create
 * @param[in] trunc_verif  Use onlu 31 bits of each half of the verifier
 *
 * @retval true if verifier matches
 */

bool check_verifier_stat(struct stat *st,
			 fsal_verifier_t verifier,
			 bool trunc_verif)
{
	uint32_t verf_hi = 0, verf_lo = 0;

	memcpy(&verf_hi,
	       verifier,
	       sizeof(uint32_t));
	memcpy(&verf_lo,
	       verifier + sizeof(uint32_t),
	       sizeof(uint32_t));

	if (trunc_verif) {
		verf_hi &= INT32_MAX;
		verf_lo &= INT32_MAX;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Passed verifier %"PRIx32" %"PRIx32
		     " file verifier %"PRIx32" %"PRIx32,
		     verf_hi, verf_lo,
		     (uint32_t) st->st_atim.tv_sec,
		     (uint32_t) st->st_mtim.tv_sec);

	return st->st_atim.tv_sec == verf_hi &&
	       st->st_mtim.tv_sec == verf_lo;
}

/**
 * @brief Check the exclusive create verifier for a file.
 *
 * The default behavior is to check verifier against atime and mtime.
 *
 * @param[in] attrs        Attributes for the file
 * @param[in] verifier     Verifier to use for exclusive create
 * @param[in] trunc_verif  Use onlu 31 bits of each half of the verifier
 *
 * @retval true if verifier matches
 */

bool check_verifier_attrlist(struct fsal_attrlist *attrs,
			     fsal_verifier_t verifier,
			     bool trunc_verif)
{
	uint32_t verf_hi = 0, verf_lo = 0;

	memcpy(&verf_hi,
	       verifier,
	       sizeof(uint32_t));
	memcpy(&verf_lo,
	       verifier + sizeof(uint32_t),
	       sizeof(uint32_t));

	if (trunc_verif) {
		verf_hi &= INT32_MAX;
		verf_lo &= INT32_MAX;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "Passed verifier %"PRIx32" %"PRIx32
		     " file verifier %"PRIx32" %"PRIx32,
		     verf_hi, verf_lo,
		     (uint32_t) attrs->atime.tv_sec,
		     (uint32_t) attrs->mtime.tv_sec);

	return attrs->atime.tv_sec == verf_hi &&
	       attrs->mtime.tv_sec == verf_lo;
}

/**
 * @brief Common is_referral routine for FSALs that use the special mode
 *
 * @param[in]     obj_hdl       Handle on which to operate
 * @param[in|out] attrs         Attributes of the handle
 * @param[in]     cache_attrs   Cache the received attrs
 *
 * Most FSALs don't support referrals, but those that do often use a special
 * mode bit combination on a directory for a junction. This routine tests for
 * that and returns true if it is a referral.
 */
bool fsal_common_is_referral(struct fsal_obj_handle *obj_hdl,
			     struct fsal_attrlist *attrs, bool cache_attrs)
{
	attrmask_t req_mask = ATTR_TYPE | ATTR_MODE;

	LogDebug(COMPONENT_FSAL,
		 "Checking attrs for referral, handle: %p, valid_mask: %" PRIx64
		 ", request_mask: %" PRIx64 ", supported: %" PRIx64,
		 obj_hdl, attrs->valid_mask,
		 attrs->request_mask, attrs->supported);

	if ((attrs->valid_mask & req_mask) != req_mask) {
		/* Required attributes are not available, need to fetch them */
		fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

		attrs->request_mask |= req_mask;

		status = obj_hdl->obj_ops->getattrs(obj_hdl, attrs);
		if (FSAL_IS_ERROR(status)) {
			/* Drop the message level to debug if referral belongs
			 * to deleted file to avoid flood of messages.
			 */
			if (status.major == ERR_FSAL_STALE) {
				LogDebug(COMPONENT_FSAL,
					"Failed to get attrs for referral, handle: %p (probably deleted), valid_mask: %"
					PRIx64
					", request_mask: %" PRIx64
					", supported: %" PRIx64
					", error: %s",
					obj_hdl, attrs->valid_mask,
					attrs->request_mask, attrs->supported,
					fsal_err_txt(status));
			} else {
				LogEvent(COMPONENT_FSAL,
					"Failed to get attrs for referral, handle: %p, valid_mask: %"
					PRIx64
					", request_mask: %" PRIx64
					", supported: %" PRIx64
					", error: %s",
					obj_hdl, attrs->valid_mask,
					attrs->request_mask, attrs->supported,
					fsal_err_txt(status));
			}
			return false;
		}
	}

	if (!fsal_obj_handle_is(obj_hdl, DIRECTORY))
		return false;

	if (!is_sticky_bit_set(obj_hdl, attrs))
		return false;

	LogDebug(COMPONENT_FSAL, "Referral found for handle: %p", obj_hdl);
	return true;
}

struct gsh_refstr *no_export;

void init_ctx_refstr(void)
{
	no_export = gsh_refstr_dup("No Export");
}

void destroy_ctx_refstr(void)
{
	gsh_refstr_put(no_export);
}

static void set_op_context_export_fsal_no_release(struct gsh_export *exp,
						  struct fsal_export *fsal_exp,
						  struct fsal_pnfs_ds *pds,
						  bool discard_refstr)
{
	if (discard_refstr) {
		gsh_refstr_put(op_ctx->ctx_fullpath);
		gsh_refstr_put(op_ctx->ctx_pseudopath);
	}

	op_ctx->ctx_export = exp;
	op_ctx->fsal_export = fsal_exp;
	op_ctx->ctx_pnfs_ds = pds;

	rcu_read_lock();

	if (op_ctx->ctx_export != NULL &&
	    op_ctx->ctx_export->fullpath) {
		op_ctx->ctx_fullpath = gsh_refstr_get(rcu_dereference(
					op_ctx->ctx_export->fullpath));
	} else {
		/* Normally an export always has a fullpath refstr, however
		 * we might be called from free_export_resources before the
		 * refstr is set up. Or we may be called with no export.
		 */
		op_ctx->ctx_fullpath = gsh_refstr_get(no_export);
	}

	if (op_ctx->ctx_export != NULL &&
	    op_ctx->ctx_export->pseudopath != NULL) {
		op_ctx->ctx_pseudopath = gsh_refstr_get(rcu_dereference(
					op_ctx->ctx_export->pseudopath));
	} else {
		/* Normally an export always has a pseudopath refstr, however
		 * we might be called from free_export_resources before the
		 * refstr is set up. Or we may be called with no export.
		 */
		op_ctx->ctx_pseudopath = gsh_refstr_get(no_export);
	}

	rcu_read_unlock();

	if (fsal_exp)
		op_ctx->fsal_module = fsal_exp->fsal;
	else if (!op_ctx->fsal_module && op_ctx->saved_op_ctx)
		op_ctx->fsal_module = op_ctx->saved_op_ctx->fsal_module;
}

void set_op_context_export_fsal(struct gsh_export *exp,
				struct fsal_export *fsal_exp)
{
	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	if (op_ctx->ctx_pnfs_ds != NULL)
		pnfs_ds_put(op_ctx->ctx_pnfs_ds);

	set_op_context_export_fsal_no_release(exp, fsal_exp, NULL, true);
}

void set_op_context_pnfs_ds(struct fsal_pnfs_ds *pds)
{
	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	if (op_ctx->ctx_pnfs_ds != NULL)
		pnfs_ds_put(op_ctx->ctx_pnfs_ds);

	set_op_context_export_fsal_no_release(pds->mds_export,
					      pds->mds_fsal_export,
					      pds,
					      true);
}

static inline void clear_op_context_export_impl(void)
{
	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	if (op_ctx->ctx_pnfs_ds != NULL)
		pnfs_ds_put(op_ctx->ctx_pnfs_ds);

	if (op_ctx->ctx_fullpath != NULL)
		gsh_refstr_put(op_ctx->ctx_fullpath);

	if (op_ctx->ctx_pseudopath != NULL)
		gsh_refstr_put(op_ctx->ctx_pseudopath);

	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;
}

void clear_op_context_export(void)
{
	clear_op_context_export_impl();

	/* An active op context will always have refstr */
	op_ctx->ctx_fullpath = gsh_refstr_get(no_export);
	op_ctx->ctx_pseudopath = gsh_refstr_get(no_export);
}

static void save_op_context_export(struct saved_export_context *saved)
{
	saved->saved_export = op_ctx->ctx_export;
	saved->saved_fullpath = op_ctx->ctx_fullpath;
	saved->saved_pseudopath = op_ctx->ctx_pseudopath;
	saved->saved_fsal_export = op_ctx->fsal_export;
	saved->saved_fsal_module = op_ctx->fsal_module;
	saved->saved_pnfs_ds = op_ctx->ctx_pnfs_ds;
	saved->saved_export_perms = op_ctx->export_perms;
}

void save_op_context_export_and_set_export(struct saved_export_context *saved,
					   struct gsh_export *exp)
{
	save_op_context_export(saved);

	/* Don't release op_ctx->ctx_export since it's saved */
	set_op_context_export_fsal_no_release(exp, exp->fsal_export, NULL,
					      false);
}

void save_op_context_export_and_clear(struct saved_export_context *saved)
{
	save_op_context_export(saved);
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;
	op_ctx->ctx_pnfs_ds = NULL;
}

void restore_op_context_export(struct saved_export_context *saved)
{
	clear_op_context_export();
	op_ctx->ctx_export = saved->saved_export;
	op_ctx->ctx_fullpath = saved->saved_fullpath;
	op_ctx->ctx_pseudopath = saved->saved_pseudopath;
	op_ctx->fsal_export = saved->saved_fsal_export;
	op_ctx->fsal_module = saved->saved_fsal_module;
	op_ctx->ctx_pnfs_ds = saved->saved_pnfs_ds;
	op_ctx->export_perms = saved->saved_export_perms;
}

void discard_op_context_export(struct saved_export_context *saved)
{
	if (saved->saved_export)
		put_gsh_export(saved->saved_export);

	if (saved->saved_pnfs_ds != NULL)
		pnfs_ds_put(op_ctx->ctx_pnfs_ds);

	if (saved->saved_fullpath != NULL)
		gsh_refstr_put(saved->saved_fullpath);

	if (saved->saved_pseudopath != NULL)
		gsh_refstr_put(saved->saved_pseudopath);
}

void init_op_context(struct req_op_context *ctx,
		     struct gsh_export *exp,
		     struct fsal_export *fsal_exp,
		     sockaddr_t *caller_data,
		     uint32_t nfs_vers,
		     uint32_t nfs_minorvers,
		     enum request_type req_type)
{
	/* Initialize ctx.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	memset(ctx, 0, sizeof(*ctx));

	ctx->saved_op_ctx = op_ctx;
	op_ctx = ctx;

	ctx->nfs_vers = nfs_vers;
	ctx->nfs_minorvers = nfs_minorvers;
	ctx->req_type = req_type;
	ctx->caller_addr = caller_data;

	/* Since this is a brand new op context, no need to release anything.
	 */
	set_op_context_export_fsal_no_release(exp, fsal_exp, NULL, false);

	ctx->export_perms.set = root_op_export_set;
	ctx->export_perms.options = root_op_export_options;
}

void release_op_context(void)
{
	struct req_op_context *cur_ctx = op_ctx;

	clear_op_context_export_impl();

	/* And now we're done with the gsh_refstr */
	op_ctx->ctx_fullpath = NULL;
	op_ctx->ctx_pseudopath = NULL;

	/* And restore the saved op_ctx */
	op_ctx = op_ctx->saved_op_ctx;
	cur_ctx->saved_op_ctx = NULL;
}

void suspend_op_context(void)
{
	/* We cannot touch the contents of op_ctx, because it may be already
	 * freed by the async callback.  Just NULL out op_ctx here.  */
	op_ctx = NULL;
}

void resume_op_context(struct req_op_context *ctx)
{
	ctx->saved_op_ctx = op_ctx;
	op_ctx = ctx;

	if (op_ctx->client != NULL) {
		/* Set the Client IP for this thread */
		SetClientIP(op_ctx->client->hostaddr_str);
	}
}

/** @} */
