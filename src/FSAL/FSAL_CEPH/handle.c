/*
 * Copyright © 2012, CohortFS, LLC.
 * Author: Adam C. Emerson <aemerson@linuxbox.com>
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
 * -------------
 */

/**
 * @file   handle.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @date   Mon Jul  9 15:18:47 2012
 *
 * @brief Interface to handle functionality
 *
 * This function implements the interfaces on the struct
 * fsal_obj_handle type.
 */

#include <fcntl.h>
#include <cephfs/libcephfs.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_pnfs_files.h"
#include "internal.h"
#include "nfs_exports.h"

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]  dir_pub The directory in which to look up the object.
 * @param[in]  path    The name to look up.
 * @param[out] obj_pub The looked up object.
 *
 * @return FSAL status codes.
 */

static fsal_status_t
lookup(struct fsal_obj_handle *dir_pub,
       const char *path,
       struct fsal_obj_handle **obj_pub)
{
        /* Generic status return */
        int rc = 0;
        /* Stat output */
        struct stat st;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);
        struct handle *obj = NULL;

        rc = ceph_ll_lookup(export->cmount, dir->wire.vi,
                            path, &st, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        rc = construct_handle(&st, export, &obj);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        *obj_pub = &obj->handle;

        return fsalstat(0, 0);
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory (excluding . and
 * .., which is ironic since the Ceph readdir call synthesizes them
 * out of nothing) and passes dirent information to the supplied
 * callback.
 *
 * @param[in]  dir_pub     The directory to read
 * @param[in]  entries_req Number of entries to return
 * @param[in]  whence      The cookie indicating resumption, NULL to start
 * @param[in]  dir_state   Opaque, passed to cb
 * @param[in]  cb          Callback that receives directory entries
 * @param[out] eof         True if there are no more entries
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_readdir(struct fsal_obj_handle *dir_pub,
             uint32_t entries_req,
             struct fsal_cookie *whence,
             void *dir_state,
             fsal_status_t (*cb)(
                     const char *name,
                     unsigned int dtype,
                     struct fsal_obj_handle *dir_hdl,
                     void *dir_state,
                     struct fsal_cookie *cookie),
             bool_t *eof)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);
        /* The director descriptor */
        struct ceph_dir_result *dir_desc = NULL;
        /* Cookie marking the start of the readdir */
        uint64_t start = 0;
        /* Count of entries processed */
        uint32_t count = 0;
        /* Return status */
        fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};

        rc = ceph_ll_opendir(export->cmount, dir->wire.vi,
                             &dir_desc, 0, 0);
        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        if (whence == NULL) {
                start = 0;
        } else if (whence->size == sizeof(uint64_t)) {
                memcpy(&start, whence->cookie, sizeof(uint64_t));
        } else {
                fsal_status.major = ERR_FSAL_INVAL;
        }

        ceph_seekdir(export->cmount, dir_desc, start);

        while ((count <= entries_req) && !(*eof)) {
                struct stat st;
                struct dirent de;
                struct fsal_cookie cookie;
                int stmask = 0;

                rc = ceph_readdirplus_r(export->cmount, dir_desc, &de,
                                        &st, &stmask);
                if (rc < 0) {
                        fsal_status = ceph2fsal_error(rc);
                        goto closedir;
                } else if (rc == 1) {
                        /* skip . and .. */
                        if((strcmp(de.d_name, ".") == 0) ||
                           (strcmp(de.d_name, "..") == 0)) {
                                continue;
                        }

                        cookie.size = sizeof(uint64_t);
                        memcpy(cookie.cookie, &de.d_off,
                               sizeof(uint64_t));
                        fsal_status = cb(de.d_name,
                                         de.d_type,
                                         dir_pub,
                                         dir_state,
                                         &cookie);
                        if (FSAL_IS_ERROR(fsal_status)) {
                                goto closedir;
                        }
                } else if (rc == 0) {
                        *eof = TRUE;
                } else {
                        /* Can't happen */
                        abort();
                }
        }


closedir:

        rc = ceph_ll_releasedir(export->cmount, dir_desc);

        if (rc < 0) {
                fsal_status = ceph2fsal_error(rc);
        }

        return fsal_status;
}

/**
 * @brief Create a regular file
 *
 * This function creates an empty, regular file.
 *
 * @param[in]  dir_pub Directory in which to create the file
 * @param[in]  name    Name of file to create
 * @param[out] attrib  Attributes of newly created file
 * @param[out] obj_pub Handle for newly created file
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_create(struct fsal_obj_handle *dir_pub,
            const char *name,
            struct attrlist *attrib,
            struct fsal_obj_handle **obj_pub)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);
        /* Newly opened file descriptor */
        Fh *fd = NULL;
        /* Status after create */
        struct stat st;
        /* Newly created object */
        struct handle *obj;

        rc = ceph_ll_create(export->cmount, dir->wire.vi, name,
                            0600, 0, &fd, &st, 0, 0);
        ceph_ll_close(export->cmount, fd);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        rc = construct_handle(&st, export, &obj);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        *obj_pub = &obj->handle;
        *attrib = obj->handle.attributes;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a directory
 *
 * This funcion creates a new directory.
 *
 * @param[in]  dir_pub The parent in which to create
 * @param[in]  name    Name of the directory to create
 * @param[out] attrib  Attributes of the newly created directory
 * @param[out] obj_pub Handle of the newly created directory
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_mkdir(struct fsal_obj_handle *dir_pub,
           const char *name,
           struct attrlist *attrib,
           struct fsal_obj_handle **obj_pub)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);
        /* Stat result */
        struct stat st;
        /* Newly created object */
        struct handle *obj = NULL;

        rc = ceph_ll_mkdir(export->cmount, dir->wire.vi, name,
                           0700, &st, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        rc = construct_handle(&st, export, &obj);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        *obj_pub = &obj->handle;
        *attrib = obj->handle.attributes;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @name Crete a symlink
 *
 * This function creates a new symlink with the given content.
 *
 * @param[in]  dir_pub   Parent directory
 * @param[in]  name      Name of the link
 * @param[in]  link_path Path linked to
 * @param[out] attrib    Attributes of the new symlink
 * @param[out] obj_pub   Handle for new symlink
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_symlink(struct fsal_obj_handle *dir_pub,
             const char *name,
             const char *link_path,
             struct attrlist *attrib,
             struct fsal_obj_handle **obj_pub)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);
        /* Stat result */
        struct stat st;
        /* Newly created object */
        struct handle *obj = NULL;

        rc = ceph_ll_symlink(export->cmount,
                             dir->wire.vi, name, link_path, &st, 0, 0);
        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        rc = construct_handle(&st, export, &obj);
        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        *obj_pub = &obj->handle;
        *attrib = obj->handle.attributes;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Retrieve the content of a symlink
 *
 * This function retrieves the content of a symlink, copying it into a
 * user specified buffer.
 *
 * @param[in]     link_pub    The handle for the link
 * @param[out]    content_buf The buffer to which the content are copied
 * @param[in,out] link_len    Length of buffer/length of content
 *                            (including NUL)
 * @param[in]     refresh     TRUE if the underlying content should be
 *                             refreshed.
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_readlink(struct fsal_obj_handle *link_pub,
              char *content_buf,
              size_t *link_len,
              bool_t refresh)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(link_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *link
                = container_of(link_pub, struct handle, handle);
        /* Pointer to the Ceph link content */
        char *content = NULL;
        /* Size of pathname */
        size_t size = 0;

        rc = ceph_ll_readlink(export->cmount,
                              link->wire.vi, &content, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        size = (strlen(content) + 1);
        if (size > *link_len) {
                *link_len = size;
                        return fsalstat(ERR_FSAL_NAMETOOLONG,0);
        }

        *link_len = size;
        memcpy(content_buf, content, size);

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given
 * file.
 *
 * @param[in]  handle_pub Object to interrogate
 * @param[out] attr       Attributes returned
 *
 * @return FSAL status.
 */

static fsal_status_t
getattrs(struct fsal_obj_handle *handle_pub)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* Stat buffer */
        struct stat st;

        rc = ceph_ll_getattr(export->cmount, handle->wire.vi,
                             &st, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        ceph2fsal_attributes(&st, &handle->handle.attributes);

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set attributes on a file
 *
 * This function sets attributes on a file.
 *
 * @param[in] handle_pub File to modify.
 * @param[in] attrs      Attributes to set.
 *
 * @return FSAL status.
 */

static fsal_status_t
setattrs(struct fsal_obj_handle *handle_pub,
         struct attrlist *attrs)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' directory handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* Stat buffer */
        struct stat st;
        /* Mask of attributes to set */
        uint32_t mask = 0;

        memset(&st, 0, sizeof(struct stat));

        if (attrs->mask & ~settable_attributes) {
                return fsalstat(ERR_FSAL_INVAL, 0);
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_MODE)) {
                mask |= CEPH_SETATTR_MODE;
                st.st_mode = fsal2unix_mode(attrs->mode);
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_OWNER)) {
                mask |= CEPH_SETATTR_UID;
                st.st_uid = attrs->owner;
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_GROUP)) {
                mask |= CEPH_SETATTR_UID;
                st.st_gid = attrs->group;
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_ATIME)) {
                mask |= CEPH_SETATTR_ATIME;
                st.st_atime = attrs->atime.seconds;
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_MTIME)) {
                mask |= CEPH_SETATTR_MTIME;
                st.st_mtime = attrs->mtime.seconds;
        }

        if (FSAL_TEST_MASK(attrs->mask, ATTR_CTIME)) {
                mask |= CEPH_SETATTR_CTIME;
                st.st_ctime = attrs->ctime.seconds;
        }

        rc = ceph_ll_setattr(export->cmount, handle->wire.vi,
                             &st, mask, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }


        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a hard link
 *
 * This function creates a link from the supplied file to a new name
 * in a new directory.
 *
 * @param[in] handle_pub  File to link
 * @param[in] destdir_pub Directory in which to create link
 * @param[in] name        Name of link
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_link(struct fsal_obj_handle *handle_pub,
          struct fsal_obj_handle *destdir_pub,
          const char *name)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* The private 'full' destination directory handle */
        struct handle *destdir
                = container_of(destdir_pub, struct handle, handle);
        struct stat st;

        rc = ceph_ll_link(export->cmount, handle->wire.vi, destdir->wire.vi,
                          name, &st, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possibly moving it into another
 * directory.  We assume most checks are done by the caller.
 *
 * @param[in] olddir_pub Source directory
 * @param[in] old_name   Original name
 * @param[in] newdir_pub Destination directory
 * @param[in] new_name   New name
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_rename(struct fsal_obj_handle *olddir_pub,
            const char *old_name,
            struct fsal_obj_handle *newdir_pub,
            const char *new_name)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(olddir_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *olddir
                = container_of(olddir_pub, struct handle, handle);
        /* The private 'full' destination directory handle */
        struct handle *newdir
                = container_of(newdir_pub, struct handle, handle);

        rc = ceph_ll_rename(export->cmount,
                            olddir->wire.vi, old_name,
                            newdir->wire.vi, new_name,
                            0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesystem and possibly
 * deletes the associated file.  Directories must be empty to be
 * removed.
 *
 * @param[in] dir_pub Parent directory
 * @param[in] name    Name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_unlink(struct fsal_obj_handle *dir_pub,
            const char *name)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(dir_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *dir
                = container_of(dir_pub, struct handle, handle);

        rc = ceph_ll_unlink(export->cmount, dir->wire.vi, name, 0, 0);
        if (rc == -EISDIR) {
                rc = ceph_ll_rmdir(export->cmount,
                                   dir->wire.vi, name, 0, 0);
        }

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Truncate a file
 *
 * This function shortens a file to the given length.
 *
 * @param[in] handle_pub File to truncate
 * @param[in] length     New file size
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_truncate(struct fsal_obj_handle *handle_pub,
              uint64_t length)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        rc = ceph_ll_truncate(export->cmount, handle->wire.vi,
                              length, 0, 0);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Open a file for read or write
 *
 * This function opens a file for reading or writing.  No lock is
 * taken, because we assume we are protected by the Cache inode
 * content lock.
 *
 * @param[in] handle_pub File to open
 * @param[in] openflags  Mode to open in
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_open(struct fsal_obj_handle *handle_pub,
          fsal_openflags_t openflags)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* Posix open flags */
        int posix_flags = 0;

        if (openflags & FSAL_O_RDWR) {
                posix_flags = O_RDWR;
        } else if (openflags & FSAL_O_READ) {
                posix_flags = O_RDONLY;
        } else if (openflags & FSAL_O_WRITE) {
                posix_flags = O_WRONLY;
        }

        /* We shouldn't need to lock anything, the content lock
           should keep the file descriptor protected. */

        if (handle->openflags != FSAL_O_CLOSED) {
                return fsalstat(ERR_FSAL_SERVERFAULT, 0);
        }

        rc = ceph_ll_open(export->cmount, handle->wire.vi,
                          posix_flags, &(handle->fd), 0, 0);
        if (rc < 0) {
                handle->fd = NULL;
                return ceph2fsal_error(rc);
        }

        handle->openflags = openflags;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Return the open status of a file
 *
 * This function returns the open status (the open mode last used to
 * open the file, in our case) for a given file.
 *
 * @param[in] handle_pub File to interrogate.
 *
 * @return Open mode.
 */

static fsal_openflags_t
status(struct fsal_obj_handle *handle_pub)
{
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        return handle->openflags;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from an open file.
 *
 * We take no lock, since we assume we are protected by the
 * Cache inode content lock.
 *
 * @param[in]  handle_pub  File to read
 * @param[in]  offset      Point at which to begin read
 * @param[in]  buffer_size Maximum number of bytes to read
 * @param[out] buffer      Buffer to store data read
 * @param[out] read_amount Count of bytes read
 * @param[out] end_of_file TRUE if the end of file is reached
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_read(struct fsal_obj_handle *handle_pub,
          uint64_t offset,
          size_t buffer_size,
          void *buffer,
          size_t *read_amount,
          bool_t *end_of_file)
{
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* Signed, so we can pick up on errors */
        int64_t nb_read = 0;

        nb_read = ceph_ll_read(export->cmount, handle->fd, offset,
                               buffer_size, buffer);

        if (nb_read < 0) {
                return ceph2fsal_error(nb_read);
        }

        if ((uint64_t) nb_read < buffer_size) {
                *end_of_file = TRUE;
        }

        *read_amount = nb_read;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write data to file
 *
 * This function writes data to an open file.
 *
 * We take no lock, since we assume we are protected by the Cache
 * inode content lock.
 *
 * @param[in]  handle_pub   File to write
 * @param[in]  offset       Position at which to write
 * @param[in]  buffer_size  Number of bytes to write
 * @param[in]  buffer       Data to write
 * @param[out] write_amount Number of bytes written
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_write(struct fsal_obj_handle *handle_pub,
           uint64_t offset,
           size_t buffer_size,
           void *buffer,
           size_t *write_amount)
{
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);
        /* Signed, so we can pick up on errors */
        int64_t nb_written = 0;

        nb_written = ceph_ll_write(export->cmount,
                                   handle->fd, offset, buffer_size,
                                   buffer);

        if (nb_written < 0) {
                return ceph2fsal_error(nb_written);
        }

        *write_amount = nb_written;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Commit written data
 *
 * This function commits written data to stable storage.  This FSAL
 * commits data from the entire file, rather than within the given
 * range.
 *
 * @param[in] handle_pub File to commit
 * @param[in] offset     Start of range to commit
 * @param[in] len        Size of range to commit
 *
 * @return FSAL status.
 */

static fsal_status_t
commit(struct fsal_obj_handle *handle_pub,
       off_t offset,
       size_t len)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        rc = ceph_ll_fsync(export->cmount, handle->fd, FALSE);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Close a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
 *
 * @param[in] handle_pub File to close
 *
 * @return FSAL status.
 */

static fsal_status_t
fsal_close(struct fsal_obj_handle *handle_pub)
{
        /* Generic status return */
        int rc = 0;
        /* The private 'full' export */
        struct export *export
                = container_of(handle_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        rc = ceph_ll_close(export->cmount, handle->fd);

        if (rc < 0) {
                return ceph2fsal_error(rc);
        }

        handle->fd = NULL;
        handle->openflags = FSAL_O_CLOSED;

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]     handle_pub  Handle to digest
 * @param[in]     output_type Type of digest requested
 * @param[in,out] fh_desc     Location/size of buffer for
 *                            digest/Length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t
handle_digest(struct fsal_obj_handle *handle_pub,
              uint32_t output_type,
              struct gsh_buffdesc *fh_desc)
{
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        switch (output_type) {
                /* Digested Handles */
        case FSAL_DIGEST_NFSV2:
        case FSAL_DIGEST_NFSV3:
        case FSAL_DIGEST_NFSV4:
                if (fh_desc->len < sizeof(handle->wire)) {
                        LogMajor(COMPONENT_FSAL,
                                 "digest_handle: space too small for "
                                 "handle.  Need %zu, have %zu",
                                 sizeof(handle->wire), fh_desc->len);
                        return fsalstat(ERR_FSAL_TOOSMALL, 0);
                } else {
                        memcpy(fh_desc->addr, &handle->wire,
                               sizeof(handle->wire));
                        fh_desc->len = sizeof(handle->wire);;
                }
                break;

        /* Integer IDs */

        case FSAL_DIGEST_FILEID2:
                return fsalstat(ERR_FSAL_TOOSMALL, 0);
                break;
        case FSAL_DIGEST_FILEID3:
        case FSAL_DIGEST_FILEID4:
                memcpy(fh_desc->addr, &handle->wire.vi.ino.val,
                       sizeof(handle->wire));
                fh_desc->len = sizeof(uint64_t);
                break;

        default:
                return fsalstat(ERR_FSAL_SERVERFAULT, 0);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]  handle_pub The file whose key is to be found
 * @param[out] fh_desc    Address and length of key
 */

static void
handle_to_key(struct fsal_obj_handle *handle_pub,
              struct gsh_buffdesc *fh_desc)
{
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(handle_pub, struct handle, handle);

        fh_desc->addr = &handle->wire.vi;
        fh_desc->len = sizeof(handle->wire.vi);
}

/**
 * @brief Grant a layout segment.
 *
 * Grant a layout on a subset of a file requested.  As a special case,
 * lie and grant a whole-file layout if requested, because Linux will
 * ignore it otherwise.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[out]    loc_body An XDR stream to which the FSAL must encode
 *                         the layout specific portion of the granted
 *                         layout segment.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, pp. 366-7.
 */

static nfsstat4
layoutget(struct fsal_obj_handle *obj_pub,
          struct req_op_context *req_ctx,
          XDR *loc_body,
          const struct fsal_layoutget_arg *arg,
          struct fsal_layoutget_res *res)
{
        /* The private 'full' export */
        struct export *export
                = container_of(obj_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(obj_pub, struct handle, handle);
        /* Structure containing the storage parameters of the file within
           the Ceph cluster. */
        struct ceph_file_layout file_layout;
        /* Width of each stripe on the file */
        uint32_t stripe_width = 0;
        /* Utility parameter */
        nfl_util4 util = 0;
        /* The last byte that can be accessed through pNFS */
        uint64_t last_possible_byte = 0;
        /* The deviceid for this layout */
        struct pnfs_deviceid deviceid = {0, 0};
        /* NFS Status */
        nfsstat4 nfs_status = 0;
        /* DS wire handle */
        struct ds_wire ds_wire;
        /* Descriptor for DS handle */
        struct gsh_buffdesc ds_desc = {.addr = &ds_wire,
                                       .len = sizeof(struct ds_wire)};

        /* We support only LAYOUT4_NFSV4_1_FILES layouts */

        if (arg->type != LAYOUT4_NFSV4_1_FILES) {
                LogCrit(COMPONENT_PNFS,
                        "Unsupported layout type: %x",
                        arg->type);
                return NFS4ERR_UNKNOWN_LAYOUTTYPE;
        }

        /* Get basic information on the file and calculate the dimensions
           of the layout we can support. */

        memset(&file_layout, 0, sizeof(struct ceph_file_layout));

        ceph_ll_file_layout(export->cmount, handle->wire.vi, &file_layout);
        stripe_width = file_layout.fl_stripe_unit;
        last_possible_byte = (BIGGEST_PATTERN * stripe_width) - 1;

        /* Since the Linux kernel refuses to work with any layout that
           doesn't cover the whole file, if a whole file layout is
           requested, lie.

           Otherwise, make sure the required layout doesn't go beyond
           what can be accessed through pNFS. */
        if (!((res->segment.offset == 0) &&
              (res->segment.length == NFS4_UINT64_MAX))) {
                struct pnfs_segment smallest_acceptable = {
                        .io_mode = res->segment.io_mode,
                        .offset = res->segment.offset,
                        .length = arg->minlength
                };
                struct pnfs_segment forbidden_area = {
                        .io_mode = res->segment.io_mode,
                        .offset = last_possible_byte + 1,
                        .length = NFS4_UINT64_MAX
                };
                if (pnfs_segments_overlap(smallest_acceptable,
                                          forbidden_area)) {
                        LogCrit(COMPONENT_PNFS,
                                "Required layout extends beyond allowed "
                                "region. offset: %"PRIu64
                                ", minlength: %" PRIu64".",
                                res->segment.offset,
                                arg->minlength);
                        return NFS4ERR_BADLAYOUT;
                }
                res->segment.offset = 0;
                res->segment.length = stripe_width * BIGGEST_PATTERN;
                res->segment.io_mode = LAYOUTIOMODE4_RW;
        }

        /* For now, just make the low quad of the deviceid be the
           inode number.  With the span of the layouts constrained
           above, this lets us generate the device address on the fly
           from the deviceid rather than storing it. */

        deviceid.export_id = arg->export_id;
        deviceid.devid = handle->wire.vi.ino.val;

        /* We return exactly one filehandle, filling in the necessary
           information for the DS server to speak to the Ceph OSD
           directly. */

        ds_wire.wire = handle->wire;
        ds_wire.layout = file_layout;
        ds_wire.snapseq = ceph_ll_snap_seq(export->cmount,
                                           handle->wire.vi);

        /* We are using sparse layouts with commit-through-DS, so our
           utility word contains only the stripe width, our first
           stripe is always at the beginning of the layout, and there
           is no pattern offset. */

        if ((stripe_width & ~NFL4_UFLG_STRIPE_UNIT_SIZE_MASK) != 0) {
                LogCrit(COMPONENT_PNFS,
                        "Ceph returned stripe width that is disallowed by "
                        "NFS: %"PRIu32".", stripe_width);
                return NFS4ERR_SERVERFAULT;
        }
        util = stripe_width;

        if ((nfs_status
             = FSAL_encode_file_layout(loc_body,
                                       &deviceid,
                                       util,
                                       0,
                                       0,
                                       obj_pub->export->exp_entry->id,
                                       1,
                                       &ds_desc))) {
                LogCrit(COMPONENT_PNFS,
                        "Failed to encode nfsv4_1_file_layout.");
                return nfs_status;
        }

        /* We grant only one segment, and we want it back when the file
           is closed. */

        res->return_on_close = TRUE;
        res->last_segment = TRUE;

        return NFS4_OK;
}

/**
 * @brief Potentially return one layout segment
 *
 * Since we don't make any reservations, in this version, or get any
 * pins to release, always succeed
 *
 * @param[in] obj_pub  Public object handle
 * @param[in] req_ctx  Request context
 * @param[in] lrf_body Nothing for us
 * @param[in] arg      Input arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 367.
 */
static nfsstat4
layoutreturn(struct fsal_obj_handle *obj_pub,
             struct req_op_context *req_ctx,
             XDR *lrf_body,
             const struct fsal_layoutreturn_arg *arg)
{
        /* Sanity check on type */
        if (arg->lo_type != LAYOUT4_NFSV4_1_FILES) {
                LogCrit(COMPONENT_PNFS,
                        "Unsupported layout type: %x",
                        arg->lo_type);
                return NFS4ERR_UNKNOWN_LAYOUTTYPE;
        }

        /* Since we no longer store DS addresses, we no longer have
           anything to free.  Later on we should unravel the Ceph
           client a bit more and coordinate with the Ceph MDS's notion
           of read and write pins, but that isn't germane until we
           have LAYOUTRECALL. */

        return NFS4_OK;
}

/**
 * @brief Commit a segment of a layout
 *
 * Update the size and time for a file accessed through a layout.
 *
 * @param[in]     obj_pub  Public object handle
 * @param[in]     req_ctx  Request context
 * @param[in]     lou_body An XDR stream containing the layout
 *                         type-specific portion of the LAYOUTCOMMIT
 *                         arguments.
 * @param[in]     arg      Input arguments of the function
 * @param[in,out] res      In/out and output arguments of the function
 *
 * @return Valid error codes in RFC 5661, p. 366.
 */
nfsstat4 layoutcommit(struct fsal_obj_handle *obj_pub,
                      struct req_op_context *req_ctx,
                      XDR *lou_body,
                      const struct fsal_layoutcommit_arg *arg,
                      struct fsal_layoutcommit_res *res)
{
        /* The private 'full' export */
        struct export *export
                = container_of(obj_pub->export, struct export, export);
        /* The private 'full' object handle */
        struct handle *handle
                = container_of(obj_pub, struct handle, handle);
        /* Old stat, so we don't truncate file or reverse time */
        struct stat stold;
        /* new stat to set time and size */
        struct stat stnew;
        /* Mask to determine exactly what gets set */
        int attrmask = 0;
        /* Error returns from Ceph */
        int ceph_status = 0;

        /* Sanity check on type */
        if (arg->type != LAYOUT4_NFSV4_1_FILES) {
                LogCrit(COMPONENT_PNFS,
                        "Unsupported layout type: %x",
                        arg->type);
                return NFS4ERR_UNKNOWN_LAYOUTTYPE;
        }

        /* A more proper and robust implementation of this would use
           Ceph caps, but we need to hack at the client to expose
           those before it can work. */

        memset(&stold, 0, sizeof(struct stat));
        if ((ceph_status
             = ceph_ll_getattr(export->cmount, handle->wire.vi,
                               &stold, 0, 0)) < 0) {
                LogCrit(COMPONENT_PNFS,
                        "Error %d in attempt to get attributes of "
                        "file %" PRIu64 ".",
                        -ceph_status, handle->wire.vi.ino.val);
                return posix2nfs4_error(-ceph_status);
        }

        memset(&stnew, 0, sizeof(struct stat));
        if (arg->new_offset) {
                if (stold.st_size < arg->last_write + 1) {
                        attrmask |= CEPH_SETATTR_SIZE;
                        stnew.st_size = arg->last_write + 1;
                        res->size_supplied = TRUE;
                        res->new_size = arg->last_write + 1;
                }
        }

        if ((arg->time_changed) &&
            (arg->new_time.seconds > stold.st_mtime)) {
                stnew.st_mtime = arg->new_time.seconds;
        } else {
                stnew.st_mtime = time(NULL);
        }

        attrmask |= CEPH_SETATTR_MTIME;

        if ((ceph_status = ceph_ll_setattr(export->cmount,
                                           handle->wire.vi, &stnew,
                                           attrmask, 0, 0)) < 0) {
                LogCrit(COMPONENT_PNFS,
                        "Error %d in attempt to get attributes of "
                        "file %" PRIu64 ".",
                        -ceph_status, handle->wire.vi.ino.val);
                return posix2nfs4_error(-ceph_status);
        }

        /* This is likely universal for files. */

        res->commit_done = TRUE;

        return NFS4_OK;
}

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with versions for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void
handle_ops_init(struct fsal_obj_ops *ops)
{
        ops->lookup = lookup;
        ops->create = fsal_create;
        ops->mkdir = fsal_mkdir;
        ops->readdir = fsal_readdir;
        ops->symlink = fsal_symlink;
        ops->readlink = fsal_readlink;
        ops->getattrs = getattrs;
        ops->setattrs = setattrs;
        ops->link = fsal_link;
        ops->rename = fsal_rename;
        ops->unlink = fsal_unlink;
        ops->truncate = fsal_truncate;
        ops->open = fsal_open;
        ops->status = status;
        ops->read = fsal_read;
        ops->write = fsal_write;
        ops->commit = commit;
        ops->close = fsal_close;
        ops->handle_digest = handle_digest;
        ops->handle_to_key = handle_to_key;
        ops->layoutget = layoutget;
        ops->layoutreturn = layoutreturn;
        ops->layoutcommit = layoutcommit;
}
