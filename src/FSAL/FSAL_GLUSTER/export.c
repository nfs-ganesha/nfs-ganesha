/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Red Hat  Inc., 2013
 * Author: Anand Subramanian anands@redhat.com
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @file  export.c
 * @author Shyamsundar R <srangana@redhat.com>
 * @author Anand Subramanian <anands@redhat.com>
 *
 * @brief GLUSTERFS FSAL export object
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include "fsal.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "config_parsing.h"
#include "gluster_internal.h"
#include "nfs_exports.h"
#include "export_mgr.h"

/**
 * @brief Implements GLUSTER FSAL exportoperation release
 */

static void export_release(struct fsal_export *exp_hdl)
{
	struct glusterfs_export *glfs_export =
	    container_of(exp_hdl, struct glusterfs_export, export);

	/* check activity on the export */

	/* detach the export */
	fsal_detach_export(glfs_export->export.fsal,
			   &glfs_export->export.exports);
	free_export_ops(&glfs_export->export);

	/* Gluster and memory cleanup */
	glfs_fini(glfs_export->gl_fs);
	glfs_export->gl_fs = NULL;
	gsh_free(glfs_export->export_path);
	glfs_export->export_path = NULL;
	gsh_free(glfs_export);
	glfs_export = NULL;
}

/**
 * @brief Implements GLUSTER FSAL exportoperation lookup_path
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
				 const char *path,
				 struct fsal_obj_handle **pub_handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	char *realpath = NULL;
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(export_pub, struct glusterfs_export, export);
	char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};

	LogFullDebug(COMPONENT_FSAL, "In args: path = %s", path);

	*pub_handle = NULL;

	if (strcmp(path, glfs_export->mount_path) == 0) {
		realpath = strdup(glfs_export->export_path);
	} else {
		/*
		 *  mount path is not same as the exported one. Should be subdir
		 *  then.
		 *  TODO: How do we handle symlinks if present in the path.
		 */
		realpath = malloc(strlen(glfs_export->export_path) +
				  strlen(path) + 1);
		if (realpath) {
			/*
			 * Handle the case wherein glfs_export->export_path
			 * is root i.e, '/' separately.
			 */
			if (strlen(glfs_export->export_path) != 1) {
				strcpy(realpath, glfs_export->export_path);
				strcpy((realpath +
					strlen(glfs_export->export_path)),
					&path[strlen(glfs_export->mount_path)]);
			} else {
				strcpy(realpath,
					&path[strlen(glfs_export->mount_path)]);
			}
		}
	}
	if (!realpath) {
		errno = ENOMEM;
		status = gluster2fsal_error(errno);
		goto out;
	}

	glhandle = glfs_h_lookupat(glfs_export->gl_fs, NULL, realpath, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*pub_handle = &objhandle->handle;

	if (realpath)
		free(realpath);

	return status;
 out:
	gluster_cleanup_vars(glhandle);
	if (realpath)
		free(realpath);

	return status;
}

/**
 * @brief Implements GLUSTER FSAL exportoperation extract_handle
 */

static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc)
{
	size_t fh_size;
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	/* sanity checks */
	if (!fh_desc || !fh_desc->addr)
		return fsalstat(ERR_FSAL_FAULT, 0);

	fh_size = GLAPI_HANDLE_LENGTH;
	if (fh_desc->len != fh_size) {
		LogMajor(COMPONENT_FSAL,
			 "Size mismatch for handle.  should be %lu, got %lu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	fh_desc->len = fh_size;	/* pass back the actual size */

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_extract_handle);
#endif
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation create_handle
 */

static fsal_status_t create_handle(struct fsal_export *export_pub,
				   struct gsh_buffdesc *fh_desc,
				   struct fsal_obj_handle **pub_handle)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct stat sb;
	struct glfs_object *glhandle = NULL;
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	struct glusterfs_export *glfs_export =
	    container_of(export_pub, struct glusterfs_export, export);
	char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	*pub_handle = NULL;

	if (fh_desc->len != GLAPI_HANDLE_LENGTH) {
		status.major = ERR_FSAL_INVAL;
		goto out;
	}

	/* First 16bytes contain volume UUID. globjhdl is in the second */
	/* half(16bytes) of the fs_desc->addr.  */
	memcpy(globjhdl, fh_desc->addr+GLAPI_UUID_LENGTH, GFAPI_HANDLE_LENGTH);

	glhandle =
	    glfs_h_create_from_handle(glfs_export->gl_fs, globjhdl,
				      GFAPI_HANDLE_LENGTH, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs, vol_uuid, GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	rc = construct_handle(glfs_export, &sb, glhandle, globjhdl,
			      GLAPI_HANDLE_LENGTH, &objhandle, vol_uuid);
	if (rc != 0) {
		status = gluster2fsal_error(rc);
		goto out;
	}

	*pub_handle = &objhandle->handle;
 out:
	if (status.major != ERR_FSAL_NO_ERROR)
		gluster_cleanup_vars(glhandle);
#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_create_handle);
#endif
	return status;
}

/**
 * @brief Implements GLUSTER FSAL exportoperation get_fs_dynamic_info
 */

static fsal_status_t get_dynamic_info(struct fsal_export *exp_hdl,
				      struct fsal_obj_handle *obj_hdl,
				      fsal_dynamicfsinfo_t *infop)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct statvfs vfssb;
	struct glusterfs_export *glfs_export =
	    container_of(exp_hdl, struct glusterfs_export, export);

	rc = glfs_statvfs(glfs_export->gl_fs, glfs_export->export_path, &vfssb);
	if (rc != 0)
		return gluster2fsal_error(rc);

	memset(infop, 0, sizeof(fsal_dynamicfsinfo_t));
	infop->total_bytes = vfssb.f_frsize * vfssb.f_blocks;
	infop->free_bytes = vfssb.f_frsize * vfssb.f_bfree;
	infop->avail_bytes = vfssb.f_frsize * vfssb.f_bavail;
	infop->total_files = vfssb.f_files;
	infop->free_files = vfssb.f_ffree;
	infop->avail_files = vfssb.f_favail;
	infop->time_delta.tv_sec = 1;
	infop->time_delta.tv_nsec = 0;

	return status;
}

/** @todo: We have gone POSIX way for the APIs below, can consider the CEPH way
 * in case all are constants across all volumes etc. */

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_supports
 */

static bool fs_supports(struct fsal_export *exp_hdl,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_supports(info, option);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxfilesize
 */

static uint64_t fs_maxfilesize(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxfilesize(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxread
 */

static uint32_t fs_maxread(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxread(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxwrite
 */

static uint32_t fs_maxwrite(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxwrite(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxlink
 */

static uint32_t fs_maxlink(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxlink(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxnamelen
 */

static uint32_t fs_maxnamelen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxnamelen(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_maxpathlen
 */

static uint32_t fs_maxpathlen(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_maxpathlen(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_lease_time
 */

static struct timespec fs_lease_time(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_lease_time(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_acl_support
 */

static fsal_aclsupp_t fs_acl_support(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_acl_support(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_supported_attrs
 */

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_supported_attrs(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_umask
 */

static uint32_t fs_umask(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_umask(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_xattr_access_rights
 */

static uint32_t fs_xattr_access_rights(struct fsal_export *exp_hdl)
{
	struct fsal_staticfsinfo_t *info;

	info = gluster_staticinfo(exp_hdl->fsal);
	return fsal_xattr_access_rights(info);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation check_quota
 */
/*
static fsal_status_t check_quota(struct fsal_export *exp_hdl,
				 const char * filepath,
				 int quota_type,
				 struct req_op_context *req_ctx)
{
	return fsalstat(ERR_FSAL_NO_ERROR, 0) ;
}
*/
/**
 * @brief Implements GLUSTER FSAL exportoperation get_quota
 */
/*
static fsal_status_t get_quota(struct fsal_export *exp_hdl,
			       const char * filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t *pquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}
*/
/**
 * @brief Implements GLUSTER FSAL exportoperation set_quota
 */
/*
static fsal_status_t set_quota(struct fsal_export *exp_hdl,
			       const char *filepath,
			       int quota_type,
			       struct req_op_context *req_ctx,
			       fsal_quota_t * pquota,
			       fsal_quota_t * presquota)
{
	return fsalstat(ERR_FSAL_NOTSUPP, 0) ;
}
*/

/**
 * @brief Registers GLUSTER FSAL exportoperation vector
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */

void export_ops_init(struct export_ops *ops)
{
	ops->release = export_release;
	ops->lookup_path = lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
}

struct glexport_params {
	char *glvolname;
	char *glhostname;
	char *glvolpath;
	char *glfs_log;
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_MAND_STR("volume", 1, MAXPATHLEN, NULL,
		      glexport_params, glvolname),
	CONF_MAND_STR("hostname", 1, MAXPATHLEN, NULL,
		      glexport_params, glhostname),
	CONF_ITEM_PATH("volpath", 1, MAXPATHLEN, "/",
		      glexport_params, glvolpath),
	CONF_ITEM_PATH("glfs_log", 1, MAXPATHLEN, "/tmp/gfapi.log",
		       glexport_params, glfs_log),
	CONFIG_EOL
};


static struct config_block export_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.gluster-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/**
 * @brief Implements GLUSTER FSAL moduleoperation create_export
 */

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      void *parse_node,
				      struct config_error_type *err_type,
				      const struct fsal_up_vector *up_ops)
{
	int rc;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfsexport = NULL;
	glfs_t *fs = NULL;
	struct glexport_params params = {
		.glvolname = NULL,
		.glhostname = NULL,
		.glvolpath = NULL,
		.glfs_log = NULL};

	LogDebug(COMPONENT_FSAL, "In args: export path = %s",
		 op_ctx->export->fullpath);

	rc = load_config_from_node(parse_node,
				   &export_param,
				   &params,
				   true,
				   err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Incorrect or missing parameters for export %s",
			op_ctx->export->fullpath);
		status.major = ERR_FSAL_INVAL;
		goto out;
	}
	LogEvent(COMPONENT_FSAL, "Volume %s exported at : '%s'",
		 params.glvolname, params.glvolpath);

	glfsexport = gsh_calloc(1, sizeof(struct glusterfs_export));
	if (glfsexport == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export object.  Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	if (fsal_export_init(&glfsexport->export) != 0) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export ops vectors.  Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	export_ops_init(&glfsexport->export.exp_ops);
	glfsexport->export.up_ops = up_ops;

	fs = glfs_new(params.glvolname);
	if (!fs) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to create new glfs. Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	rc = glfs_set_volfile_server(fs, "tcp", params.glhostname, 24007);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to set volume file. Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	rc = glfs_set_logging(fs, params.glfs_log, 7);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to set logging. Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	rc = glfs_init(fs);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to initialize volume. Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	rc = fsal_attach_export(fsal_hdl, &glfsexport->export.exports);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach export. Export: %s",
			op_ctx->export->fullpath);
		goto out;
	}

	glfsexport->mount_path = op_ctx->export->fullpath;
	glfsexport->export_path = params.glvolpath;
	glfsexport->gl_fs = fs;
	glfsexport->saveduid = geteuid();
	glfsexport->savedgid = getegid();
	glfsexport->export.fsal = fsal_hdl;
	glfsexport->acl_enable =
		((op_ctx->export->export_perms.options &
		  EXPORT_OPTION_DISABLE_ACL) ? 0 : 1);

	op_ctx->fsal_export = &glfsexport->export;

 out:
	if (params.glvolname)
		gsh_free(params.glvolname);
	if (params.glhostname)
		gsh_free(params.glhostname);
	if (params.glfs_log)
		gsh_free(params.glfs_log);

	if (status.major != ERR_FSAL_NO_ERROR) {
		if (params.glvolpath)
			gsh_free(params.glvolpath);

		if (fs)
			glfs_fini(fs);

		if (glfsexport)
			gsh_free(glfsexport);
	}

	return status;
}
