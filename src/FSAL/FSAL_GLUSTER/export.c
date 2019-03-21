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
#include "FSAL/fsal_config.h"
#include "fsal_convert.h"
#include "config_parsing.h"
#include "gluster_internal.h"
#include "nfs_exports.h"
#include "export_mgr.h"
#include "pnfs_utils.h"
#include "sal_data.h"

/* The default location of gfapi log
 * if glfs_log param is not defined in
 * the export file */
#define GFAPI_LOG_LOCATION "/var/log/ganesha/ganesha-gfapi.log"

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

	glusterfs_free_fs(glfs_export->gl_fs);

	glfs_export->gl_fs = NULL;
	gsh_free(glfs_export->export_path);
	glfs_export->export_path = NULL;
	gsh_free(glfs_export->sec_label_xattr);
	glfs_export->sec_label_xattr = NULL;
	gsh_free(glfs_export);
	glfs_export = NULL;
}

/**
 * @brief Implements GLUSTER FSAL exportoperation lookup_path
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
				 const char *path,
				 struct fsal_obj_handle **pub_handle,
				 struct attrlist *attrs_out)
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
		realpath = gsh_strdup(glfs_export->export_path);
	} else {
		/*
		 *  mount path is not same as the exported one. Should be subdir
		 *  then.
		 */
		/** @todo: How do we handle symlinks if present in the path.
		 */
		realpath = gsh_malloc(strlen(glfs_export->export_path) +
				      strlen(path) + 1);
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

	glhandle = glfs_h_lookupat(glfs_export->gl_fs->fs, NULL, realpath,
				&sb, 1);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs->fs, vol_uuid,
			       GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	construct_handle(glfs_export, &sb, glhandle, globjhdl,
			 &objhandle, vol_uuid);

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&sb, attrs_out);
	}

	*pub_handle = &objhandle->handle;

	gsh_free(realpath);

	return status;
 out:
	gluster_cleanup_vars(glhandle);
	gsh_free(realpath);

	return status;
}

/**
 * @brief Implements GLUSTER FSAL exportoperation wire_to_host
 */

static fsal_status_t wire_to_host(struct fsal_export *exp_hdl,
				  fsal_digesttype_t in_type,
				  struct gsh_buffdesc *fh_desc,
				  int flags)
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
			 "Size mismatch for handle.  should be %zu, got %zu",
			 fh_size, fh_desc->len);
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	fh_desc->len = fh_size;	/* pass back the actual size */

#ifdef GLTIMING
	now(&e_time);
	latency_update(&s_time, &e_time, lat_wire_to_host);
#endif
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Implements GLUSTER FSAL exportoperation create_handle
 */

static fsal_status_t create_handle(struct fsal_export *export_pub,
				   struct gsh_buffdesc *fh_desc,
				   struct fsal_obj_handle **pub_handle,
				   struct attrlist *attrs_out)
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
	    glfs_h_create_from_handle(glfs_export->gl_fs->fs, globjhdl,
				      GFAPI_HANDLE_LENGTH, &sb);
	if (glhandle == NULL) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	rc = glfs_get_volumeid(glfs_export->gl_fs->fs, vol_uuid,
			       GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	construct_handle(glfs_export, &sb, glhandle, globjhdl,
			 &objhandle, vol_uuid);

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(&sb, attrs_out);
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
 * @brief Given a glfs_object handle, construct handle for
 * FSAL to use.
 */

fsal_status_t glfs2fsal_handle(struct glusterfs_export *glfs_export,
			       struct glfs_object *glhandle,
			       struct fsal_obj_handle **pub_handle,
			       struct stat *sb,
			       struct attrlist *attrs_out)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	unsigned char globjhdl[GFAPI_HANDLE_LENGTH] = {'\0'};
	struct glusterfs_handle *objhandle = NULL;
	char vol_uuid[GLAPI_UUID_LENGTH] = {'\0'};
#ifdef GLTIMING
	struct timespec s_time, e_time;

	now(&s_time);
#endif

	*pub_handle = NULL;

	if (!glfs_export || !glhandle) {
		status.major = ERR_FSAL_INVAL;
		goto out;
	}

	rc = glfs_h_extract_handle(glhandle, globjhdl, GFAPI_HANDLE_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}
	rc = glfs_get_volumeid(glfs_export->gl_fs->fs, vol_uuid,
				 GLAPI_UUID_LENGTH);
	if (rc < 0) {
		status = gluster2fsal_error(errno);
		goto out;
	}

	construct_handle(glfs_export, sb, glhandle, globjhdl,
			 &objhandle, vol_uuid);

	if (attrs_out != NULL) {
		posix2fsal_attributes_all(sb, attrs_out);
	}

	*pub_handle = &objhandle->handle;
 out:
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

	rc = glfs_statvfs(glfs_export->gl_fs->fs, glfs_export->export_path,
			  &vfssb);
	if (rc != 0)
		return gluster2fsal_error(errno);

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

/**
 * @brief Allocate a state_t structure
 *
 * Note that this is not expected to fail since memory allocation is
 * expected to abort on failure.
 *
 * @param[in] exp_hdl	       Export state_t will be associated with
 * @param[in] state_type	    Type of state to allocate
 * @param[in] related_state         Related state if appropriate
 *
 * @returns a state structure.
 */
struct state_t *glusterfs_alloc_state(struct fsal_export *exp_hdl,
				enum state_type state_type,
				struct state_t *related_state)
{
	struct state_t *state;
	struct glusterfs_fd *my_fd;

	state = init_state(gsh_calloc(1, sizeof(struct glusterfs_state_fd)),
			   exp_hdl, state_type, related_state);

	my_fd = &container_of(state, struct glusterfs_state_fd,
			      state)->glusterfs_fd;

	my_fd->glfd = NULL;
	my_fd->openflags = FSAL_O_CLOSED;
	PTHREAD_RWLOCK_init(&my_fd->fdlock, NULL);

	return state;
}

/**
 * @brief free a gluster_state_fd structure
 *
 * @param[in] exp_hdl  Export state_t will be associated with
 * @param[in] state    Related state if appropriate
 *
 */
void glusterfs_free_state(struct fsal_export *exp_hdl, struct state_t *state)
{
	struct glusterfs_state_fd *state_fd =
		container_of(state, struct glusterfs_state_fd, state);
	struct glusterfs_fd *my_fd = &state_fd->glusterfs_fd;

	PTHREAD_RWLOCK_destroy(&my_fd->fdlock);

	gsh_free(state_fd);
}

/** @todo: We have gone POSIX way for the APIs below, can consider the CEPH way
 * in case all are constants across all volumes etc.
 */

/**
 * @brief Implements GLUSTER FSAL exportoperation fs_supported_attrs
 */

static attrmask_t fs_supported_attrs(struct fsal_export *exp_hdl)
{
	attrmask_t supported_mask;

	supported_mask = fsal_supported_attrs(&exp_hdl->fsal->fs_info);
	if (!NFSv4_ACL_SUPPORT)
		supported_mask &= ~ATTR_ACL;
	return supported_mask;
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
	ops->wire_to_host = wire_to_host;
	ops->create_handle = create_handle;
	ops->get_fs_dynamic_info = get_dynamic_info;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->alloc_state = glusterfs_alloc_state;
	ops->free_state = glusterfs_free_state;
}

enum transport {
	GLUSTER_TCP_VOL,
	GLUSTER_RDMA_VOL
};

struct glexport_params {
	char *glvolname;
	char *glhostname;
	char *glvolpath;
	char *glfs_log;
	uint64_t up_poll_usec;
	bool enable_upcall;
	enum transport gltransport;
	char *sec_label_xattr;
};

static struct config_item_list transportformats[] = {
	CONFIG_LIST_TOK("tcp", GLUSTER_TCP_VOL),
	CONFIG_LIST_TOK("rdma", GLUSTER_RDMA_VOL),
	CONFIG_LIST_EOL
};

static struct config_item export_params[] = {
	CONF_ITEM_NOOP("name"),
	CONF_MAND_STR("volume", 1, MAXPATHLEN, NULL,
		      glexport_params, glvolname),
	CONF_MAND_STR("hostname", 1, MAXPATHLEN, NULL,
		      glexport_params, glhostname),
	CONF_ITEM_PATH("volpath", 1, MAXPATHLEN, "/",
		      glexport_params, glvolpath),
	CONF_ITEM_PATH("glfs_log", 1, MAXPATHLEN, GFAPI_LOG_LOCATION,
		       glexport_params, glfs_log),
	CONF_ITEM_UI64("up_poll_usec", 1, 60*1000*1000, 10,
		       glexport_params, up_poll_usec),
	CONF_ITEM_BOOL("enable_upcall", true, glexport_params,
		       enable_upcall),
	CONF_ITEM_TOKEN("transport", GLUSTER_TCP_VOL, transportformats,
			glexport_params, gltransport),
	CONF_ITEM_STR("sec_label_xattr", 0, 256, "security.selinux",
			glexport_params, sec_label_xattr),
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

/*
 * Given glusterfs_fs object, decrement the refcount. In case if it
 * becomes zero, free the resources.
 */
void
glusterfs_free_fs(struct glusterfs_fs *gl_fs)
{
	int64_t refcnt;
	int err     = 0;

	PTHREAD_MUTEX_lock(&GlusterFS.lock);

	refcnt = --(gl_fs->refcnt);

	assert(refcnt >= 0);

	if (refcnt) {
		LogDebug(COMPONENT_FSAL,
			 "There are still (%ld)active shares for volume(%s)",
			 gl_fs->refcnt, gl_fs->volname);
		PTHREAD_MUTEX_unlock(&GlusterFS.lock);
		return;
	}

	glist_del(&gl_fs->fs_obj);
	PTHREAD_MUTEX_unlock(&GlusterFS.lock);

	atomic_inc_int8_t(&gl_fs->destroy_mode);

	if (!gl_fs->enable_upcall)
		goto skip_upcall;

	/* Cancel upcall readiness if not yet done */
	up_ready_cancel((struct fsal_up_vector *)gl_fs->up_ops);

#ifndef USE_GLUSTER_UPCALL_REGISTER
	int *retval = NULL;
	/* Wait for up_thread to exit */
	err = pthread_join(gl_fs->up_thread, (void **)&retval);

	if (retval && *retval) {
		LogDebug(COMPONENT_FSAL, "Up_thread join returned value %d",
			 *retval);
	}

	if (err) {
		LogWarn(COMPONENT_FSAL, "Up_thread join failed (%s)",
			strerror(err));
	}
#else
	err = glfs_upcall_unregister(gl_fs->fs, GLFS_EVENT_ANY);

	if ((err < 0) || (!(err & GLFS_EVENT_INODE_INVALIDATE))) {
		/* Or can we ignore the error like in case of single
		 * node ganesha server. */
		LogWarn(COMPONENT_FSAL,
			"Unable to unregister for upcalls. Volume: %s",
			gl_fs->volname);
	}
#endif

skip_upcall:
	/* Gluster and memory cleanup */
	glfs_fini(gl_fs->fs);
	gsh_free(gl_fs->volname);
	gsh_free(gl_fs);
}

/**
 * @brief Given Gluster export params, find and return if there is
 * already existing export entry. If not create one.
 */
struct glusterfs_fs*
glusterfs_get_fs(struct glexport_params params,
		 const struct fsal_up_vector *up_ops)
{
	int rc = 0;
	struct glusterfs_fs *gl_fs = NULL;
	glfs_t  *fs = NULL;
	struct glist_head *glist, *glistn;

	PTHREAD_MUTEX_lock(&GlusterFS.lock);

	glist_for_each_safe(glist, glistn, &GlusterFS.fs_obj) {
		gl_fs = glist_entry(glist, struct glusterfs_fs,
				    fs_obj);
		if (!strcmp(params.glvolname, gl_fs->volname)) {
			goto found;
		}
	}

	gl_fs = gsh_calloc(1, sizeof(struct glusterfs_fs));

	glist_init(&gl_fs->fs_obj);

	fs = glfs_new(params.glvolname);
	if (!fs) {
		LogCrit(COMPONENT_FSAL,
			"Unable to create new glfs. Volume: %s",
			params.glvolname);
		goto out;
	}

	switch (params.gltransport) {
	case GLUSTER_RDMA_VOL:
		rc = glfs_set_volfile_server(fs, "rdma", params.glhostname,
				24007);
		break;
	default:
		rc = glfs_set_volfile_server(fs, "tcp", params.glhostname,
				24007);
		break;
	}

	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to set volume file. Volume: %s",
			params.glvolname);
		goto out;
	}

	rc = glfs_set_logging(fs, params.glfs_log, 7);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to set logging. Volume: %s",
			params.glvolname);
		goto out;
	}

	rc = glfs_init(fs);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to initialize volume. Volume: %s",
			params.glvolname);
		goto out;
	}

	gl_fs->fs = fs;
	gl_fs->volname = strdup(params.glvolname);
	gl_fs->destroy_mode = 0;
	gl_fs->up_poll_usec = params.up_poll_usec;

	gl_fs->up_ops = up_ops;

	gl_fs->enable_upcall = params.enable_upcall;

	if (!gl_fs->enable_upcall)
		goto skip_upcall;

#ifndef USE_GLUSTER_UPCALL_REGISTER
	rc = initiate_up_thread(gl_fs);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to create GLUSTERFSAL_UP_Thread. Volume: %s",
			params.glvolname);
		goto out;
	}
#else
	/* We are mainly interested in INODE_INVALIDATE for now. Still
	 * register for all the events
	 */
	rc = glfs_upcall_register(fs, GLFS_EVENT_ANY, gluster_process_upcall,
				  gl_fs);

	if ((rc < 0) || (!(rc & GLFS_EVENT_INODE_INVALIDATE))) {
		/* Or can we ignore the error like in case of single
		 * node ganesha server. */
		LogCrit(COMPONENT_FSAL,
			"Unable to register for upcalls. Volume: %s",
			params.glvolname);
		goto out;
	}
#endif

skip_upcall:
	glist_add(&GlusterFS.fs_obj, &gl_fs->fs_obj);

found:
	++(gl_fs->refcnt);
	PTHREAD_MUTEX_unlock(&GlusterFS.lock);
	return gl_fs;

out:
	PTHREAD_MUTEX_unlock(&GlusterFS.lock);
	if (fs)
		glfs_fini(fs);

	glist_del(&gl_fs->fs_obj); /* not needed atm */
	gsh_free(gl_fs);

	return NULL;
}

/**
 * @brief Implements GLUSTER FSAL moduleoperation create_export
 */

fsal_status_t glusterfs_create_export(struct fsal_module *fsal_hdl,
				      void *parse_node,
				      struct config_error_type *err_type,
				      const struct fsal_up_vector *up_ops)
{
	int rc = 0;
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	struct glusterfs_export *glfsexport = NULL;
	bool fsal_attached = false;
	struct glexport_params params = {
		.glvolname = NULL,
		.glhostname = NULL,
		.glvolpath = NULL,
		.glfs_log = NULL};

	LogDebug(COMPONENT_FSAL, "In args: export path = %s",
		 op_ctx->ctx_export->fullpath);

	glfsexport = gsh_calloc(1, sizeof(struct glusterfs_export));

	rc = load_config_from_node(parse_node,
				   &export_param,
				   &params,
				   true,
				   err_type);
	if (rc != 0) {
		LogCrit(COMPONENT_FSAL,
			"Incorrect or missing parameters for export %s",
			op_ctx->ctx_export->fullpath);
		status.major = ERR_FSAL_INVAL;
		goto out;
	}
	LogEvent(COMPONENT_FSAL, "Volume %s exported at : '%s'",
		 params.glvolname, params.glvolpath);

	fsal_export_init(&glfsexport->export);
	export_ops_init(&glfsexport->export.exp_ops);

	glfsexport->gl_fs = glusterfs_get_fs(params, up_ops);
	if (!glfsexport->gl_fs) {
		status.major = ERR_FSAL_SERVERFAULT;
		goto out;
	}

	rc = fsal_attach_export(fsal_hdl, &glfsexport->export.exports);
	if (rc != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL, "Unable to attach export. Export: %s",
			op_ctx->ctx_export->fullpath);
		goto out;
	}
	fsal_attached = true;

	glfsexport->mount_path = op_ctx->ctx_export->fullpath;
	glfsexport->export_path = params.glvolpath;
	glfsexport->saveduid = geteuid();
	glfsexport->savedgid = getegid();
	glfsexport->export.fsal = fsal_hdl;
	glfsexport->sec_label_xattr = params.sec_label_xattr;

	op_ctx->fsal_export = &glfsexport->export;

	glfsexport->pnfs_ds_enabled =
		glfsexport->export.exp_ops.fs_supports(&glfsexport->export,
						fso_pnfs_ds_supported);
	if (glfsexport->pnfs_ds_enabled) {
		struct fsal_pnfs_ds *pds = NULL;

		status =
		    fsal_hdl->m_ops.fsal_pnfs_ds(fsal_hdl, parse_node, &pds);

		if (status.major != ERR_FSAL_NO_ERROR)
			goto out;

		/* special case: server_id matches export_id */
		pds->id_servers = op_ctx->ctx_export->export_id;
		pds->mds_export = op_ctx->ctx_export;
		pds->mds_fsal_export = op_ctx->fsal_export;

		if (!pnfs_ds_insert(pds)) {
			LogCrit(COMPONENT_CONFIG,
				"Server id %d already in use.",
				pds->id_servers);
			status.major = ERR_FSAL_EXIST;
			goto out;
		}

		LogDebug(COMPONENT_PNFS,
			 "glusterfs_fsal_create: pnfs ds was enabled for [%s]",
			 op_ctx->ctx_export->fullpath);
	}

	glfsexport->pnfs_mds_enabled =
		glfsexport->export.exp_ops.fs_supports(&glfsexport->export,
						fso_pnfs_mds_supported);
	if (glfsexport->pnfs_mds_enabled) {
		LogDebug(COMPONENT_PNFS,
			 "glusterfs_fsal_create: pnfs mds was enabled for [%s]",
			 op_ctx->ctx_export->fullpath);
		export_ops_pnfs(&glfsexport->export.exp_ops);
		fsal_ops_pnfs(&glfsexport->export.fsal->m_ops);
	}

	glfsexport->export.up_ops = up_ops;

 out:
	gsh_free(params.glvolname);
	gsh_free(params.glhostname);
	gsh_free(params.glfs_log);

	if (status.major != ERR_FSAL_NO_ERROR) {
		gsh_free(params.glvolpath);

		if (fsal_attached)
			fsal_detach_export(fsal_hdl,
					   &glfsexport->export.exports);
		if (glfsexport->gl_fs)
			glusterfs_free_fs(glfsexport->gl_fs);
		gsh_free(glfsexport);
	}

	return status;
}

