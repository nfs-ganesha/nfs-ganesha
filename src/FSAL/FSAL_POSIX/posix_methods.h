/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
 *
 * contributeur : Jim Lieb          jlieb@panasas.com
 *                Philippe DENIEL   philippe.deniel@cea.fr
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ------------- 
 */




struct posix_fsal_export {
    struct fsal_export export;
#define POSIX_FSAL_EXPORT_MAGIC         0xbc0a2a76U
    unsigned int magic;
    char *mntdir;
#ifdef SUPPORT_LINUX_QUOTAS
    char *fs_spec;
    char *fstype;
    dev_t root_dev;
#endif
};


fsal_status_t posix_lookup_path (struct fsal_export *exp_hdl, const char *path, struct fsal_obj_handle **handle);

fsal_status_t posix_create_handle (struct fsal_export *exp_hdl,
                                   struct gsh_buffdesc *hdl_desc, struct fsal_obj_handle **handle);

struct posix_fsal_obj_handle {
    struct fsal_obj_handle obj_handle;
    struct handle_data *handle;         /* actually, it points to ... */
    union {
        struct {
            int fd;
            fsal_openflags_t openflags;
        } file;
        struct {
            unsigned char *link_content;
            int link_size;
        } symlink;
    } u;
    /* ... here <=== */
};


        /* I/O management */
fsal_status_t posix_open (struct fsal_obj_handle *obj_hdl, fsal_openflags_t openflags);
fsal_openflags_t posix_status (struct fsal_obj_handle *obj_hdl);
fsal_status_t posix_read (struct fsal_obj_handle *obj_hdl,
                          uint64_t offset,
                          size_t buffer_size, void *buffer, size_t * read_amount, bool_t * end_of_file);
fsal_status_t posix_write (struct fsal_obj_handle *obj_hdl,
                           uint64_t offset, size_t buffer_size, void *buffer, size_t * write_amount);
fsal_status_t posix_commit (struct fsal_obj_handle *obj_hdl,    /* sync */
                            off_t offset, size_t len);
fsal_status_t posix_lock_op (struct fsal_obj_handle *obj_hdl,
                             void *p_owner,
                             fsal_lock_op_t lock_op,
                             fsal_lock_param_t * request_lock, fsal_lock_param_t * conflicting_lock);
fsal_status_t posix_share_op (struct fsal_obj_handle *obj_hdl, void *p_owner,   /* IN (opaque to FSAL) */
                              fsal_share_param_t request_share);
fsal_status_t posix_close (struct fsal_obj_handle *obj_hdl);
fsal_status_t posix_lru_cleanup (struct fsal_obj_handle *obj_hdl, lru_actions_t requests);

/* extended attributes management */
fsal_status_t posix_list_ext_attrs (struct fsal_obj_handle *obj_hdl,
                                    unsigned int cookie,
                                    fsal_xattrent_t * xattrs_tab,
                                    unsigned int xattrs_tabsize, unsigned int *p_nb_returned, int *end_of_list);
fsal_status_t posix_getextattr_id_by_name (struct fsal_obj_handle *obj_hdl,
                                           const char *xattr_name, unsigned int *pxattr_id);
fsal_status_t posix_getextattr_value_by_name (struct fsal_obj_handle *obj_hdl,
                                              const char *xattr_name,
                                              caddr_t buffer_addr, size_t buffer_size, size_t * p_output_size);
fsal_status_t posix_getextattr_value_by_id (struct fsal_obj_handle *obj_hdl,
                                            unsigned int xattr_id,
                                            caddr_t buffer_addr, size_t buffer_size, size_t * p_output_size);
fsal_status_t posix_setextattr_value (struct fsal_obj_handle *obj_hdl,
                                      const char *xattr_name, caddr_t buffer_addr, size_t buffer_size, int create);
fsal_status_t posix_setextattr_value_by_id (struct fsal_obj_handle *obj_hdl,
                                            unsigned int xattr_id, caddr_t buffer_addr, size_t buffer_size);
fsal_status_t posix_getextattr_attrs (struct fsal_obj_handle *obj_hdl, unsigned int xattr_id, struct attrlist *p_attrs);
fsal_status_t posix_remove_extattr_by_id (struct fsal_obj_handle *obj_hdl, unsigned int xattr_id);
fsal_status_t posix_remove_extattr_by_name (struct fsal_obj_handle *obj_hdl, const char *xattr_name);
