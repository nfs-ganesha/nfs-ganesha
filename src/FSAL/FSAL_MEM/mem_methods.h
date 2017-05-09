/*
 * vim:shiftwidth=8:tabstop=8:
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

/* MEM methods for handles
*/

#include "avltree.h"
#include "gsh_list.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif

struct mem_fsal_obj_handle;

/*
 * MEM internal export
 */
struct mem_fsal_export {
	struct fsal_export export;
	char *export_path;
	struct mem_fsal_obj_handle *root_handle;
};

fsal_status_t mem_lookup_path(struct fsal_export *exp_hdl,
				const char *path,
				struct fsal_obj_handle **handle,
				struct attrlist *attrs_out);

fsal_status_t mem_create_handle(struct fsal_export *exp_hdl,
				  struct gsh_buffdesc *hdl_desc,
				  struct fsal_obj_handle **handle,
				  struct attrlist *attrs_out);

struct mem_fd {
	/** The open and share mode etc. */
	fsal_openflags_t openflags;
	/** Current file offset location */
	off_t offset;
};

/*
 * MEM internal object handle
 */

#define V4_FH_OPAQUE_SIZE 58 /* Size of state_obj digest */

struct mem_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct attrlist attrs;
	char handle[V4_FH_OPAQUE_SIZE];
	struct mem_fsal_obj_handle *parent;
	union {
		struct {
			struct avltree avl_name;
			struct avltree avl_index;
			uint32_t numlinks;
		} mh_dir;
		struct {
			struct fsal_share share;
			struct mem_fd fd;
			off_t length;
		} mh_file;
		struct {
			object_file_type_t nodetype;
			fsal_dev_t dev;
		} mh_node;
		struct {
			char *link_contents;
		} mh_symlink;
	};
	struct avltree_node avl_n;
	struct avltree_node avl_i;
	uint32_t index; /* index in parent */
	uint32_t next_i; /* next child index */
	char *m_name;
	bool inavl;
	uint32_t datasize;
	char data[0]; /* Allocated data */
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
 * @param[in] hdl	Handle to free
 */
static inline void _mem_free_handle(struct mem_fsal_obj_handle *hdl,
				    const char *func, int line)
{
#ifdef USE_LTTNG
	tracepoint(fsalmem, mem_free, func, line, hdl);
#endif
	if (hdl->m_name != NULL) {
		gsh_free(hdl->m_name);
		hdl->m_name = NULL;
	}

	gsh_free(hdl);
}

void mem_clean_dir_tree(struct mem_fsal_obj_handle *parent);

struct mem_fsal_module {
	struct fsal_module fsal;
	struct fsal_staticfsinfo_t fs_info;
	uint32_t inode_size;
};

extern struct mem_fsal_module MEM;
