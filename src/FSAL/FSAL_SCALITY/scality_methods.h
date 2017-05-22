/* -*- mode: c; c-tab-always-indent: t; c-basic-offset: 8 -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* SCALITY methods for handles
 */

#include "avltree.h"
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"


#define V4_FH_OPAQUE_SIZE (NFS4_FHSIZE - sizeof(struct file_handle_v4))
#define SCALITY_OPAQUE_SIZE (sizeof(uint64_t))

enum static_assert_cookie_size {  static_assert_cookie_size_check = 1 / (sizeof(fsal_cookie_t) == SCALITY_OPAQUE_SIZE) };

#define MAX_URL_SIZE 4096
#define S3_DELIMITER "/"
#define S3_DELIMITER_SZ (sizeof(S3_DELIMITER)-1)
#define READDIR_MAX_KEYS 50
#define DEFAULT_PART_SIZE (5*(1<<20))
#define FLUSH_THRESHOLD (30*(1<<20))

enum static_assert_s3_delimiter_size { static_assert_s3_delimiter_size_check = 1 / ( S3_DELIMITER_SZ == 1 ) };


struct scality_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;

	char *dbd_url;
	char *sproxyd_url;

	char *redis_host;
	unsigned short redis_port;
};

struct scality_fsal_obj_handle;

/*
 * SCALITY internal export
 */
struct scality_fsal_export {
	struct fsal_export export;

	struct scality_fsal_module *module;
	char *export_path;
	char *bucket;
	int metadata_version;
	struct timespec creation_date;
	char *owner_display_name;
	char *owner_id;
	mode_t umask;

	struct avltree handles;
	pthread_mutex_t handles_mutex;
	struct scality_fsal_obj_handle *root_handle;
};

fsal_status_t scality_lookup_path(struct fsal_export *exp_hdl,
				  const char *path,
				  struct fsal_obj_handle **handle,
				  struct attrlist *attrs_out);

fsal_status_t scality_create_handle(struct fsal_export *exp_hdl,
				    struct gsh_buffdesc *hdl_desc,
				    struct fsal_obj_handle **handle,
				    struct attrlist *attrs_out);

typedef enum {
	STENCIL_ZERO, //content is zero, happens when file is extended
	STENCIL_READ, //content of the buffer is undefined, read the storage
	STENCIL_COPY, //content of the buffer is set and dirty, copy it
} stencil_byte_t;

/*
 * SCALITY internal object handle
 * handle is a pointer because
 *  a) the last element of file_handle is a char[] meaning variable len...
 *  b) we cannot depend on it *always* being last or being the only
 *     variable sized struct here...  a pointer is safer.
 */

struct scality_location
{
	ssize_t start;
	ssize_t size;
	ssize_t buffer_size;
	char *key;
	char *content;
	char *stencil;
	struct avltree_node avltree_node;
};

struct scality_part
{
	char *key;
	struct scality_part *next;
};

enum scality_fsal_obj_state {
	SCALITY_FSAL_OBJ_STATE_CLEAN,
	SCALITY_FSAL_OBJ_STATE_DIRTY,
	SCALITY_FSAL_OBJ_STATE_DELETED
};

struct scality_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct state_hdl obj_state;
	struct attrlist attributes;
	char *handle;
	uint32_t numlinks;
	int32_t ref_count;

	char *object; // object in the bucket (without any heading delimiter)

	pthread_mutex_t content_mutex;
	struct avltree locations;
	size_t n_locations;
	fsal_openflags_t openflags;

	enum scality_fsal_obj_state state;
	size_t part_size;
	size_t memory_used;
	struct scality_part *delete_on_commit;
	struct scality_part *delete_on_rollback;

	struct avltree_node avltree_node;
};


enum scality_fsal_cleanup_flag {
	SCALITY_FSAL_CLEANUP_NONE = 0,
	SCALITY_FSAL_CLEANUP_COMMIT = 1u<<0,
	SCALITY_FSAL_CLEANUP_ROLLBACK = 1u<<1,
	SCALITY_FSAL_CLEANUP_PARTS = 1u<<2,
};

void
scality_sanity_check_parts(struct scality_fsal_export *export,
			   struct scality_fsal_obj_handle *myself);

void
scality_cleanup(struct scality_fsal_export *export,
	       struct scality_fsal_obj_handle *myself,
	       enum scality_fsal_cleanup_flag cleanup_flag);

int scality_fsal_open(struct scality_fsal_obj_handle *, int, fsal_errors_t *);
int scality_fsal_readlink(struct scality_fsal_obj_handle *, fsal_errors_t *);

static inline bool scality_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

static inline void
scality_content_lock(struct scality_fsal_obj_handle *myself)
{
	int content_lock_ret;
	content_lock_ret = pthread_mutex_lock(&myself->content_mutex);
	assert(0 == content_lock_ret);
}

static inline void
scality_content_unlock(struct scality_fsal_obj_handle *myself)
{
	int content_unlock_ret;
	content_unlock_ret = pthread_mutex_unlock(&myself->content_mutex);
	assert(0 == content_unlock_ret);
}

static inline void
scality_export_lock(struct scality_fsal_export *export)
{
	int export_lock_ret;
	export_lock_ret = pthread_mutex_lock(&export->handles_mutex);
	assert(0 == export_lock_ret);
}

static inline void
scality_export_unlock(struct scality_fsal_export *export)
{
	int export_unlock_ret;
	export_unlock_ret = pthread_mutex_unlock(&export->handles_mutex);
	assert(0 == export_unlock_ret);
}

struct scality_location *
scality_location_new(const char *key, size_t start, size_t size);
void
scality_location_free(struct scality_location *location);

struct scality_location *
scality_location_lookup(struct scality_fsal_obj_handle *myself,
			uint64_t offset,
			size_t size);

/* I/O management */
fsal_status_t scality_open(struct fsal_obj_handle *obj_hdl,
			  fsal_openflags_t openflags);
fsal_openflags_t scality_status(struct fsal_obj_handle *obj_hdl);
fsal_status_t scality_read(struct fsal_obj_handle *obj_hdl,
			  uint64_t offset,
			  size_t buffer_size, void *buffer,
			  size_t *read_amount, bool *end_of_file);
void
scality_add_to_free_list(struct scality_part **partp, char *key);
fsal_status_t
scality_truncate(struct scality_fsal_obj_handle *myself,
		 size_t filesize);

fsal_status_t scality_write(struct fsal_obj_handle *obj_hdl,
			   uint64_t offset,
			   size_t buffer_size, void *buffer,
			   size_t *write_amount, bool *fsal_stable);
fsal_status_t scality_commit(struct fsal_obj_handle *obj_hdl,	/* sync */
			    off_t offset, size_t len);
fsal_status_t scality_lock_op(struct fsal_obj_handle *obj_hdl,
			     void *p_owner,
			     fsal_lock_op_t lock_op,
			     fsal_lock_param_t *request_lock,
			     fsal_lock_param_t *conflicting_lock);
fsal_status_t scality_share_op(struct fsal_obj_handle *obj_hdl, void *p_owner,
			      fsal_share_param_t request_share);
fsal_status_t scality_close(struct fsal_obj_handle *obj_hdl);

/* extended attributes management */
fsal_status_t scality_list_ext_attrs(struct fsal_obj_handle *obj_hdl,
				    unsigned int cookie,
				    fsal_xattrent_t *xattrs_tab,
				    unsigned int xattrs_tabsize,
				    unsigned int *p_nb_returned,
				    int *end_of_list);
fsal_status_t scality_getextattr_id_by_name(struct fsal_obj_handle *obj_hdl,
					   const char *xattr_name,
					   unsigned int *pxattr_id);
fsal_status_t scality_getextattr_value_by_name(struct fsal_obj_handle *obj_hdl,
					      const char *xattr_name,
					      caddr_t buffer_addr,
					      size_t buffer_size,
					      size_t *p_output_size);
fsal_status_t scality_getextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size,
					    size_t *p_output_size);
fsal_status_t scality_setextattr_value(struct fsal_obj_handle *obj_hdl,
				      const char *xattr_name,
				      caddr_t buffer_addr, size_t buffer_size,
				      int create);
fsal_status_t scality_setextattr_value_by_id(struct fsal_obj_handle *obj_hdl,
					    unsigned int xattr_id,
					    caddr_t buffer_addr,
					    size_t buffer_size);
fsal_status_t scality_remove_extattr_by_id(struct fsal_obj_handle *obj_hdl,
					  unsigned int xattr_id);
fsal_status_t scality_remove_extattr_by_name(struct fsal_obj_handle *obj_hdl,
					    const char *xattr_name);

void scality_handle_ops_init(struct fsal_obj_ops *ops);

/* Internal SCALITY method linkage to export object
 */

fsal_status_t scality_create_export(struct fsal_module *fsal_hdl,
				     void *parse_node,
				     struct config_error_type *err_type,
				     const struct fsal_up_vector *up_ops);
