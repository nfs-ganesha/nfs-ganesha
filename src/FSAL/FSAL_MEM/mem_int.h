/*
 * vim:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017-2018 Red Hat, Inc.
 * Author: Daniel Gryniewicz  dang@redhat.com
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

/* MEM methods for handles
*/

#include "avltree.h"
#include "gsh_list.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif

struct mem_fsal_obj_handle;

/**
 * MEM internal export
 */
struct mem_fsal_export {
	/** Export this wraps */
	struct fsal_export export;
	/** The path for this export */
	char *export_path;
	/** Root object for this export */
	struct mem_fsal_obj_handle *root_handle;
	/** Entry into list of exports */
	struct glist_head export_entry;
	/** Lock protecting mfe_objs */
	pthread_rwlock_t mfe_exp_lock;
	/** List of all the objects in this export */
	struct glist_head mfe_objs;
};

fsal_status_t mem_lookup_path(struct fsal_export *exp_hdl,
				const char *path,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out);

fsal_status_t mem_create_handle(struct fsal_export *exp_hdl,
				  struct gsh_buffdesc *hdl_desc,
				  struct fsal_obj_handle **handle,
				  struct attrlist *attrs_out);

/*
 * MEM internal object handle
 */

#define V4_FH_OPAQUE_SIZE 58 /* Size of state_obj digest */

struct mem_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attrs;
	uint64_t inode;
	char handle[V4_FH_OPAQUE_SIZE];
	union {
		struct {
			struct mem_fsal_obj_handle *parent;
			struct avltree avl_name;
			struct avltree avl_index;
			uint32_t numkids;
			uint32_t next_i; /* next child index */
		} mh_dir;
		struct {
			struct fsal_share share;
			struct fsal_fd fd;
		} mh_file;
		struct {
			object_file_type_t nodetype;
			fsal_dev_t dev;
		} mh_node;
		struct {
			char *link_contents;
		} mh_symlink;
	};
	struct glist_head dirents; /**< List of dirents pointing to obj */
	struct glist_head mfo_exp_entry; /**< Link into mfs_objs */
	struct mem_fsal_export *mfo_exp; /**< Export owning object */
	char *m_name;	/**< Base name of obj, for debugging */
	uint32_t datasize;
	bool is_export;
	uint32_t refcount; /**< We persist handles, so we need a refcount */
	char data[0]; /* Allocated data */
};

/**
 * @brief Dirent for FSAL_MEM
 */
struct mem_dirent {
	struct mem_fsal_obj_handle *hdl; /**< Handle dirent points to */
	struct mem_fsal_obj_handle *dir; /**< Dir containing dirent */
	const char *d_name;		 /**< Name of dirent */
	uint32_t d_index;		 /**< index in dir */
	struct avltree_node avl_n;	 /**< Entry in dir's avl_name tree */
	struct avltree_node avl_i;	 /**< Entry in dir's avl_index tree */
	struct glist_head dlist;	 /**< Entry in hdl's dirents list */
};

static inline bool mem_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

void mem_handle_ops_init(struct fsal_obj_ops *ops);

/* Internal MEM method linkage to export object
*/

fsal_status_t mem_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);


#define mem_free_handle(h) _mem_free_handle(h, __func__, __LINE__)
/**
 * @brief Free a MEM handle
 *
 * @note mfe_exp_lock MUST be held for write
 * @param[in] hdl	Handle to free
 */
static inline void _mem_free_handle(struct mem_fsal_obj_handle *hdl,
				    const char *func, int line)
{
#ifdef USE_LTTNG
	tracepoint(fsalmem, mem_free, func, line, hdl, hdl->m_name);
#endif

	glist_del(&hdl->mfo_exp_entry);
	hdl->mfo_exp = NULL;

	if (hdl->m_name != NULL) {
		gsh_free(hdl->m_name);
		hdl->m_name = NULL;
	}

	gsh_free(hdl);
}

void mem_clean_export(struct mem_fsal_obj_handle *root);
void mem_clean_all_dirents(struct mem_fsal_obj_handle *parent);

/**
 * @brief FSAL Module wrapper for MEM
 */
struct mem_fsal_module {
	/** Module we're wrapping */
	struct fsal_module fsal;
	/** fsal_obj_handle ops vector */
	struct fsal_obj_ops handle_ops;
	/** List of MEM exports. TODO Locking when we care */
	struct glist_head mem_exports;
	/** Config - size of data in inode */
	uint32_t inode_size;
	/** Config - Interval for UP call thread */
	uint32_t up_interval;
	/** Next unused inode */
	uint64_t next_inode;
};


/* UP testing */
fsal_status_t mem_up_pkginit(void);
fsal_status_t mem_up_pkgshutdown(void);

extern struct mem_fsal_module MEM;
