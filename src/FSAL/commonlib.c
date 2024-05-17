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
 * on the public part of the FSAL api and are therefore shareable by all fsal
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
#include "atomic_utils.h"
#include "sys_resource.h"
#ifdef USE_DBUS
#include "gsh_dbus.h"
#endif

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
	PTHREAD_RWLOCK_wrlock(&fsal_hdl->fsm_lock);
	glist_del(obj_link);
	PTHREAD_RWLOCK_unlock(&fsal_hdl->fsm_lock);
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
			  object_file_type_t type, bool add_to_fsal_handle)
{
	obj->fsal = exp->fsal;
	obj->type = type;
	PTHREAD_RWLOCK_init(&obj->obj_lock, NULL);

	if (add_to_fsal_handle) {
		PTHREAD_RWLOCK_wrlock(&obj->fsal->fsm_lock);
		glist_add(&obj->fsal->handles, &obj->handles);
		PTHREAD_RWLOCK_unlock(&obj->fsal->fsm_lock);
	}
}

void fsal_obj_handle_fini(struct fsal_obj_handle *obj,
			  bool added_to_fsal_handle)
{
	if (added_to_fsal_handle) {
		PTHREAD_RWLOCK_wrlock(&obj->fsal->fsm_lock);
		glist_del(&obj->handles);
		PTHREAD_RWLOCK_unlock(&obj->fsal->fsm_lock);
	}
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
	assert(pds->fsal);

	PTHREAD_RWLOCK_wrlock(&pds->fsal->fsm_lock);
	glist_del(&pds->server);
	PTHREAD_RWLOCK_unlock(&pds->fsal->fsm_lock);

	memset(&pds->s_ops, 0, sizeof(pds->s_ops));	/* poison myself */

	fsal_put(pds->fsal);
	pds->fsal = NULL;
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
		nfs4_acl_release_entry(attrs->acl);
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
fsal_mode_gen_set(fsal_ace_t *ace_owner_group, fsal_ace_t *ace_everyone,
		  uint32_t mode)
{
	fsal_ace_t *allow, *deny;
	/* All should have the READ_ATTR & READ_ACL allowed by default.
	 * Owner in addition Should have the write attr, acl & owner
	 * (actually owner may just change the gid to group it belongs to) */
	const fsal_aceperm_t default_attr_acl_read_perm =
			FSAL_ACE_PERM_READ_ATTR | FSAL_ACE_PERM_READ_ACL;

	/* @OWNER */
	deny = ace_owner_group;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_OWNER;
	GET_FSAL_ACE_IFLAG(*allow) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_OWNER;
	GET_FSAL_ACE_IFLAG(*deny) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_PERM(*allow) |= default_attr_acl_read_perm;
	GET_FSAL_ACE_PERM(*allow) |= FSAL_ACE_PERM_WRITE_ATTR |
			FSAL_ACE_PERM_WRITE_ACL | FSAL_ACE_PERM_WRITE_OWNER;
	fsal_mode_set_ace(deny, allow, mode & S_IRWXU);
	/* @GROUP */
	deny += 2;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_GROUP;
	GET_FSAL_ACE_IFLAG(*allow) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_GROUP;
	GET_FSAL_ACE_IFLAG(*deny) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_PERM(*allow) |= default_attr_acl_read_perm;
	fsal_mode_set_ace(deny, allow, (mode & S_IRWXG) << 3);
	/* @EVERYONE */
	deny = ace_everyone;
	allow = deny + 1;
	GET_FSAL_ACE_USER(*allow) = FSAL_ACE_SPECIAL_EVERYONE;
	GET_FSAL_ACE_IFLAG(*allow) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_USER(*deny) = FSAL_ACE_SPECIAL_EVERYONE;
	GET_FSAL_ACE_IFLAG(*deny) |= FSAL_ACE_IFLAG_SPECIAL_ID;
	GET_FSAL_ACE_PERM(*allow) |= default_attr_acl_read_perm;
	fsal_mode_set_ace(deny, allow, (mode & S_IRWXO) << 6);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t
fsal_mode_gen_acl(struct fsal_attrlist *attrs)
{
	fsal_acl_data_t acl_data;
	fsal_acl_status_t acl_status;

	if (attrs->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		nfs4_acl_release_entry(attrs->acl);
	}

	acl_data.naces = 6;
	acl_data.aces = nfs4_ace_alloc(acl_data.naces);

	fsal_mode_gen_set(acl_data.aces, acl_data.aces + 4, attrs->mode);

	attrs->acl = nfs4_acl_new_entry(&acl_data, &acl_status);
	if (attrs->acl == NULL)
		LogFatal(COMPONENT_FSAL,
				"Failed in nfs4_acl_new_entry, acl_status %d",
				acl_status);

	FSAL_SET_MASK(attrs->valid_mask, ATTR_ACL);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

static bool
fsal_check_ace_couple(const fsal_ace_t *aces, uid_t who)
{
	for (uint32_t i = 0; i < 2; i++) {
		if (!IS_FSAL_ACE_SPECIAL_ID(aces[i]) ||
				!IS_FSAL_ACE_USER(aces[i], who))
			return false;
		if (IS_FSAL_ACE_INHERIT(aces[i]))
			return false;
		if (i == 0) {
			if (!IS_FSAL_ACE_DENY(aces[i]))
				return false;
		} else {
			if (!IS_FSAL_ACE_ALLOW(aces[i]))
				return false;
		}
	}
	return true;
}

bool fsal_can_reuse_mode_to_acl(const fsal_acl_t *sacl)
{
	/*
	 * Identify whether existing aces can be reused for representing mode.
	 * Can't rely on the FSAL_ACE_IFLAG_MODE_GEN because if the client
	 * calls another SETATTR with ACL, the internal flag info will be lost.
	*/
	const fsal_ace_t *ace;

	if (!sacl || sacl->naces < 6)
		return false;

	/* OWNER@ */
	ace = &sacl->aces[0];
	if (!fsal_check_ace_couple(ace, FSAL_ACE_SPECIAL_OWNER))
		return false;
	ace = &sacl->aces[2];
	if (!fsal_check_ace_couple(ace, FSAL_ACE_SPECIAL_GROUP))
		return false;
	ace = &sacl->aces[sacl->naces - 2];
	if (!fsal_check_ace_couple(ace, FSAL_ACE_SPECIAL_EVERYONE))
		return false;

	return true;
}

static bool
fsal_can_skip_ace(const fsal_ace_t *ace, uint32_t indx, uint32_t naces,
		  bool can_reuse)
{
	/* We can skip the copy of special ID aces which don't hold permission
	 * flags such as DELETE or DELETE-CHILD which are not covered by mode
	 * bits. Those special ID aces are going to be generated (or reused)
	 * by the mode to acl mechanism. */

	/* The first 4 aces and last 2 aces are going to be reused. */
	if (can_reuse && (indx < 4 || indx >= naces - 2))
		return false;

	if (!IS_FSAL_ACE_SPECIAL_ID(*ace))
		return false;

	if (IS_FSAL_ACE_INHERIT_ONLY(*ace))
		return false;

	if (IS_FSAL_ACE_DELETE(*ace) || IS_FSAL_ACE_DELETE_CHILD(*ace))
		return false;

	return true;
}

fsal_status_t fsal_mode_to_acl(struct fsal_attrlist *attrs, fsal_acl_t *sacl)
{
	int naces;
	fsal_ace_t *sace, *dace;
	bool can_reuse;

	if (!FSAL_TEST_MASK(attrs->valid_mask, ATTR_MODE))
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	if (!sacl || sacl->naces == 0)
		return fsal_mode_gen_acl(attrs);

	can_reuse = fsal_can_reuse_mode_to_acl(sacl);
	LogFullDebug(COMPONENT_FSAL, "Can reuse aces for mode: %d", can_reuse);

	naces = 0;
	for (sace = sacl->aces; sace < sacl->aces + sacl->naces; sace++) {
		if (fsal_can_skip_ace(sace, sace - sacl->aces, sacl->naces,
				can_reuse))
			continue;
		naces++;
		if (IS_FSAL_ACE_INHERIT_ONLY(*sace))
			continue;
		if (!IS_FSAL_ACE_PERM(*sace))
			continue;
		/* XXX dang dup for non-special case */
	}

	if (naces == 0) {
		/* Only mode generate aces */
		return fsal_mode_gen_acl(attrs);
	}

	/* Space for generated ACEs - OWNER, GROUP at start and EVERYONE
	 * at the end */
	if (!can_reuse) {
		naces += 6;
	}

	if (attrs->acl != NULL) {
		/* We should never be passed attributes that have an
		 * ACL attached, but just in case some future code
		 * path changes that assumption, let's not release the
		 * old ACL properly.
		 */
		nfs4_acl_release_entry(attrs->acl);
	}

	LogFullDebug(COMPONENT_FSAL, "naces: %d", naces);

	fsal_acl_data_t acl_data;

	acl_data.aces = nfs4_ace_alloc(naces);
	acl_data.naces = 0;
	dace = can_reuse ? acl_data.aces : acl_data.aces + 4;

	for (sace = sacl->aces; sace < sacl->aces + sacl->naces;
	     sace++) {
		if (fsal_can_skip_ace(sace, sace - sacl->aces, sacl->naces,
				can_reuse))
			continue;
		*dace = *sace;
		acl_data.naces++;

		if (IS_FSAL_ACE_INHERIT_ONLY(*dace) ||
		    (!IS_FSAL_ACE_PERM(*dace))) {
			dace++;
			continue;
		}

		if (IS_FSAL_ACE_SPECIAL_ID(*dace))
			GET_FSAL_ACE_PERM(*dace) &=
				~(FSAL_ACE_PERM_READ_DATA |
				  FSAL_ACE_PERM_LIST_DIR |
				  FSAL_ACE_PERM_WRITE_DATA |
				  FSAL_ACE_PERM_ADD_FILE |
				  FSAL_ACE_PERM_APPEND_DATA |
				  FSAL_ACE_PERM_ADD_SUBDIRECTORY |
				  FSAL_ACE_PERM_EXECUTE);
		else if (IS_FSAL_ACE_ALLOW(*dace)) {
			/* Do non-special stuff */
			if ((attrs->mode & S_IRGRP) == 0)
				GET_FSAL_ACE_PERM(*dace) &=
						~(FSAL_ACE_PERM_READ_DATA |
						  FSAL_ACE_PERM_LIST_DIR);
			if ((attrs->mode & S_IWGRP) == 0)
				GET_FSAL_ACE_PERM(*dace) &=
					~(FSAL_ACE_PERM_WRITE_DATA |
					  FSAL_ACE_PERM_ADD_FILE |
					  FSAL_ACE_PERM_APPEND_DATA |
					  FSAL_ACE_PERM_ADD_SUBDIRECTORY);
			if ((attrs->mode & S_IXGRP) == 0)
				GET_FSAL_ACE_PERM(*dace) &=
						~FSAL_ACE_PERM_EXECUTE;
		}
		dace++;
	}

	if ((!can_reuse && naces - acl_data.naces != 6) ||
			(can_reuse && naces != acl_data.naces)) {
		LogDebug(COMPONENT_FSAL, "Bad naces: %d not %d",
			 acl_data.naces, naces - 6);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	/* 4 aces for OWNER & GROUP shall be set in the beginning, and 2 aces
	 * for EVERYONE shall be placed at the end. */
	fsal_mode_gen_set(acl_data.aces, acl_data.aces + naces - 2,
			attrs->mode);

	fsal_acl_status_t acl_status;

	acl_data.naces = naces;
	attrs->acl = nfs4_acl_new_entry(&acl_data, &acl_status);
	LogFullDebug(COMPONENT_FSAL, "acl_status after nfs4_acl_new_entry: %d",
			acl_status);
	if (attrs->acl == NULL)
		LogFatal(COMPONENT_FSAL, "Failed in nfs4_acl_new_entry");
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
	{ /* other */
		S_IROTH, S_IWOTH, S_IXOTH
	}
};

fsal_status_t fsal_acl_to_mode(struct fsal_attrlist *attrs)
{
	uint32_t *modes;
	uint32_t who;

	if (!FSAL_TEST_MASK(attrs->valid_mask, ATTR_ACL))
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	if (!attrs->acl || attrs->acl->naces == 0)
		return fsalstat(ERR_FSAL_NO_ERROR, 0);

	/* Clear all mode bits except the first 3 special bits */
	attrs->mode &= (S_ISUID | S_ISGID | S_ISVTX);

	/* Compute the mode bits according to RFC 8881 section 6.3.2 */
	for (who = FSAL_ACE_SPECIAL_OWNER; who <= FSAL_ACE_SPECIAL_EVERYONE;
			who++) {
		uint32_t allowed = 0, denied = 0;
		uint32_t i;

		for (i = 0; i < attrs->acl->naces; i++) {
			const fsal_ace_t *ace = &attrs->acl->aces[i];

			if (!IS_FSAL_ACE_PERM(*ace) ||
					IS_FSAL_ACE_INHERIT_ONLY(*ace))
				continue;
			if (!IS_FSAL_ACE_SPECIAL_ID(*ace))
				continue;
			if (!IS_FSAL_ACE_USER(*ace, who) &&
					!IS_FSAL_ACE_SPECIAL_EVERYONE(*ace))
				continue;

			modes = ace_modes[who - FSAL_ACE_SPECIAL_OWNER];
			if (IS_FSAL_ACE_READ_DATA(*ace)) {
				if (IS_FSAL_ACE_ALLOW(*ace) &&
						(denied & modes[0]) == 0)
					allowed |= modes[0];
				else
					denied |= modes[0];
			}
			if (IS_FSAL_ACE_WRITE_DATA(*ace) ||
					IS_FSAL_ACE_APPEND_DATA(*ace)) {
				if (IS_FSAL_ACE_ALLOW(*ace) &&
						(denied & modes[1]) == 0)
					allowed |= modes[1];
				else
					denied |= modes[1];
			}
			if (IS_FSAL_ACE_EXECUTE(*ace)) {
				if (IS_FSAL_ACE_ALLOW(*ace) &&
						(denied & modes[2]) == 0)
					allowed |= modes[2];
				else
					denied |= modes[2];
			}
		}
		FSAL_SET_MASK(attrs->mode, allowed);
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
this refinement gets rid of the rw lock in the I/O path and provides a bit
more fairness to open/upgrade/downgrade/close and uses dec_and_lock to
better prevent spurious wakeup due to a race that is not handled above

increment io_work
if fd_work != 0
	lock mutex
	if fd_work != 0
		// fd work is waiting, block until it's done
		if decrement io_work == 0
			signal condition var
		while fd_work != 0
			wait on condition var
		// we are done waiting for fd work, so resume I/O
		increment io_work
	unlock mutex
// now we know no fd work is waiting or in progress and can't start
initiate I/O

on I/O complete (async or immediate):
-------------------------------------

if decrement_and_lock io_work
	if fd_work != 0
		signal condition var
	unlock mutex

open/upgrade/downgrade/close:
-----------------------------

increment fd_work
lock mutex
while io_work != 0
	// now fd_work is non-zero and io_work is zero, any I/O that
	// tries to start will block until fd_work goes to zero
	wait on cond var

// io_work was zero, and because we checked while holding the mutex, but made
// fd_work non-zero before taking the mutex any I/O threads that try to start
// after we took the mutex MUST be blocked, proceed
unlock mutex

// serialize any open/upgrade/downgrade/close
take write lock
re-open fd in new mode or close it
release write lock

// now ready to allow I/O to resume
if decrement_and_lock fd_work
	if fd_work == 0
		signal cond var
	unlock mutex
*/

pthread_mutex_t fsal_fd_mutex;
pthread_cond_t fsal_fd_cond;
struct glist_head fsal_fd_global_lru = GLIST_HEAD_INIT(fsal_fd_global_lru);
int32_t fsal_fd_global_counter;
uint32_t fsal_fd_state_counter;
uint32_t fsal_fd_temp_counter;
time_t lru_run_interval;
bool Cache_FDs;
static struct fridgethr *fd_lru_fridge;

uint32_t lru_try_one(void)
{
	struct fsal_fd *fsal_fd;
	fsal_status_t status;
	struct req_op_context op_context;
	struct fsal_obj_handle *obj_hdl;
	int work = 0;

	PTHREAD_MUTEX_lock(&fsal_fd_mutex);

	fsal_fd = glist_last_entry(&fsal_fd_global_lru,
				   struct fsal_fd,
				   fd_lru);

	if (fsal_fd != NULL) {
		/* Protect the fsal_fd until we can get it's lock. */
		atomic_inc_int32_t(&fsal_fd->lru_reclaim);
		/* Drop the fsal_fd_mutex so we can take the work_mutex. */
		PTHREAD_MUTEX_unlock(&fsal_fd_mutex);

		get_gsh_export_ref(fsal_fd->fsal_export->owning_export);
		/* Now we can safely work on the object, we want to close it. */
		init_op_context_simple(&op_context,
				       fsal_fd->fsal_export->owning_export,
				       fsal_fd->fsal_export);

		fsal_fd->fsal_export->exp_ops.get_fsal_obj_hdl(
			fsal_fd->fsal_export, fsal_fd, &obj_hdl);

		status = close_fsal_fd(obj_hdl, fsal_fd, true);

		if (!FSAL_IS_ERROR(status))
			work = 1;

		release_op_context();

		/* Now reacquire the fsal_fd_mutex */
		PTHREAD_MUTEX_lock(&fsal_fd_mutex);
		/* And drop the flag */
		atomic_dec_int32_t(&fsal_fd->lru_reclaim);
		/* And let anyone waiting know we're ok... */
		PTHREAD_COND_signal(&fsal_fd_cond);
	}

	PTHREAD_MUTEX_unlock(&fsal_fd_mutex);

	return work;
}

struct fd_lru_state fd_lru_state;

uint32_t futility_count;
uint32_t required_progress;
uint32_t reaper_work;

/**
 * @brief Function that executes in the fd_lru thread
 *
 * This function is responsible for cleaning the FD cache.  It works
 * by the following rules:
 *
 *  - If the number of open FDs is below the low water mark, do
 *    nothing.
 *
 *  - If the number of open FDs is between the low and high water
 *    mark, make one pass through...
 *
 *  - If the number of open FDs is greater than the high water mark,
 *    we consider ourselves to be in extremis.  In this case we make a
 *    number of passes through the queue not to exceed the number of
 *    passes that would be required to process the number of entries
 *    equal to a biggest_window percent of the system specified
 *    maximum.
 *
 *  - If we are in extremis, and performing the maximum amount of work
 *    allowed has not moved the open FD count required_progress%
 *    toward the high water mark, increment fd_lru_state.futility.  If
 *    fd_lru_state.futility reaches futility_count, temporarily disable
 *    FD caching.
 *
 *  - Every time we wake through timeout, reset futility_count to 0.
 *
 *  - If we fall below the low water mark and FD caching has been
 *    temporarily disabled, re-enable it.
 *
 * @param[in] ctx Fridge context
 */

void fd_lru_run(struct fridgethr_context *ctx)
{
	/* True if we were explicitly awakened. */
	bool woke = ctx->woke;
	/* Finalized */
	uint32_t fdratepersec = 1, fds_avg, fddelta;
	float fdnorm, fdwait_ratio, fdmulti;
	time_t threadwait = lru_run_interval;
	/* True if we are taking extreme measures to reclaim FDs */
	bool extremis = false;
	/* Total work done in all passes so far.  If this exceeds the
	 * window, stop.
	 */
	uint32_t totalwork = 0;
	/* The current count (after reaping) of open FDs */
	int32_t currentopen = 0;
	time_t new_thread_wait;
	static bool first_time = TRUE;

	if (first_time) {
		/* Wait for NFS server to properly initialize */
		nfs_init_wait();
		first_time = FALSE;
	}

	SetNameFunction("fd_lru");

	fds_avg = (fd_lru_state.fds_hiwat - fd_lru_state.fds_lowat) / 2;

	currentopen = atomic_fetch_int32_t(&fsal_fd_global_counter);

	extremis = currentopen > fd_lru_state.fds_hiwat;

	LogFullDebug(COMPONENT_FSAL, "FD LRU awakes.");

	if (!woke) {
		/* If we make it all the way through a timed sleep
		   without being woken, we assume we aren't racing
		   against the impossible. */
		if (fd_lru_state.futility >= futility_count)
			LogInfo(COMPONENT_FSAL,
				"Leaving FD futility mode.");

		fd_lru_state.futility = 0;
	}

	/* Check for fd_state transitions */

	LogDebug(COMPONENT_FSAL,
		 "FD count fsal_fd_global_counter is %"PRIi32
		 " and low water mark is %"PRIi32
		 " and high water mark is %"PRIi32" %s",
		 currentopen, fd_lru_state.fds_lowat, fd_lru_state.fds_hiwat,
		 ((currentopen >= fd_lru_state.fds_lowat)
			|| (Cache_FDs == false))
			? "(reaping)" : "(not reaping)");

	if (currentopen < fd_lru_state.fds_lowat) {
		if (atomic_fetch_uint32_t(&fd_lru_state.fd_state) > FD_LOW) {
			LogEvent(COMPONENT_FSAL,
				 "Return to normal fd reaping.");
			atomic_store_uint32_t(&fd_lru_state.fd_state, FD_LOW);
		}
	} else if (currentopen < fd_lru_state.fds_hiwat &&
		   atomic_fetch_uint32_t(&fd_lru_state.fd_state) == FD_LIMIT) {
		LogEvent(COMPONENT_FSAL,
			 "Count of fd is below high water mark.");
		atomic_store_uint32_t(&fd_lru_state.fd_state,
				      FD_MIDDLE);
	}

	/* Reap file descriptors.  This is a preliminary example of the
	 * L2 functionality rather than something we expect to be
	 * permanent.  (It will have to adapt heavily to the new FSAL
	 * API, for example.)
	 */

	if ((currentopen >= fd_lru_state.fds_lowat) || (Cache_FDs == false)) {
		/* The count of open file descriptors before this run
		   of the reaper. */
		int32_t formeropen = currentopen;
		/* Work done in the most recent pass of all queues.  if
		   value is less than the work to do in a single queue,
		   don't spin through more passes. */
		uint32_t workpass = 0;
		time_t curr_time = time(NULL);

		if ((curr_time >= fd_lru_state.prev_time) &&
		    (curr_time - fd_lru_state.prev_time <
							fridgethr_getwait(ctx)))
			threadwait = curr_time - fd_lru_state.prev_time;

		fdratepersec = ((curr_time <= fd_lru_state.prev_time) ||
				(formeropen < fd_lru_state.prev_fd_count))
			? 1 : (formeropen - fd_lru_state.prev_fd_count) /
					(curr_time - fd_lru_state.prev_time);

		LogFullDebug(COMPONENT_FSAL,
			     "fdrate:%u fdcount:%" PRIu32
			     " slept for %" PRIu64 " sec",
			     fdratepersec, formeropen,
			     ((uint64_t) (curr_time - fd_lru_state.prev_time)));

		if (extremis) {
			LogDebug(COMPONENT_FSAL,
				 "Open FDs over high water mark, reaping aggressively.");
		}

		/* Attempt to close fds. */
		do {
			int i;

			workpass = 0;

			LogDebug(COMPONENT_FSAL,
				 "Reaping up to %" PRIu32 " fds",
				 reaper_work);

			LogFullDebug(COMPONENT_FSAL,
				     "formeropen=%" PRIu32
				     " totalwork=%" PRIu32,
				     formeropen, totalwork);

			for (i = 0; i < reaper_work; ++i)
				workpass += lru_try_one();

			totalwork += workpass;
		} while (extremis && (workpass >= reaper_work)
			 && (totalwork < fd_lru_state.biggest_window));

		currentopen = atomic_fetch_int32_t(&fsal_fd_global_counter);

		if (extremis &&
		    ((currentopen > formeropen)
		     || (formeropen - currentopen <
			 (((formeropen - fd_lru_state.fds_hiwat) *
			      required_progress) / 100)))) {
			if (++fd_lru_state.futility ==
			    futility_count) {
				LogWarn(COMPONENT_FSAL,
					"Futility count exceeded.  Client load is opening FDs faster than the LRU thread can close them. current_open = %"
					PRIi32", former_open = %"PRIi32,
					currentopen, formeropen);
			}
		}
	}

	/* The following calculation will progressively garbage collect
	 * more frequently as these two factors increase:
	 * 1. current number of open file descriptors
	 * 2. rate at which file descriptors are being used.
	 *
	 * When there is little activity, this thread will sleep at the
	 * "LRU_Run_Interval" from the config.
	 *
	 * When there is a lot of activity, the thread will sleep for a
	 * much shorter time.
	 */
	fd_lru_state.prev_fd_count = currentopen;
	fd_lru_state.prev_time = time(NULL);

	fdnorm = (fdratepersec + fds_avg) / fds_avg;
	fddelta = (currentopen > fd_lru_state.fds_lowat)
			? (currentopen - fd_lru_state.fds_lowat) : 0;
	fdmulti = (fddelta * 10) / fds_avg;
	fdmulti = fdmulti ? fdmulti : 1;
	fdwait_ratio = fd_lru_state.fds_hiwat /
			((fd_lru_state.fds_hiwat + fdmulti * fddelta) * fdnorm);

	new_thread_wait = threadwait * fdwait_ratio;

	if (new_thread_wait < lru_run_interval / 10)
		new_thread_wait = lru_run_interval / 10;

	/* if new_thread_wait is 0, lru_run will not be scheduled */
	if (new_thread_wait == 0)
		new_thread_wait = 1;

	fridgethr_setwait(ctx, new_thread_wait);

	LogDebug(COMPONENT_FSAL,
		 "After work, fsal_fd_global_counter:%" PRIi32
		 " fdrate:%u new_thread_wait=%" PRIu64,
		 atomic_fetch_int32_t(&fsal_fd_global_counter),
		 fdratepersec, (uint64_t) new_thread_wait);
	LogFullDebug(COMPONENT_FSAL,
		     "currentopen=%" PRIu32
		     " futility=%d totalwork=%" PRIu32
		     " biggest_window=%d extremis=%d fds_lowat=%d ",
		     currentopen, fd_lru_state.futility, totalwork,
		     fd_lru_state.biggest_window, extremis,
		     fd_lru_state.fds_lowat);
}

/**
 * @brief Bump this fsal_fd in the fd LRU if this is a global fd.
 *
 * @param[in] fsal_fd  The fsal_fd to insert.
 *
 */

void bump_fd_lru(struct fsal_fd *fsal_fd)
{
	if (fsal_fd->fd_type == FSAL_FD_GLOBAL) {
		PTHREAD_MUTEX_lock(&fsal_fd_mutex);

		glist_del(&fsal_fd->fd_lru);
		glist_add(&fsal_fd_global_lru, &fsal_fd->fd_lru);

		PTHREAD_MUTEX_unlock(&fsal_fd_mutex);
		LogFullDebug(COMPONENT_FSAL,
			"Inserted fsal_fd(%p) to fd_global_lru with count(%d)",
			fsal_fd, atomic_fetch_int32_t(&fsal_fd_global_counter));
	}
}

/**
 * @brief Increment the appropriate fd counter and insert into fd LRU if
 *        this is a global fd.
 *
 * @param[in] fsal_fd  The fsal_fd to insert.
 *
 */

void insert_fd_lru(struct fsal_fd *fsal_fd)
{
	LogFullDebug(COMPONENT_FSAL,
		"Inserting fsal_fd(%p) to fd_lru for type(%d) count(%d/%d/%d)",
		fsal_fd, fsal_fd->fd_type,
		atomic_fetch_int32_t(&fsal_fd_global_counter),
		atomic_fetch_int32_t(&fsal_fd_state_counter),
		atomic_fetch_int32_t(&fsal_fd_temp_counter));

	switch (fsal_fd->fd_type) {
	case FSAL_FD_OLD_STYLE:
		/* OOPS - we shouldn't get here... */
		assert(fsal_fd->fd_type < FSAL_FD_GLOBAL);
		break;
	case FSAL_FD_GLOBAL:
		atomic_inc_int32_t(&fsal_fd_global_counter);
		bump_fd_lru(fsal_fd);
		break;
	case FSAL_FD_STATE:
		atomic_inc_int32_t(&fsal_fd_state_counter);
		break;
	case FSAL_FD_TEMP:
		atomic_inc_int32_t(&fsal_fd_temp_counter);
		break;
	}
}

/**
 * @brief Decrement the appropriate fd counter and remove from fd LRU if
 *        this is a global fd.
 *
 * @param[in] fsal_fd  The fsal_fd to insert.
 *
 */

void remove_fd_lru(struct fsal_fd *fsal_fd)
{
	int32_t count;

	LogFullDebug(COMPONENT_FSAL,
		"Removing fsal_fd(%p) from fd_lru for type(%d) count(%d/%d/%d)",
		fsal_fd, fsal_fd->fd_type,
		atomic_fetch_int32_t(&fsal_fd_global_counter),
		atomic_fetch_int32_t(&fsal_fd_state_counter),
		atomic_fetch_int32_t(&fsal_fd_temp_counter));

	switch (fsal_fd->fd_type) {
	case FSAL_FD_OLD_STYLE:
		/* OOPS - we shouldn't get here... */
		assert(fsal_fd->fd_type < FSAL_FD_GLOBAL);
		break;
	case FSAL_FD_GLOBAL:
		count = atomic_dec_int32_t(&fsal_fd_global_counter);

		if (count < 0) {
			LogCrit(COMPONENT_FSAL,
				"fsal_fd_global_counter is negative: %"PRIi32,
				count);
			abort();
		}

		PTHREAD_MUTEX_lock(&fsal_fd_mutex);

		glist_del(&fsal_fd->fd_lru);

		PTHREAD_MUTEX_unlock(&fsal_fd_mutex);
		break;
	case FSAL_FD_STATE:
		atomic_dec_int32_t(&fsal_fd_state_counter);
		break;
	case FSAL_FD_TEMP:
		atomic_dec_int32_t(&fsal_fd_temp_counter);
		break;
	}
}

void fsal_init_fds_limit(struct fd_lru_parameter *params)
{
	int code = 0;
	/* Rlimit for open file descriptors */
	struct rlimit rlim = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY
	};

	fd_lru_state.fd_fallback_limit = params->fd_fallback_limit;

	/* Find out the system-imposed file descriptor limit */
	if (get_open_file_limit(&rlim) != 0) {
		code = errno;
		LogCrit(COMPONENT_MDCACHE_LRU,
			"Call to getrlimit failed with error %d. This should not happen.  Assigning default of %d.",
			code, fd_lru_state.fd_fallback_limit);
		fd_lru_state.fds_system_imposed =
						fd_lru_state.fd_fallback_limit;
	} else {
		if (rlim.rlim_cur < rlim.rlim_max) {
			/* Save the old soft value so we can fall back to it
			   if setrlimit fails. */
			rlim_t old_soft = rlim.rlim_cur;

			LogInfo(COMPONENT_MDCACHE_LRU,
				"Attempting to increase soft limit from %"
				PRIu64 " to hard limit of %" PRIu64,
				(uint64_t) rlim.rlim_cur,
				(uint64_t) rlim.rlim_max);
			rlim.rlim_cur = rlim.rlim_max;
			if (setrlimit(RLIMIT_NOFILE, &rlim) < 0) {
				code = errno;
				LogWarn(COMPONENT_MDCACHE_LRU,
					"Attempt to raise soft FD limit to hard FD limit failed with error %d.  Sticking to soft limit.",
					code);
				rlim.rlim_cur = old_soft;
			}
		}

		if (rlim.rlim_cur == RLIM_INFINITY) {
			FILE *nr_open;

			nr_open = fopen("/proc/sys/fs/nr_open", "r");
			if (nr_open == NULL) {
				code = errno;
				LogWarn(COMPONENT_MDCACHE_LRU,
					"Attempt to open /proc/sys/fs/nr_open failed (%d)",
					code);
				goto err_open;
			}

			code = fscanf(nr_open, "%" SCNu32 "\n",
				      &fd_lru_state.fds_system_imposed);

			if (code != 1) {
				code = errno;

				LogMajor(COMPONENT_MDCACHE_LRU,
					 "The rlimit on open file descriptors is infinite and the attempt to find the system maximum failed with error %d.",
					 code);
				LogMajor(COMPONENT_MDCACHE_LRU,
					 "Assigning the default fallback of %d which is almost certainly too small.",
					 fd_lru_state.fd_fallback_limit);
				LogMajor(COMPONENT_MDCACHE_LRU,
					 "If you are on a Linux system, this should never happen.");
				LogMajor(COMPONENT_MDCACHE_LRU,
					 "If you are running some other system, please set an rlimit on file descriptors (for example, with ulimit) for this process and consider editing "
					 __FILE__
					 "to add support for finding your system's maximum.");

				fd_lru_state.fds_system_imposed =
				    fd_lru_state.fd_fallback_limit;
			}

			fclose(nr_open);
err_open:
			;
		} else {
			fd_lru_state.fds_system_imposed = rlim.rlim_cur;
		}
	}

	LogEvent(COMPONENT_MDCACHE_LRU,
		"Setting the system-imposed limit on FDs to %d.",
		fd_lru_state.fds_system_imposed);

	fd_lru_state.fds_hard_limit =
	    (params->fd_limit_percent * fd_lru_state.fds_system_imposed) / 100;

	fd_lru_state.fds_hiwat =
	    (params->fd_hwmark_percent * fd_lru_state.fds_system_imposed) / 100;

	fd_lru_state.fds_lowat =
	    (params->fd_lwmark_percent * fd_lru_state.fds_system_imposed) / 100;

	fd_lru_state.futility = 0;

	if (params->reaper_work) {
		/* Backwards compatibility */
		reaper_work = (params->reaper_work + 16) / 17;
	} else {
		/* New parameter */
		reaper_work = params->reaper_work_per_lane;
	}

	fd_lru_state.biggest_window =
	      (params->biggest_window * fd_lru_state.fds_system_imposed) / 100;
}

/**
 * Initialize subsystem
 */
fsal_status_t fd_lru_pkginit(struct fd_lru_parameter *params)
{
	/* Return code from system calls */
	int code = 0;
	struct fridgethr_params frp;

	PTHREAD_MUTEX_init(&fsal_fd_mutex, NULL);
	PTHREAD_COND_init(&fsal_fd_cond, NULL);

	futility_count = params->futility_count;
	required_progress = params->required_progress;
	lru_run_interval = params->lru_run_interval;
	Cache_FDs = params->Cache_FDs;

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = 1;
	frp.thr_min = 1;
	frp.thread_delay = params->lru_run_interval;
	frp.flavor = fridgethr_flavor_looper;

	atomic_store_int32_t(&fsal_fd_global_counter, 0);
	fd_lru_state.prev_fd_count = 0;
	atomic_store_uint32_t(&fd_lru_state.fd_state, FD_LOW);
	fsal_init_fds_limit(params);

	/* spawn LRU background thread */
	code = fridgethr_init(&fd_lru_fridge, "FD_LRU_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Unable to initialize FD LRU fridge, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	code = fridgethr_submit(fd_lru_fridge, fd_lru_run, NULL);
	if (code != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Unable to start Entry LRU thread, error code %d.",
			 code);
		return fsalstat(posix2fsal_error(code), code);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * Shutdown subsystem
 *
 * @return 0 on success, POSIX errors on failure.
 */
fsal_status_t fd_lru_pkgshutdown(void)
{
	int rc;

	rc = fridgethr_sync_command(fd_lru_fridge, fridgethr_comm_stop, 120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(fd_lru_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_MDCACHE_LRU,
			 "Failed shutting down LRU thread: %d", rc);
	}

	PTHREAD_MUTEX_destroy(&fsal_fd_mutex);
	PTHREAD_COND_destroy(&fsal_fd_cond);

	return fsalstat(posix2fsal_error(rc), rc);
}

/**
 * @brief Function to close a fsal_fd while protecting it.
 *
 * @param[in]  obj_hdl        File on which to operate
 * @param[in]  fsal_fd        File handle to close
 * @param[in]  is_reclaiming  Indicates we are closing files to reclaim fd
 *
 * @return FSAL status.
 */

fsal_status_t close_fsal_fd(struct fsal_obj_handle *obj_hdl,
			    struct fsal_fd *fsal_fd,
			    bool is_reclaiming)
{
	fsal_status_t status;
	bool is_globalfd = fsal_fd->fd_type == FSAL_FD_GLOBAL;

	/* Assure that is_reclaiming is only set when it's a global fd */
	assert(is_reclaiming ? is_globalfd : true);

	/* Indicate we want to do fd work */
	status = fsal_start_fd_work(fsal_fd, is_reclaiming);

	if (FSAL_IS_ERROR(status)) {
		LogFullDebug(COMPONENT_FSAL,
			 "fsal_start_fd_work returned %s",
			 fsal_err_txt(status));
		return status;
	}

	/* Now we hold the mutex and no one is doing I/O so we can safely
	 * close the fd.
	 */
	status = obj_hdl->obj_ops->close_func(obj_hdl, fsal_fd);

	if (status.major != ERR_FSAL_NOT_OPENED) {
		if (is_globalfd) {
			/* Need to decrement the appropriate counter and remove
			 * from LRU for globalfd.
			 */
			remove_fd_lru(fsal_fd);
		}
	} else {
		/* Wasn't open.  Not an error, but shouldn't remove from LRU. */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	fsal_complete_fd_work(fsal_fd);

	if (is_reclaiming) {
		PTHREAD_MUTEX_lock(&fsal_fd_mutex);
		PTHREAD_COND_signal(&fsal_fd_cond);
		PTHREAD_MUTEX_unlock(&fsal_fd_mutex);
	} else if (is_globalfd) {
		while (atomic_fetch_int32_t(&fsal_fd->lru_reclaim)) {
			/* Just in case FD LRU is trying to work on this fd,
			 * wait until its done. Note it really won't have
			 * anything to do since we have just closed the fd, but
			 * this assures the lifetime of the fsal_fd is
			 * maintained while LRU does its work.
			 */
			PTHREAD_MUTEX_lock(&fsal_fd_mutex);
			PTHREAD_COND_wait(&fsal_fd_cond, &fsal_fd_mutex);
			PTHREAD_MUTEX_unlock(&fsal_fd_mutex);
		}
	}

	return status;
}

/**
 * @brief Function to open or reopen a fsal_fd.
 *
 * NOTE: Assumes fsal_fd->work_mutex is held and that fd_work has been
 *       incremented. After re_opening if necessary, fd_work will be
 *       decremented, fd_work_cond will be signaled, and io_work_cond will be
 *       broadcast.
 *
 * NOTE: We should not come in here with openflags of FSAL_O_ANY
 *
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  openflags   New mode for open
 * @param[out] fd          File descriptor that is to be used
 * @param[in]  can_start   We know for sure we can start
 *
 * @return FSAL status.
 */

fsal_status_t reopen_fsal_fd(struct fsal_obj_handle *obj_hdl,
			     fsal_openflags_t openflags,
			     struct fsal_fd *fsal_fd,
			     bool can_start)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	/* Wait for lull in io work */
	while (!can_start && atomic_fetch_int32_t(&fsal_fd->io_work) != 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "%p wait for lull - io_work = %"PRIi32
			     " fd_work = %"PRIi32,
			     fsal_fd,
			     atomic_fetch_int32_t(&fsal_fd->io_work),
			     atomic_fetch_int32_t(&fsal_fd->fd_work));

		/* io work is in progress or trying to start,
		 * wait for it to complete (or not start)
		 */
		PTHREAD_COND_wait(&fsal_fd->fd_work_cond,
				  &fsal_fd->work_mutex);
	}

	fsal_openflags_t old_openflags = fsal_fd->openflags;
	/* Now that we are actually about to open or re-open, let's
	 * make sure we get the file opened however desired.
	 *
	 * Take the requested mode and combine with the existing mode.
	 * Then, in case any thread is asking for read combine that,
	 * an in case any thread is asking for write, combine that.
	 */
	openflags |= fsal_fd->openflags & FSAL_O_RDWR;

	if (atomic_fetch_int32_t(&fsal_fd->want_read))
		openflags |= FSAL_O_READ;

	if (atomic_fetch_int32_t(&fsal_fd->want_write))
		openflags |= FSAL_O_WRITE;

	/* And THEN check the combined mode against the current mode. */
	if (!open_correct(fsal_fd->openflags, openflags)) {
		status = fsal_reopen_fd(obj_hdl, openflags, fsal_fd);
		LogDebug(COMPONENT_FSAL,
			 "fsal_reopen_fd returned %s",
			 fsal_err_txt(status));

		if (FSAL_IS_SUCCESS(status)) {
			if (old_openflags == FSAL_O_CLOSED) {
				/* This is actually an open, need to increment
				* appropriate counter and insert into LRU.
				*/
				insert_fd_lru(fsal_fd);
			} else {
				/* We are touching the file so bump it in the
				* LRU to help keep the LRU tail free of
				* unreapable fds.
				*/
				bump_fd_lru(fsal_fd);
			}
		}
	}

	/* Indicate we are done with fd work and signal any waiters. */
	atomic_dec_int32_t(&fsal_fd->fd_work);

	LogFullDebug(COMPONENT_FSAL,
		     "%p done fd work - io_work = %"PRIi32
		     " fd_work = %"PRIi32,
		     fsal_fd,
		     atomic_fetch_int32_t(&fsal_fd->io_work),
		     atomic_fetch_int32_t(&fsal_fd->fd_work));

	/* Wake up at least one thread waiting to do fd work */
	PTHREAD_COND_signal(&fsal_fd->fd_work_cond);

	/* Wake up all threads waiting to do io work */
	PTHREAD_COND_broadcast(&fsal_fd->io_work_cond);

	return status;
}

/**
 * @brief Check if allowed to open or re-open.
 *
 * @param[in] fsal_fd    The fsal_fd we are checking
 * @param[in] may_open   Allowed to open
 * @param[in] may_reopen Allowed to re-open
 *
 * @return true if not allowed to open or re-open
 * @return false otherwise
 *
 */

static inline bool cant_reopen(struct fsal_fd *fsal_fd,
			       bool may_open,
			       bool may_reopen)
{
	int32_t open_fds = atomic_fetch_int32_t(&fsal_fd_global_counter);

	if (fsal_fd->fd_type == FSAL_FD_GLOBAL &&
	    open_fds >= fd_lru_state.fds_hard_limit) {
		LogAtLevel(COMPONENT_FSAL,
			   atomic_fetch_uint32_t(&fd_lru_state.fd_state)
								!= FD_LIMIT
				? NIV_CRIT
				: NIV_DEBUG,
			   "FD Hard Limit (%"PRIu32
			   ") Exceeded (fsal_fd_global_counter = %" PRIi32
			   "), waking LRU thread.",
			   fd_lru_state.fds_hard_limit, open_fds);
		atomic_store_uint32_t(&fd_lru_state.fd_state, FD_LIMIT);
		fridgethr_wake(fd_lru_fridge);

		/* Too many open files, don't open any more. */
		return true;
	}

	if (fsal_fd->fd_type == FSAL_FD_GLOBAL &&
	    open_fds >= fd_lru_state.fds_hiwat) {
		LogAtLevel(COMPONENT_FSAL,
			   atomic_fetch_uint32_t(&fd_lru_state.fd_state)
				== FD_LOW
					? NIV_INFO
					: NIV_DEBUG,
			   "FDs above high water mark (%"PRIu32
			   ", fsal_fd_global_counter = %" PRIi32
			   "), waking LRU thread.",
			   fd_lru_state.fds_hiwat, open_fds);
		atomic_store_uint32_t(&fd_lru_state.fd_state, FD_HIGH);
		fridgethr_wake(fd_lru_fridge);
	}

	if (may_open && fsal_fd->openflags == 0) {
		/* Can open and was closed */
		return false;
	}

	/* Assumed the openflags were wrong, reverse sense of may_reopen */
	return !may_reopen;
}

/**
 * @brief Wait to start I/O. Returns with io_work counter incremented.
 *
 * NOTE: If openflags is FSAL_O_ANY then may_open and may_reopen MUST be false.
 *
 * @param[in] obj_hdl         The objet we are working on
 * @param[in] fsal_fd         The file descriptor to do I/O on
 * @param[in] openflags       The open mode required for the I/O desired
 * @param[in] may_open        Allowed to open the file
 * @param[in] may_reopen      Allowed to re-open the file
 *
 */

fsal_status_t wait_to_start_io(struct fsal_obj_handle *obj_hdl,
			       struct fsal_fd *fsal_fd,
			       fsal_openflags_t openflags,
			       bool may_open,
			       bool may_reopen)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	bool retried = false;

retry:

	/* Indicate we want to do I/O work */
	atomic_inc_int32_t(&fsal_fd->io_work);

	LogFullDebug(COMPONENT_FSAL,
		     "%p try io_work = %"PRIi32" fd_work = %"PRIi32,
		     fsal_fd,
		     atomic_fetch_int32_t(&fsal_fd->io_work),
		     atomic_fetch_int32_t(&fsal_fd->fd_work));

	/* The following two checks make sure that if multiple threads are
	 * contending to do fd_work, whoever actually opens or re-opens the
	 * file will open it in a mode that satisfies everyone. Note that we
	 * only want to do this once, thus the check for retried == false.
	 */

	if (openflags & FSAL_O_READ && retried == false) {
		/* Indicate we want to read */
		atomic_inc_int32_t(&fsal_fd->want_read);
	}

	if (openflags & FSAL_O_WRITE && retried == false) {
		/* Indicate we want to read */
		atomic_inc_int32_t(&fsal_fd->want_write);
	}

	/* Check if fd_work is in progress (or trying to start). We do this
	 * check unlocked because fd work will check io work in a way that
	 * assures that io work always wins any race.
	 */
	while (atomic_fetch_int32_t(&fsal_fd->fd_work) != 0) {
		/* We need to back off on trying to do I/O so the fd work can
		 * complete.
		 */
		LogFullDebug(COMPONENT_FSAL,
			     "%p back off io_work (-1) = %"PRIi32
			     " fd_work = %"PRIi32,
			     fsal_fd,
			     atomic_fetch_int32_t(&fsal_fd->io_work) - 1,
			     atomic_fetch_int32_t(&fsal_fd->fd_work));

		if (PTHREAD_MUTEX_dec_int32_t_and_lock(&fsal_fd->io_work,
						       &fsal_fd->work_mutex)) {
			/* Let the thread waiting to do fd work know it can
			 * proceed.
			 */
			PTHREAD_COND_signal(&fsal_fd->fd_work_cond);
		} else {
			/* need the mutex anyway... */
			PTHREAD_MUTEX_lock(&fsal_fd->work_mutex);
		}

		/* Now we need to wait on the io_work condition variable...
		 */
		while (atomic_fetch_int32_t(&fsal_fd->fd_work) != 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "%p wait for fd work - io_work = %"PRIi32
				     " fd_work = %"PRIi32,
				     fsal_fd,
				     atomic_fetch_int32_t(&fsal_fd->io_work),
				     atomic_fetch_int32_t(&fsal_fd->fd_work));

			PTHREAD_COND_wait(&fsal_fd->io_work_cond,
					  &fsal_fd->work_mutex);
		}

		/* At this point the fd's open flags are protected because we
		 * hold the mutex so we can check them and see if we need to
		 * open or re-open. We do this here even though it's somewhat
		 * redundant with the similar check outside the lock so we
		 * don't thrash the mutex.
		 */
		LogFullDebug(COMPONENT_FSAL,
			     "Open mode = %x, desired mode = %x",
			     (int) fsal_fd->openflags,
			     (int) openflags);

		if (!open_correct(fsal_fd->openflags, openflags)) {
			if (cant_reopen(fsal_fd, may_open, may_reopen)) {
				/* fsal_fd is in wrong mode and we aren't
				 * allowed to reopen, so return EBUSY.
				 */
				PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);
				/* Use fsalstat to avoid LogInfo... */
				status = fsalstat(ERR_FSAL_DELAY, EBUSY);
				goto out;
			}

			/* Indicate we want to do fd work */
			atomic_inc_int32_t(&fsal_fd->fd_work);

			LogFullDebug(COMPONENT_FSAL,
				     "%p try fd work - io_work = %"PRIi32
				     " fd_work = %"PRIi32,
				     fsal_fd,
				     atomic_fetch_int32_t(&fsal_fd->io_work),
				     atomic_fetch_int32_t(&fsal_fd->fd_work));

			status = reopen_fsal_fd(obj_hdl, openflags, fsal_fd,
						false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);
				goto out;
			}
		}

		/* Try again to indicate we want to do I/O work. Note that this
		 * WILL prevent the fd from being closed, which means that
		 * since reopen_fsal_fd() combines all desired open modes, we
		 * WILL be able to use the fd once we actually get to start
		 * I/O.
		 */
		atomic_inc_int32_t(&fsal_fd->io_work);

		LogFullDebug(COMPONENT_FSAL,
			     "%p try io_work = %"PRIi32" fd_work = %"PRIi32,
			     fsal_fd,
			     atomic_fetch_int32_t(&fsal_fd->io_work),
			     atomic_fetch_int32_t(&fsal_fd->fd_work));

		PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);
	}

	/* At this point the fd's open flags are protected so we can check
	 * them. If we had to wait for other threads to do fd_work, then
	 * because reopen_fsal_fd() combines modes, the file MUST now be in a
	 * usable state, and open_correct() will return true and we are on our
	 * way. Otherwise, we had no contention for the fd and may need to
	 * open or re-open it.
	 */
	LogFullDebug(COMPONENT_FSAL,
		     "Open mode = %x, desired mode = %x",
		     (int) fsal_fd->openflags,
		     (int) openflags);

	if (!open_correct(fsal_fd->openflags, openflags)) {
		bool can_start = false;

		if (retried || cant_reopen(fsal_fd, may_open, may_reopen)) {
			/* fsal_fd is in wrong mode and we aren't
			 * allowed to reopen, so return EBUSY.
			 * OR we've already been here.
			 */
			/* Use fsalstat to avoid LogInfo... */
			status = fsalstat(ERR_FSAL_DELAY, EBUSY);

			/* We no longer need to claim io work on fsal_fd
			 * because we are going to continue with a temporary fd.
			 */
			LogFullDebug(COMPONENT_FSAL,
				     "%p we don't need to claim io_work (-1) = %"
				     PRIi32" fd_work = %"PRIi32,
				     fsal_fd,
				     atomic_fetch_int32_t(&fsal_fd->io_work)-1,
				     atomic_fetch_int32_t(&fsal_fd->fd_work));

			if (PTHREAD_MUTEX_dec_int32_t_and_lock(
							&fsal_fd->io_work,
							&fsal_fd->work_mutex)) {
				/* Let the thread waiting to do fd work know it
				 * can proceed.
				 */
				PTHREAD_COND_signal(&fsal_fd->fd_work_cond);
				PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);
			}

			goto out;
		}

		/* Indicate we want to do fd work */
		atomic_inc_int32_t(&fsal_fd->fd_work);

		LogFullDebug(COMPONENT_FSAL,
			     "%p back off io_work (-1) = %"PRIi32
			     " fd_work = %"PRIi32,
			     fsal_fd,
			     atomic_fetch_int32_t(&fsal_fd->io_work) - 1,
			     atomic_fetch_int32_t(&fsal_fd->fd_work));

		if (PTHREAD_MUTEX_dec_int32_t_and_lock(&fsal_fd->io_work,
						       &fsal_fd->work_mutex)) {
			/* We can proceed, we hold the work_mutex. */
			can_start = true;
		} else {
			/* need the mutex anyway... */
			PTHREAD_MUTEX_lock(&fsal_fd->work_mutex);
		}

		status = reopen_fsal_fd(obj_hdl, openflags, fsal_fd, can_start);

		PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);

		if (FSAL_IS_ERROR(status))
			goto out;

		/* Now that we have the file open, we need to try and start i/o
		 * again since we dropped our io_work count we might have to
		 * wait for more fd work.
		 *
		 * Note that reopen_fsal_fd opens with the union of desired
		 * modes so we should be good here.
		 *
		 * Also note we've already been here so we don't get stuck in an
		 * infinite loop in case something is trying to close the fd.
		 */
		retried = true;
		goto retry;
	}

	/* Now we have the file open in the correct mode, and I/O has started.
	 * Allow our caller to proceed.
	 */

out:

	if (openflags & FSAL_O_READ) {
		/* Indicate we want to read */
		atomic_dec_int32_t(&fsal_fd->want_read);
	}

	if (openflags & FSAL_O_WRITE) {
		/* Indicate we want to read */
		atomic_dec_int32_t(&fsal_fd->want_write);
	}

	return status;
}

/**
 * @brief Start I/O on the global fd for the object.
 *
 * If the global fd is busy, tmp_fd will be opened to be used for the I/O.
 *
 * This function assures that the fd is open in the mode requested. If
 * the fd was already open, it closes it and reopens with the OR of the
 * requested modes.
 *
 * Optionally, out_fd can be NULL in which case a file is not actually
 * opened, in this case, all that actually happens is the share reservation
 * check.
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
 * @param[in,out] out_fd   File descriptor that is to be used
 * @param[in]  obj_hdl     File on which to operate
 * @param[in]  my_fd       The file descriptor associated with the object
 * @param[in]  tmp_fd      A temporary file descriptor
 * @param[in]  openflags   Mode for open
 * @param[in]  bypass      Indicates to bypass share_deny_read and
 *                         share_deny_write but not share_deny_write_mand
 * @param[in]  share       The fsal_share associated with the object
 *
 * @return FSAL status.
 */

fsal_status_t fsal_start_global_io(struct fsal_fd **out_fd,
				   struct fsal_obj_handle *obj_hdl,
				   struct fsal_fd *my_fd,
				   struct fsal_fd *tmp_fd,
				   fsal_openflags_t openflags,
				   bool bypass,
				   struct fsal_share *share)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	bool open_any = openflags == FSAL_O_ANY;

	if (!open_any && share != NULL) {
		status = check_share_conflict_and_update_locked(obj_hdl,
								share,
								FSAL_O_CLOSED,
								openflags,
								bypass);

		if (FSAL_IS_ERROR(status)) {
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			LogDebug(COMPONENT_FSAL,
				 "check_share_conflict_and_update_locked failed with %s",
				 fsal_err_txt(status));
			return status;
		}
	}

	status = wait_to_start_io(obj_hdl, my_fd, openflags, !open_any,
				  !open_any);

	if (status.major == ERR_FSAL_DELAY) {
		/* We can't use the global fd, use the tmp_fd. We don't need
		 * any synchronization as the tmp_fd is private to the
		 * particular operation.
		 */
		status = fsal_reopen_fd(obj_hdl,
					open_any ? FSAL_O_READ : openflags,
					tmp_fd);

		if (!FSAL_IS_ERROR(status))
			tmp_fd->close_on_complete = true;

		*out_fd = tmp_fd;
	} else {
		*out_fd = my_fd;
	}

	if (!FSAL_IS_ERROR(status))
		return status;

	/* Can't proceed... */
	LogDebug(COMPONENT_FSAL,
		 "%s failed with %s",
		 *out_fd == my_fd
			? "wait_to_start_io"
			: "fsal_reopen_fd",
		 fsal_err_txt(status));

	if (!open_any && share != NULL) {
		/* Release the share reservation now by updating the counters.
		 */
		update_share_counters_locked(obj_hdl, share,
					     openflags, FSAL_O_CLOSED);
	}

	*out_fd = NULL;

	return status;
}

/**
 * @brief Indicate to start I/O on an object, returning a usable fsal_fd.
 *
 * This function will find a usable fsal_fd to operate on and protect it
 * for I/O. If necessary, the fsal_fd will be opened or re-opend which may
 * entail waiting for io_work to complete. If a lock state is provided, there
 * is an option to return the fsal_fd from the open state instead, in which
 * case reusing_open_state_fd will be set true if the pointer is not NULL.
 *
 * Unlike the previous implemnentation of fsal_find_fd, this implementation does
 * not handle share reservation only requests and out_fd MUST be non-NULL.
 *
 * Note that FSAL_O_ANY may be passed on to fsal_start_global_io, see the
 * documentation of that function for the implications.
 *
 * @param[in,out] out_fd         File descriptor that is to be used
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     obj_fd         The file descriptor associated with the object
 * @param[in]     tmp_fd         A temporary file descriptor
 * @param[in]     state          state_t to use for this operation
 * @param[in]     openflags      Mode for open
 * @param[in]     open_for_locks Indicates file is open for locks
 * @param[out]    reusing_open_state_fd Indicates whether already opened fd
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in]     share          The fsal_share associated with the object
 *
 * @return FSAL status.
 */

fsal_status_t fsal_start_io(struct fsal_fd **out_fd,
			    struct fsal_obj_handle *obj_hdl,
			    struct fsal_fd *obj_fd,
			    struct fsal_fd *tmp_fd,
			    struct state_t *state,
			    fsal_openflags_t openflags,
			    bool open_for_locks,
			    bool *reusing_open_state_fd,
			    bool bypass,
			    struct fsal_share *share)
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

	/* We don't want to open it if it's not yet open, see below... Note that
	 * the fd associated with a non-lock state_t will already be open.
	 *
	 * Thus the return should be ERR_FSAL_DELAY because the fd is not yet
	 * open OR we have success because the fd is open and useable.
	 */
	/** @todo FSF - oops, what about a delegation?
	 */
	status = wait_to_start_io(obj_hdl, state_fd, openflags, false, false);

	if (FSAL_IS_SUCCESS(status)) {
		/* It was valid, return it.
		 * Since we found a valid fd in the state, no need to
		 * check deny modes.
		 */
		LogFullDebug(COMPONENT_FSAL, "Use state_fd %p", state_fd);

		if (out_fd)
			*out_fd = state_fd;

		return status;
	}

	LogFullDebug(COMPONENT_FSAL,
		     "wait_to_start_io failed returned %s",
		     fsal_err_txt(status));

	assert(status.major == ERR_FSAL_DELAY);

	if (open_for_locks) {
		/* This is being opened for locks, we will not be able to
		 * re-open so open for read/write. If that fails permission
		 * check and openstate is available, retry with that state's
		 * access mode.
		 */
		status = wait_to_start_io(obj_hdl, state_fd, FSAL_O_RDWR,
					  true, false);

		if (status.major == ERR_FSAL_ACCESS &&
		    state->state_type == STATE_TYPE_LOCK) {
			/* Got an EACCESS and openstate may be available, try
			 * again with it's openflags. Otherwise leave the
			 * access error as is.
			 */
			struct state_t *openstate;

			/** @todo FSF - interesting question - what do we do if
			 *              openstate is being re-opened??? I think
			 *              we may also need to start I/O on the
			 *              open state while we do this reopen of
			 *              the lock state.
			 *
			 *              Partial answer - operations on a
			 *              stateid SHOULD be serialized. If we are
			 *              opening for locks, it's the first lock,
			 *              so the caller used the open stateid.
			 */

			openstate = nfs4_State_Get_Pointer(
					state->state_data.lock.openstate_key);

			if (openstate != NULL) {
				struct fsal_fd *related_fd;

				related_fd = (struct fsal_fd *) (openstate + 1);

				status = wait_to_start_io(
					obj_hdl, state_fd,
					related_fd->openflags & FSAL_O_RDWR,
					true, false);

				dec_state_t_ref(openstate);
			}
		} else if (status.major == ERR_FSAL_DELAY) {
			/* Try with actual openflags which SHOULD succeed. */
			status = wait_to_start_io(obj_hdl, state_fd, openflags,
						  false, false);
			if (status.major == ERR_FSAL_DELAY) {
				LogCrit(COMPONENT_FSAL,
					"Conflicting open, can not re-open fd with locks");
				status = posix2fsal_status(EINVAL);
			}
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

		return status;
	}

	/* At this point, we have a state_t we are trying to use in a wrong
	 * mode or it isn't open at all. If it's a lock state with an associated
	 * open state, use the fd from that (this mode is used by some FSALs
	 * that don't want to open a separate fd for lock states).
	 */
	if (state->state_type == STATE_TYPE_LOCK) {
		struct state_t *openstate;
		struct fsal_fd *related_fd;

		openstate = nfs4_State_Get_Pointer(
					state->state_data.lock.openstate_key);

		if (openstate == NULL) {
			/* The open state was not usable so use the global fd.
			 */
			goto global;
		}

		related_fd = (struct fsal_fd *) (openstate + 1);

		LogFullDebug(COMPONENT_FSAL,
			     "related_fd->openflags = %d openflags = %d",
			     related_fd->openflags, openflags);

		status = wait_to_start_io(obj_hdl, related_fd, openflags,
					  false, false);

		/* We either succeeded in starting I/O on the openstate's
		 * fsal_fd, in which case the fsal_fd will NOT go away while we
		 * are using it so we can safely drop the reference on
		 * openstate.
		 *
		 * If we failed to use the openstate's fsal_fd then we don't
		 * need the reference.
		 */
		dec_state_t_ref(openstate);

		if (FSAL_IS_SUCCESS(status)) {
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
				if (reusing_open_state_fd != NULL)
					*reusing_open_state_fd = true;
			}

			return status;
		}
	}

	/* At this point, we have an ERR_FSAL_DELAY because an open or
	 * delegation state was being used the wrong way. This should only
	 * happen for READs where we always allow a READ on a write-only open
	 * or delegation. The READ will proceed effectively as an anonymous READ
	 * including all share reservation activity including being blocked by
	 * deny read.
	 *
	 * Note that we do not allow write locks on read-only opens and read
	 * locks on write only opens.
	 */

 global:

	/* No useable state_t so return the global file descriptor. */
	LogFullDebug(COMPONENT_FSAL,
		     "Use global fd openflags = %x",
		     openflags);

	/* Make sure global is open as necessary otherwise return a
	 * temporary file descriptor. Check share reservation if not
	 * opening FSAL_O_ANY. If we were passed a state, then we won't
	 * need to check share reservation.
	 */
	return fsal_start_global_io(out_fd, obj_hdl, obj_fd, tmp_fd, openflags,
				    bypass, state == NULL ? share : NULL);
}

fsal_status_t fsal_complete_io(struct fsal_obj_handle *obj_hdl,
			       struct fsal_fd *fsal_fd)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};
	bool got_mutex;

	if (fsal_fd->close_on_complete) {
		LogFullDebug(COMPONENT_FSAL, "closing temp fd %p", fsal_fd);
		/* Close the temp fd, no need to do the rest since this fsal_fd
		 * was only being used for this operation.
		 */
		return fsal_close_fd(obj_hdl, fsal_fd);
	}

	/* Indicate I/O done, and if we were last I/O, signal fd_work_cond
	 * condition in case any threads are waiting to do fd work.
	 */
	LogFullDebug(COMPONENT_FSAL,
		     "%p done io_work (-1) = %"PRIi32" fd_work = %"PRIi32,
		     fsal_fd,
		     atomic_fetch_int32_t(&fsal_fd->io_work) - 1,
		     atomic_fetch_int32_t(&fsal_fd->fd_work));

	got_mutex = PTHREAD_MUTEX_dec_int32_t_and_lock(&fsal_fd->io_work,
						       &fsal_fd->work_mutex);

	if (got_mutex)
		PTHREAD_COND_signal(&fsal_fd->fd_work_cond);

	/* We choose to bump the fd at the completion of I/O so we don't have
	 * to introduce new locking.
	 */
	bump_fd_lru(fsal_fd);

	if (got_mutex)
		PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);

	return status;
}

/**
 * @brief Manage starting fsal_fd fd work (open/close).
 *
 * NOTE: This function takes the fsal_fd work_mutex. It should be used in
 *       conjunction with fsal_complete_fd_work.
 *
 * @param[in]  fsal_fd        The fd to do work on.
 * @param[in]  is_reclaiming  Indicates we are closing files to reclaim fd
 *
 */

fsal_status_t fsal_start_fd_work(struct fsal_fd *fsal_fd, bool is_reclaiming)
{
	/* Indicate we want to do fd work */
	atomic_inc_int32_t(&fsal_fd->fd_work);

	PTHREAD_MUTEX_lock(&fsal_fd->work_mutex);

	if ((atomic_fetch_int32_t(&fsal_fd->want_read) != 0 ||
	     atomic_fetch_int32_t(&fsal_fd->want_write) != 0) &&
	    is_reclaiming) {
		/* I/O is trying to start but needs to re-open, and we're trying
		 * to reclaim, this isn't a good candidate to reclaim, bump it
		 * so we can skip it.
		 */
		bump_fd_lru(fsal_fd);
		fsal_complete_fd_work(fsal_fd);
		/* Use fsalstat to avoid LogInfo... */
		return fsalstat(ERR_FSAL_DELAY, EBUSY);
	}

	LogFullDebug(COMPONENT_FSAL,
		     "%p try fd work - io_work = %"PRIi32" fd_work = %"PRIi32,
		     fsal_fd,
		     atomic_fetch_int32_t(&fsal_fd->io_work),
		     atomic_fetch_int32_t(&fsal_fd->fd_work));

	/* Wait for lull in io work */
	while (atomic_fetch_int32_t(&fsal_fd->io_work) != 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "%p wait for lull - io_work = %"PRIi32
			     " fd_work = %"PRIi32,
			     fsal_fd,
			     atomic_fetch_int32_t(&fsal_fd->io_work),
			     atomic_fetch_int32_t(&fsal_fd->fd_work));

		if (is_reclaiming) {
			/* We are trying to reclaim an in-use fsal_fd, bump
			 * it so we skip it.
			 */
			bump_fd_lru(fsal_fd);
			fsal_complete_fd_work(fsal_fd);
			/* Use fsalstat to avoid LogInfo... */
			return fsalstat(ERR_FSAL_DELAY, EBUSY);
		}

		/* io work is in progress or trying to start, wait for it to
		 * complete (or not start)
		 */
		PTHREAD_COND_wait(&fsal_fd->fd_work_cond, &fsal_fd->work_mutex);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Manage completeion of fsal_fd fd work (open/close).
 *
 * NOTE: This function releases the fsal_fd work_mutex. It should be used in
 *       conjunction with fsal_start_fd_work.
 *
 * @param[in]  fsal_fd        The fd to do work on.
 *
 */

void fsal_complete_fd_work(struct fsal_fd *fsal_fd)
{
	/* Indicate we are done with fd work and signal any waiters. */
	atomic_dec_int32_t(&fsal_fd->fd_work);

	LogFullDebug(COMPONENT_FSAL,
		     "%p done fd work io_work = %"PRIi32" fd_work = %"PRIi32,
		     fsal_fd,
		     atomic_fetch_int32_t(&fsal_fd->io_work),
		     atomic_fetch_int32_t(&fsal_fd->fd_work));

	/* Wake up at least one thread waiting to do fd work */
	PTHREAD_COND_signal(&fsal_fd->fd_work_cond);

	/* Wake up all threads waiting to do io work */
	PTHREAD_COND_broadcast(&fsal_fd->io_work_cond);

	/* Completely done, release the mutex. */
	PTHREAD_MUTEX_unlock(&fsal_fd->work_mutex);
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
				LogEventLimited(COMPONENT_FSAL,
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

/**
 * @brief Set an export into an op_context (could be NULL).
 *
 * This is the core function that sets up an op_context. It makes no assumptions
 * of what is already in the op_context. It will set up a refstr for both
 * op_ctx->ctx_fullpath and op_ctx->ctx_pseudopath. If no export is set, or
 * those strings are not available from the export, the refstr will reference
 * the special "no_export" refstr.
 *
 * @param[in] exp       The gsh_export to set, can be NULL.
 * @param[in] fsal_exp  The fsal_export to set, can be NULL.
 * @param[in] pds       The pnfs ds to set, can be NULL.
 */
static void set_op_context_export_fsal_no_release(struct gsh_export *exp,
						  struct fsal_export *fsal_exp,
						  struct fsal_pnfs_ds *pds)
{
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

/** @brief Remove the current export from the op_context so the op_context has
 *         no export.
 *
 * If an export is referenced by the op_context, the reference will be
 * dropped.
 *
 * If the op_context had a valid ctx_fullpath or ctx_pseudopath they will
 * be unreferenced but no new refstr will be attached.
 *
 * This function should only be called from functions that will either be
 * freeing the op_context, or will be overwriting the export data.
 *
 * Functions that are just removing the export data, but not freeing the
 * op_context or overwriting the export data should call
 * clear_op_context_export() instead.
 *
 * The following callers do call this function directly for the reasons
 * explained below.
 *
 * When called by set_op_context_export() or set_op_context_pnfs_ds(),
 * a new export (even if NULL) is about to be set.
 *
 * When called by release_op_context(), we are "destructing" the op_context.
 *
 * When called by restore_op_context_export(), we are replacing with the saved
 * op_context so all the references that had been saved will be restored.
 *
 * No parameters, operates on the op_context directly.
 *
 */
static inline void clear_op_context_export_impl(void)
{
	if (op_ctx->ctx_export != NULL)
		put_gsh_export(op_ctx->ctx_export);

	if (op_ctx->ctx_pnfs_ds != NULL)
		pnfs_ds_put(op_ctx->ctx_pnfs_ds);

	/* Note these are NEVER NULL in an active op_context. When an op_context
	 * is initialized, if there is no export, these strings will be set to
	 * reference the special "no_export" string. These refstr will ONLY be
	 * set to NULL when release_op_context is called to "destroy" the
	 * op_context.
	 */
	gsh_refstr_put(op_ctx->ctx_fullpath);
	gsh_refstr_put(op_ctx->ctx_pseudopath);
}

/**
 * @brief Remove the current export from the op_context so the op_context
 *        has no export.
 *
 * If an export is referenced by the op_context, the reference will be
 * dropped.
 *
 * If the op_context had a valid ctx_fullpath or ctx_pseudopath they will
 * be unreferenced and references to the "no_export" string will be attached.
 *
 * No parameters, operates on the op_context directly.
 *
 */
void clear_op_context_export(void)
{
	clear_op_context_export_impl();

	/* Clear the ctx_export and fsal_export */
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;

	/* An active op context will always have refstr */
	op_ctx->ctx_fullpath = gsh_refstr_get(no_export);
	op_ctx->ctx_pseudopath = gsh_refstr_get(no_export);
}

/**
 * @brief Set an export into the op_context.
 *
 * @param[in] exp       The gsh_export to set, can be NULL.
 *
 * Will set a NULL pnfs ds and sets fsal_exp from exp.
 *
 */
void set_op_context_export(struct gsh_export *exp)
{
	struct fsal_export *fsal_exp = exp ? exp->fsal_export : NULL;

	clear_op_context_export_impl();

	set_op_context_export_fsal_no_release(exp, fsal_exp, NULL);
}

/**
 * @brief Set a pnfs ds into the op_context.
 *
 * @param[in] pds       The pnfs ds to set, can be NULL.
 *
 * Will set export and fsal_export from pnfs ds.
 *
 */
void set_op_context_pnfs_ds(struct fsal_pnfs_ds *pds)
{
	clear_op_context_export_impl();

	set_op_context_export_fsal_no_release(pds->mds_export,
					      pds->mds_fsal_export,
					      pds);
}

/**
 * @brief Save the op_context export and all its references.
 *
 * This is used when the caller will re-use the op_context to perform operations
 * on other exports, and will then want to restore the original export.
 *
 * @param[in] saved  Pointer to a saved_export_context to save the data into.
 *
 */
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

/**
 * @brief Save the op_context export and all its references and then set a new
 *        export.
 *
 * @param[in] saved  Pointer to a saved_export_context to save the data into.
 * @param[in] exp    The new export to set.
 */
void save_op_context_export_and_set_export(struct saved_export_context *saved,
					   struct gsh_export *exp)
{
	save_op_context_export(saved);

	/* Don't release op_ctx->ctx_export or the refstr since it's saved */
	set_op_context_export_fsal_no_release(exp, exp->fsal_export, NULL);
}

/**
 * @brief Save the op_context export and all its references and then clear the
 *        export from the op_context.
 *
 * This is used when the caller will re-use the op_context to perform operations
 * on other exports, and will then want to restore the original export.
 *
 * @param[in] saved  Pointer to a saved_export_context to save the data into.
 *
 */
void save_op_context_export_and_clear(struct saved_export_context *saved)
{
	save_op_context_export(saved);
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;
	op_ctx->ctx_pnfs_ds = NULL;

	/* An active op context will always have refstr but the original
	 * refstr are saved off, so replace with new refs to no_export.
	 */
	op_ctx->ctx_fullpath = gsh_refstr_get(no_export);
	op_ctx->ctx_pseudopath = gsh_refstr_get(no_export);
}

/**
 * @brief Restore a saved op_context export.
 *
 * @param[in] saved  Pointer to a saved_export_context to restore the data from.
 *
 */
void restore_op_context_export(struct saved_export_context *saved)
{
	/* Since we are about to restore op_ctx->ctx_fullpath and
	 * op_ctx->ctx_pseudopath, we don't want to fill them in with
	 * references to the no_export gsh_refstr.
	 */
	clear_op_context_export_impl();
	op_ctx->ctx_export = saved->saved_export;
	op_ctx->ctx_fullpath = saved->saved_fullpath;
	op_ctx->ctx_pseudopath = saved->saved_pseudopath;
	op_ctx->fsal_export = saved->saved_fsal_export;
	op_ctx->fsal_module = saved->saved_fsal_module;
	op_ctx->ctx_pnfs_ds = saved->saved_pnfs_ds;
	op_ctx->export_perms = saved->saved_export_perms;
}

/**
 * @brief Discard the saved export data from an op_context.
 *
 * This is used when the caller determines it does not need to restore the
 * saved op_context export data.
 *
 * @param[in] saved  Pointer to a saved_export_context to discard the data from.
 *
 */
void discard_op_context_export(struct saved_export_context *saved)
{
	if (saved->saved_export)
		put_gsh_export(saved->saved_export);

	if (saved->saved_pnfs_ds != NULL)
		pnfs_ds_put(saved->saved_pnfs_ds);

	if (saved->saved_fullpath != NULL)
		gsh_refstr_put(saved->saved_fullpath);

	if (saved->saved_pseudopath != NULL)
		gsh_refstr_put(saved->saved_pseudopath);
}

/**
 * @brief Initialize an op_context.
 *
 * This initializes all the fields in an op_context including setting up all the
 * references if an export is provided. After this call, the op_context is
 * totally valid even if NULL was passed for the export.
 *
 * If the thread's op_ctx is non-NULL, that will be saved in ctx->saved_op_ctx.
 *
 * The thread's op_ctx will be set to ctx.
 *
 * The export permissions and options will be set to root defaults.
 *
 * This is basically the "constructor" for an op_context.
 *
 * @param[in] ctx            The op_context to initialoze
 * @param[in] exp            The gsh_export if any to reference
 * @param[in] fsal_exp       The fsal_export if any to reference
 * @param[in] caller_data    The caller address data, can be NULL
 * @param[in] nfs_vers       The NFS version
 * @param[in] nfs_minorvers  The minor version for 4.x
 * @param[in] req_type       UNKNOWN_REQUEST, NFS_REQUEST, _9P_REQUEST, etc.
 *
 */
static uint32_t op_id;
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
	set_op_context_export_fsal_no_release(exp, fsal_exp, NULL);

	ctx->export_perms.set = root_op_export_set;
	ctx->export_perms.options = root_op_export_options;
	ctx->op_id = atomic_postadd_uint32_t(&op_id, 1);
	ctx->flags.pseudo_fsal_internal_lookup = false;
}

/**
 * @brief Release an op_context and its references.
 *
 * This will release the thread's op_ctx and restore any previous one.
 *
 * The op_context will be set such that all the export bits are NULL and all
 * references released.
 *
 * This is basically the "destructor" for an op_context.
 *
 */
void release_op_context(void)
{
	struct req_op_context *cur_ctx = op_ctx;

	clear_op_context_export_impl();

	/* Clear the ctx_export and fsal_export */
	op_ctx->ctx_export = NULL;
	op_ctx->fsal_export = NULL;

	/* And now we're done with the gsh_refstr (the refs just got released so
	 * we really can NULL them out. This op_context is now done for.
	 * This is analogous to a destructor.
	 */
	op_ctx->ctx_fullpath = NULL;
	op_ctx->ctx_pseudopath = NULL;

	/* And restore the saved op_ctx */
	op_ctx = op_ctx->saved_op_ctx;
	cur_ctx->saved_op_ctx = NULL;
}

/**
 * @brief Suspend an op_context so the thread may suspend and the operation be
 *        resumed later.
 *
 * When a thread is processing an async request, the op_context must be
 * preserved to resume that request. The request actually contains the
 * op_context, so all this does is set the thread's op_ctx to NULL.
 *
 * NOTE: A suspended op_context better be a top level one, i.e. saved_op_ctx is
 *       NULL. That will be asserted by resume_op_context().
 */
void suspend_op_context(void)
{
	/* We cannot touch the contents of op_ctx, because it may be already
	 * freed by the async callback.  Just NULL out op_ctx here.  */
	op_ctx = NULL;
}

/**
 * @brief Resume an op_context that had been suspended.
 *
 * When the request resumes, the op_context will be restored. The current
 * op_ctx will be saved.
 *
 * The resume will also reset the client IP address string.
 *
 * NOTE: The caller must not have an op_ctx in use. Any op_context that may be
 *       suspended and resumed MUST be a top level op_context with a NULL
 *       saved_op_ctx. This is because a resumed op_context may be suspended
 *       again, and suspend has no way to revert to a saved_op_ctx.
 *
 */
void resume_op_context(struct req_op_context *ctx)
{
	assert(ctx->saved_op_ctx == NULL);
	assert(op_ctx == NULL);
	op_ctx = ctx;

	if (op_ctx->client != NULL) {
		/* Set the Client IP for this thread */
		SetClientIP(op_ctx->client->hostaddr_str);
	}
}

#ifdef USE_DBUS
void fd_usage_summarize_dbus(DBusMessageIter *iter)
{
	DBusMessageIter struct_iter;
	char *type;
	uint32_t global_fds, state_fds;
	uint32_t fd_limit, fd_state;
	uint32_t fds_lowat, fds_hiwat, fds_hard_limit;
	uint64_t v4_open_states_count;

	dbus_message_iter_open_container(iter, DBUS_TYPE_STRUCT, NULL,
					 &struct_iter);
	/* Gather the data */
	fd_limit = atomic_fetch_uint32_t(&fd_lru_state.fds_system_imposed);
	fds_lowat = atomic_fetch_uint32_t(&fd_lru_state.fds_lowat);
	fds_hiwat = atomic_fetch_uint32_t(&fd_lru_state.fds_hiwat);
	fds_hard_limit = atomic_fetch_uint32_t(&fd_lru_state.fds_hard_limit);
	fd_state = atomic_fetch_uint32_t(&fd_lru_state.fd_state);

	global_fds = atomic_fetch_int32_t(&fsal_fd_global_counter);
	state_fds = atomic_fetch_uint32_t(&fsal_fd_state_counter);

	/* Gets open v4 FD counts by traversing all the HT
	 * to get all clientIDs and count their open states
	 */
	v4_open_states_count = get_total_count_of_open_states();

	type = "System limit on FDs";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &fd_limit);

	type = "FD Low WaterMark";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &fds_lowat);

	type = "FD High WaterMark";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &fds_hiwat);

	type = "FD Hard Limt";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &fds_hard_limit);

	type = "FD usage";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	switch (fd_state) {
	case FD_LOW:
		type = "        Below Low Water Mark ";
		break;
	case FD_MIDDLE:
		type = "        Below High Water Mark ";
		break;
	case FD_HIGH:
		type = "        Above High Water Mark ";
		break;
	case FD_LIMIT:
		type = "        Hard Limit reached ";
		break;
	}
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);

	type = "FSAL opened Global FD count";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &global_fds);

	type = "FSAL opened State FD count";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT32,
				       &state_fds);

	type = "NFSv4 open state count";
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_STRING, &type);
	dbus_message_iter_append_basic(&struct_iter, DBUS_TYPE_UINT64,
				       &v4_open_states_count);

	dbus_message_iter_close_container(iter, &struct_iter);
}
#endif /* USE_DBUS */

/** @} */
