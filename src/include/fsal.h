/*
 *
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @defgroup FSAL File-System Abstraction Layer
 * @{
 */

/**
 * @file  fsal.h
 * @brief Main FSAL externs and functions
 * @note  not called by other header files.
 */

/**
 * @brief Thread Local Storage (TLS).
 *
 * TLS variables look like globals but since they are global only in the
 * context of a single thread, they do not require locks.  This is true
 * of all thread either within or separate from a/the fridge.
 *
 * All thread local storage is declared extern here.  The actual
 * storage declaration is in fridgethr.c.
 */

/**
 * @brief Operation context (op_ctx).
 *
 * This carries everything relevant to a protocol operation.
 * Space for the struct itself is allocated elsewhere.
 * Test/assert opctx != NULL first (or let the SEGV kill you)
 */

extern __thread struct req_op_context *op_ctx;

#ifndef FSAL_H
#define FSAL_H

#include "fsal_api.h"
#include "nfs23.h"
#include "nfs4_acls.h"
#include "nfs4_fs_locations.h"

/**
 * @brief If we don't know how big a buffer we want for a link, use
 * this value.
 */

#define fsal_default_linksize (4096)

/**
 * @brief Pointer to FSAL module by number.
 * This is actually defined in common_pnfs.c
 */
extern struct fsal_module *pnfs_fsal[];

/**
 * @brief Delegations types list for the Delegations parameter in FSAL.
 * This is actually defined in exports.c
 */
extern struct config_item_list deleg_types[];

/* Export permissions for root op context, defined in protocol layer */
extern uint32_t root_op_export_options;
extern uint32_t root_op_export_set;

/**
 * @brief node id used to construct recovery directory in
 * cluster implementation.
 */
extern int g_nodeid;

/**
 * @brief Ops context for asynch and not protocol tasks that need to use
 * subsystems that depend on op_ctx.
 */

struct root_op_context {
	struct req_op_context req_ctx;
	struct req_op_context *old_op_ctx;
	struct user_cred creds;
	struct export_perms export_perms;
};

extern size_t open_fd_count;

static inline void init_root_op_context(struct root_op_context *ctx,
					struct gsh_export *exp,
					struct fsal_export *fsal_exp,
					uint32_t nfs_vers,
					uint32_t nfs_minorvers,
					uint32_t req_type)
{
	/* Initialize req_ctx.
	 * Note that a zeroed creds works just fine as root creds.
	 */
	memset(ctx, 0, sizeof(*ctx));
	ctx->req_ctx.creds = &ctx->creds;
	ctx->req_ctx.nfs_vers = nfs_vers;
	ctx->req_ctx.nfs_minorvers = nfs_minorvers;
	ctx->req_ctx.req_type = req_type;

	ctx->req_ctx.ctx_export = exp;
	ctx->req_ctx.fsal_export = fsal_exp;
	if (fsal_exp)
		ctx->req_ctx.fsal_module = fsal_exp->fsal;
	else if (op_ctx)
		ctx->req_ctx.fsal_module = op_ctx->fsal_module;

	ctx->req_ctx.export_perms = &ctx->export_perms;
	ctx->export_perms.set = root_op_export_set;
	ctx->export_perms.options = root_op_export_options;

	ctx->old_op_ctx = op_ctx;
	op_ctx = &ctx->req_ctx;
}

static inline void release_root_op_context(void)
{
	struct root_op_context *ctx;

	ctx = container_of(op_ctx, struct root_op_context, req_ctx);
	op_ctx = ctx->old_op_ctx;
}

/******************************************************
 *                Structure used to define a fsal
 ******************************************************/

#include "FSAL/access_check.h"	/* rethink where this should go */

/**
 * Global fsal manager functions
 * used by nfs_main to initialize fsal modules.
 */

/* Called only within MODULE_INIT and MODULE_FINI functions of a fsal
 * module
 */

/**
 * @brief Register a FSAL
 *
 * This function registers an FSAL with ganesha and initializes the
 * public portion of the FSAL data structure, including providing
 * default operation vectors.
 *
 * @param[in,out] fsal_hdl      The FSAL module to register.
 * @param[in]     name          The FSAL's name
 * @param[in]     major_version Major version fo the API against which
 *                              the FSAL was written
 * @param[in]     minor_version Minor version of the API against which
 *                              the FSAL was written.
 *
 * @return 0 on success.
 * @return EINVAL on version mismatch.
 */

int register_fsal(struct fsal_module *fsal_hdl, const char *name,
		  uint32_t major_version, uint32_t minor_version,
		  uint8_t fsal_id);
/**
 * @brief Unregister an FSAL
 *
 * This function unregisters an FSAL from Ganesha.  It should be
 * called from the module finalizer as part of unloading.
 *
 * @param[in] fsal_hdl The FSAL to unregister
 *
 * @return 0 on success.
 * @return EBUSY if outstanding references or exports exist.
 */

int unregister_fsal(struct fsal_module *fsal_hdl);

/**
 * @brief Find and take a reference on an FSAL
 *
 * This function finds an FSAL by name and increments its reference
 * count.  It is used as part of export setup.  The @c put method
 * should be used  to release the reference before unloading.
 */
struct fsal_module *lookup_fsal(const char *name);

int load_fsal(const char *name,
	      struct fsal_module **fsal_hdl);

int fsal_load_init(void *node, const char *name,
		   struct fsal_module **fsal_hdl_p,
		   struct config_error_type *err_type);

struct fsal_args {
	char *name;
};

void *fsal_init(void *link_mem, void *self_struct);

struct subfsal_args {
	char *name;
	void *fsal_node;
};

int subfsal_commit(void *node, void *link_mem, void *self_struct,
		   struct config_error_type *err_type);

void destroy_fsals(void);
void emergency_cleanup_fsals(void);
void start_fsals(void);

void display_fsinfo(struct fsal_module *fsal);

int display_attrlist(struct display_buffer *dspbuf,
		     struct attrlist *attr, bool is_obj);

void log_attrlist(log_components_t component, log_levels_t level,
		  const char *reason, struct attrlist *attr, bool is_obj,
		  char *file, int line, char *function);

#define LogAttrlist(component, level, reason, attr, is_obj)                  \
	do {                                                                 \
		if (unlikely(isLevel(component, level)))                     \
			log_attrlist(component, level, reason, attr, is_obj, \
				     (char *) __FILE__, __LINE__,            \
				     (char *) __func__);                     \
	} while (0)

const char *msg_fsal_err(fsal_errors_t fsal_err);
#define fsal_err_txt(s) msg_fsal_err((s).major)

/*
 * FSAL helpers
 */

enum cb_state {
	CB_ORIGINAL,
	CB_JUNCTION,
	CB_PROBLEM,
};

typedef fsal_errors_t (*helper_readdir_cb)
	(void *opaque,
	 struct fsal_obj_handle *obj,
	 const struct attrlist *attr,
	 uint64_t mounted_on_fileid,
	 uint64_t cookie,
	 enum cb_state cb_state);

/**
 * @brief Type of callback for fsal_readdir
 *
 * This callback provides the upper level protocol handling function
 * with one directory entry at a time.  It may use the opaque to keep
 * track of the structure it is filling, space used, and so forth.
 *
 * This function should return true if the entry has been added to the
 * caller's responde, or false if the structure is fulled and the
 * structure has not been added.
 */

struct fsal_readdir_cb_parms {
	void *opaque;		/*< Protocol specific parms */
	const char *name;	/*< Dir entry name */
	bool attr_allowed;	/*< True if caller has perm to getattr */
	bool in_result;		/*< true if the entry has been added to the
				 *< caller's responde, or false if the
				 *< structure is filled and the entry has not
				 *< been added. */
};

fsal_status_t fsal_setattr(struct fsal_obj_handle *obj, bool bypass,
			   struct state_t *state, struct attrlist *attr);

/**
 *
 * @brief Checks the permissions on an object
 *
 * This function returns success if the supplied credentials possess
 * permission required to meet the specified access.
 *
 * @param[in]  obj         The object to be checked
 * @param[in]  access_type The kind of access to be checked
 *
 * @return FSAL status
 *
 */
static inline
fsal_status_t fsal_access(struct fsal_obj_handle *obj,
			  fsal_accessflags_t access_type)
{
	return
	    obj->obj_ops->test_access(obj, access_type, NULL, NULL, false);
}

fsal_status_t fsal_link(struct fsal_obj_handle *obj,
			struct fsal_obj_handle *dest_dir,
			const char *name);
fsal_status_t fsal_readlink(struct fsal_obj_handle *obj,
			    struct gsh_buffdesc *link_content);
fsal_status_t fsal_lookup(struct fsal_obj_handle *parent,
			  const char *name,
			  struct fsal_obj_handle **obj,
			  struct attrlist *attrs_out);
fsal_status_t fsal_lookupp(struct fsal_obj_handle *obj,
			   struct fsal_obj_handle **parent,
			   struct attrlist *attrs_out);
fsal_status_t fsal_create(struct fsal_obj_handle *parent,
			  const char *name,
			  object_file_type_t type,
			  struct attrlist *attrs,
			  const char *link_content,
			  struct fsal_obj_handle **obj,
			  struct attrlist *attrs_out);
void fsal_create_set_verifier(struct attrlist *sattr, uint32_t verf_hi,
			      uint32_t verf_lo);
bool fsal_create_verify(struct fsal_obj_handle *obj, uint32_t verf_hi,
			uint32_t verf_lo);

fsal_status_t fsal_readdir(struct fsal_obj_handle *directory, uint64_t cookie,
			   unsigned int *nbfound, bool *eod_met,
			   attrmask_t attrmask, helper_readdir_cb cb,
			   void *opaque);
fsal_status_t fsal_remove(struct fsal_obj_handle *parent, const char *name);
fsal_status_t fsal_rename(struct fsal_obj_handle *dir_src,
			  const char *oldname,
			  struct fsal_obj_handle *dir_dest,
			  const char *newname);
fsal_status_t fsal_open2(struct fsal_obj_handle *in_obj,
			 struct state_t *state,
			 fsal_openflags_t openflags,
			 enum fsal_create_mode createmode,
			 const char *name,
			 struct attrlist *attr,
			 fsal_verifier_t verifier,
			 struct fsal_obj_handle **obj,
			 struct attrlist *attrs_out);
fsal_status_t fsal_reopen2(struct fsal_obj_handle *obj,
			   struct state_t *state,
			   fsal_openflags_t openflags,
			   bool check_permission);
fsal_status_t get_optional_attrs(struct fsal_obj_handle *obj_hdl,
				 struct attrlist *attrs_out);
/**
 * @brief Close a file
 *
 * This handles both support_ex case and regular case (in case of
 * support_ex, close method is expected to manage whether file is
 * actually open or not, in old API case, close method should only
 * be closed if the file is open).
 *
 * In a change to the old way, non-regular files are just ignored.
 *
 * @param[in] obj	File to close
 * @return FSAL status
 */
static inline fsal_status_t fsal_close(struct fsal_obj_handle *obj_hdl)
{
	if (obj_hdl->type != REGULAR_FILE) {
		/* Can only close a regular file */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	/* Return the result of close method. */
	fsal_status_t status = obj_hdl->obj_ops->close(obj_hdl);

	if (status.major != ERR_FSAL_NOT_OPENED) {
		ssize_t count;

		count = atomic_dec_size_t(&open_fd_count);
		if (count < 0) {
			LogCrit(COMPONENT_FSAL,
				"open_fd_count is negative: %zd", count);
		}
	} else {
		/* Wasn't open.  Not an error, but shouldn't decrement */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	return status;
}

fsal_status_t fsal_statfs(struct fsal_obj_handle *obj,
			  fsal_dynamicfsinfo_t *dynamicinfo);

/**
 * @brief Commit a section of a file to storage
 *
 * @param[in] obj	File to commit
 * @param[in] offset	Offset for start of commit
 * @param[in] len	Length of commit
 * @return FSAL status
 */
static inline
fsal_status_t fsal_commit(struct fsal_obj_handle *obj, off_t offset,
			 size_t len)
{
	if ((uint64_t) len > ~(uint64_t) offset)
		return fsalstat(ERR_FSAL_INVAL, 0);

	return obj->obj_ops->commit2(obj, offset, len);
}
fsal_status_t fsal_verify2(struct fsal_obj_handle *obj,
			   fsal_verifier_t verifier);

/**
 * @brief Pepare an attrlist for fetching attributes.
 *
 * @param[in,out] attrs   The attrlist to work with
 * @param[in]             The mask to use for the fetch
 *
 */

static inline void fsal_prepare_attrs(struct attrlist *attrs,
				      attrmask_t request_mask)
{
	memset(attrs, 0, sizeof(*attrs));
	attrs->request_mask = request_mask;
}

/**
 * @brief Release any extra resources from an attrlist.
 *
 * @param[in] attrs   The attrlist to work with
 *
 */

static inline void fsal_release_attrs(struct attrlist *attrs)
{
	if (attrs->acl != NULL) {
		int acl_status;

		acl_status = nfs4_acl_release_entry(attrs->acl);

		if (acl_status != NFS_V4_ACL_SUCCESS)
			LogCrit(COMPONENT_FSAL,
				"Failed to release old acl, status=%d",
				acl_status);

		/* Poison the acl since we no longer hold a reference. */
		attrs->acl = NULL;
		attrs->valid_mask &= ~ATTR_ACL;
	}

	if (attrs->fs_locations) {
		nfs4_fs_locations_release(attrs->fs_locations);
		attrs->fs_locations = NULL;
		attrs->valid_mask &= ~ATTR4_FS_LOCATIONS;
	}

	attrs->sec_label.slai_data.slai_data_len = 0;
	gsh_free(attrs->sec_label.slai_data.slai_data_val);
	attrs->sec_label.slai_data.slai_data_val = NULL;
}

/**
 * @brief Copy a set of attributes
 *
 * If ACL is requested in dest->request_mask, then ACL reference is acquired,
 * otherwise acl pointer is set to NULL.
 *
 * @param[in,out] dest       The attrlist to receive the copy (mask must be set)
 * @param[in]     src        The attrlist to make a copy of
 * @param[in]     pass_refs  If true, pass the ACL reference to dest.
 *
 */

static inline void fsal_copy_attrs(struct attrlist *dest,
				   struct attrlist *src,
				   bool pass_refs)
{
	attrmask_t save_request_mask = dest->request_mask;

	/* Copy source to dest, but retain dest->request_mask */
	*dest = *src;
	dest->request_mask = save_request_mask;

	if (pass_refs && ((save_request_mask & ATTR_ACL) != 0)) {
		/* Pass any ACL reference to the dest, so remove from
		 * src without adjusting the refcount.
		 */
		src->acl = NULL;
		src->valid_mask &= ~ATTR_ACL;
	} else if (dest->acl != NULL && ((save_request_mask & ATTR_ACL) != 0)) {
		/* Take reference on ACL if necessary */
		nfs4_acl_entry_inc_ref(dest->acl);
	} else {
		/* Make sure acl is NULL and don't pass a ref back (so
		 * caller when calling fsal_release_attrs will not have to
		 * release the ACL reference).
		 */
		dest->acl = NULL;
		dest->valid_mask &= ~ATTR_ACL;
	}

	if (pass_refs && ((save_request_mask & ATTR4_FS_LOCATIONS) != 0)) {
		src->fs_locations = NULL;
		src->valid_mask &= ~ATTR4_FS_LOCATIONS;
	} else if (dest->fs_locations != NULL &&
		((save_request_mask & ATTR4_FS_LOCATIONS) != 0)) {
		nfs4_fs_locations_get_ref(dest->fs_locations);
	} else {
		dest->fs_locations = NULL;
		dest->valid_mask &= ~ATTR4_FS_LOCATIONS;
	}

	/*
	 * Ditto for security label. Here though, we just make a copy if
	 * needed.
	 */
	if (pass_refs && ((save_request_mask & ATTR4_SEC_LABEL) != 0)) {
		src->sec_label.slai_data.slai_data_len = 0;
		src->sec_label.slai_data.slai_data_val = NULL;
		src->valid_mask &= ~ATTR4_SEC_LABEL;
	} else if (dest->sec_label.slai_data.slai_data_val != NULL &&
		((save_request_mask & ATTR4_SEC_LABEL) != 0)) {
		dest->sec_label.slai_data.slai_data_val = (char *)
			gsh_memdup(dest->sec_label.slai_data.slai_data_val,
				   dest->sec_label.slai_data.slai_data_len);
	} else {
		dest->sec_label.slai_data.slai_data_len = 0;
		dest->sec_label.slai_data.slai_data_val = NULL;
		dest->valid_mask &= ~ATTR4_SEC_LABEL;
	}
}

/**
 * @brief Return a changeid4 for this file.
 *
 * @param[in] obj   The file to query.
 *
 * @return A changeid4 indicating the last modification of the file.
 */

static inline changeid4
fsal_get_changeid4(struct fsal_obj_handle *obj)
{
	struct attrlist attrs;
	fsal_status_t status;
	changeid4 change;

	fsal_prepare_attrs(&attrs, ATTR_CHANGE | ATTR_CHGTIME);

	status = obj->obj_ops->getattrs(obj, &attrs);

	if (FSAL_IS_ERROR(status))
		return 0;

	change = (changeid4) attrs.change;

	/* Done with the attrs */
	fsal_release_attrs(&attrs);

	return change;
}

static inline
enum fsal_create_mode nfs4_createmode_to_fsal(createmode4 createmode)
{
	return (enum fsal_create_mode) (1 + (unsigned int) createmode);
}

static inline
enum fsal_create_mode nfs3_createmode_to_fsal(createmode3 createmode)
{
	return (enum fsal_create_mode) (1 + (unsigned int) createmode);
}

/**
 * @brief Determine if the openflags associated with an fd indicate it
 * is not open in a mode usable by the caller.
 *
 * The caller may pass FSAL_O_ANY to indicate any mode of open (RDONLY,
 * WRONLY, or RDWR is useable - often just to fetch attributes or something).
 *
 * @param[in] fd_openflags The openflags describing the fd
 * @param[in] to_openflags The openflags describing the desired mode
 */

static inline bool not_open_usable(fsal_openflags_t fd_openflags,
				   fsal_openflags_t to_openflags)
{
	/* 1. fd_openflags will NEVER be FSAL_O_ANY.
	 * 2. If to_openflags == FSAL_O_ANY, the first half will be true if the
	 *    file is closed, and the second half MUST be true (per statement 1)
	 * 3. If to_openflags is anything else, the first half will be true and
	 *    the second half will be true if fd_openflags does not include
	 *    the requested modes.
	 */
	return (to_openflags != FSAL_O_ANY || fd_openflags == FSAL_O_CLOSED)
	       && ((fd_openflags & to_openflags) != to_openflags);
}

/**
 * @brief Determine if the openflags associated with an fd indicate it
 * is open in a mode usable by the caller.
 *
 * The caller may pass FSAL_O_ANY to indicate any mode of open (RDONLY,
 * WRONLY, or RDWR is useable - often just to fetch attributes or something).
 *
 * Note that this function is not just an inversion of the above function
 * because O_SYNC is not considered.
 *
 * @param[in] fd_openflags The openflags describing the fd
 * @param[in] to_openflags The openflags describing the desired mode
 */

static inline bool open_correct(fsal_openflags_t fd_openflags,
				fsal_openflags_t to_openflags)
{
	return (to_openflags == FSAL_O_ANY && fd_openflags != FSAL_O_CLOSED)
	       || (to_openflags != FSAL_O_ANY
		   && (fd_openflags & to_openflags & FSAL_O_RDWR)
					== (to_openflags & FSAL_O_RDWR));
}

/**
 * @brief "fsal_op_stats" struct useful for all the fsals which are going to
 * implement support for FSAL specific statistics
 */
struct fsal_op_stats {
	uint16_t op_code;
	uint64_t resp_time;
	uint64_t num_ops;
	uint64_t resp_time_max;
	uint64_t resp_time_min;
};

struct fsal_stats {
	uint16_t total_ops;
	struct fsal_op_stats *op_stats;
};

/* Async Processes that will be made synchronous */
struct async_process_data {
	/** Return from process */
	fsal_status_t ret;
	/** Indicator callback is done. */
	bool done;
	/** Mutex to protect done and condition variable. */
	pthread_mutex_t *mutex;
	/** Condition variable to signal callback is done. */
	pthread_cond_t *cond;
};

extern void fsal_read(struct fsal_obj_handle *obj_hdl,
		      bool bypass,
		      struct fsal_io_arg *arg,
		      struct async_process_data *data);

extern void fsal_write(struct fsal_obj_handle *obj_hdl,
		       bool bypass,
		       struct fsal_io_arg *arg,
		       struct async_process_data *data);

#endif				/* !FSAL_H */
/** @} */
