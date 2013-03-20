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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ------------- 
 */


#include "config.h"

#include "fsal.h"
#include "fsal_handle_syscalls.h"
#include <libgen.h> /* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "nlm_list.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"
#include <stdbool.h>
#include "posix_methods.h"

#include "redblack.h"
#include "sockbuf.h"
#include "nodedb.h"
#include "connection.h"
#include "connectionpool.h"
#include "interface.h"

#define xfree(x)	do { if ((x)) { free(x); (x) = NULL; } } while (0)
#define xclosedir(x)	do { if ((x)) { closedir(x); (x) = NULL; } } while (0)

extern struct connection_pool *connpool;



static struct posix_fsal_obj_handle *alloc_handle_ (struct handle_data *d, const struct stat *stat,
                                                    const char *link_content, struct fsal_export *exp_hdl)
{
    struct posix_fsal_obj_handle *hdl;
    fsal_status_t st;

    hdl = malloc (sizeof (struct posix_fsal_obj_handle) + sizeof (struct handle_data));
    if (hdl == NULL)
        return NULL;
    memset (hdl, 0, (sizeof (struct posix_fsal_obj_handle) + sizeof (struct handle_data)));
    hdl->handle = (struct handle_data *) &hdl[1];
    memcpy (hdl->handle, d, sizeof (struct handle_data));
    hdl->obj_handle.type = posix2fsal_type (stat->st_mode);
    if (hdl->obj_handle.type == REGULAR_FILE) {
        hdl->u.file.fd = -1;    /* no open on this yet */
        hdl->u.file.openflags = FSAL_O_CLOSED;
    } else if (hdl->obj_handle.type == SYMBOLIC_LINK && link_content != NULL) {
        size_t len = strlen (link_content) + 1;

        hdl->u.symlink.link_content = malloc (len);
        if (hdl->u.symlink.link_content == NULL) {
            goto spcerr;
        }
        memcpy (hdl->u.symlink.link_content, link_content, len);
        hdl->u.symlink.link_size = len;
    }
    hdl->obj_handle.export = exp_hdl;
    hdl->obj_handle.attributes.mask = exp_hdl->ops->fs_supported_attrs (exp_hdl);
    st = posix2fsal_attributes (stat, &hdl->obj_handle.attributes);
    if (FSAL_IS_ERROR (st))
        goto spcerr;
    if (!fsal_obj_handle_init (&hdl->obj_handle, exp_hdl, posix2fsal_type (stat->st_mode)))
        return hdl;

    hdl->obj_handle.ops = NULL;
    pthread_mutex_unlock (&hdl->obj_handle.lock);
    pthread_mutex_destroy (&hdl->obj_handle.lock);
  spcerr:
    if (hdl->obj_handle.type == SYMBOLIC_LINK) {
        xfree (hdl->u.symlink.link_content);
    }
    free (hdl);                 /* elvis has left the building */
    return NULL;
}

static struct file_data *nodedb_handle (unsigned long long fsid, struct stat *st, struct handle_data *f_handle_parent, const char *name)
{
    struct file_data child;
    memset (&child, '\0', sizeof (child));
    nodedb_stat_to_file_data (fsid, st, &child);
    return MARSHAL_nodedb_add (connpool, &child, f_handle_parent, (char *) name);       /* FIXME: try cleanly remove warning */
}

static struct file_data *get_dir_path (struct fsal_obj_handle *dir_hdl, char **p, int *retval, unsigned long long *fsid, struct stat *st_)
{
    struct file_data *parent;
    struct posix_fsal_obj_handle *myself;
    myself = container_of (dir_hdl, struct posix_fsal_obj_handle, obj_handle);
    parent = MARSHAL_nodedb_clean_stale_paths (connpool, myself->handle, p, retval, fsid, st_);
    if (!parent)
        return NULL;
    return parent;
}

static fsal_status_t lookup (struct fsal_obj_handle *dir_hdl,
			     const struct req_op_context *opctx,
			     const char *name,
			     struct fsal_obj_handle **handle)
{
    struct posix_fsal_obj_handle *hdl = NULL;
    char *link_content = NULL;
    struct file_data *parent = NULL, *child = NULL;
    char *dirpart = NULL;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    char *p = NULL, *path = NULL;
    int retval = 0;
    struct stat stat, st_;
    unsigned long long fsid = 0LL;        /* initialize to 0 to prevent valgrind warning */

    *handle = NULL;

    if (!dir_hdl->ops->handle_is (dir_hdl, DIRECTORY)) {
        LogCrit (COMPONENT_FSAL, "Parent handle is not a directory. hdl = 0x%p", dir_hdl);
        return fsalstat (ERR_FSAL_NOTDIR, 0);
    }

    if (!(parent = get_dir_path (dir_hdl, &p, &retval, &fsid, &st_))) {
        fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
        goto out;
    }

    path = dir_entry_name_cat (p, name);
    xfree (p);

    if ((retval = lstat (path, &stat)) < 0) {
        retval = errno;
        goto posixerror;
    }

    if (!(child = nodedb_handle (fsid, &stat, &parent->handle, name))) {
        fsal_error = ERR_FSAL_STALE;
        retval = 0;
        goto out;
    }

    if (S_ISLNK (stat.st_mode)) {
        ssize_t retlink;
        link_content = malloc (PATH_MAX + 1);
        retlink = readlink (path, link_content, PATH_MAX);
        if (retlink < 0 || retlink == PATH_MAX) {
            retval = errno;
            if (retlink == PATH_MAX)
                retval = ENAMETOOLONG;
            goto posixerror;
        }
        link_content[retlink] = '\0';
    }
    /* allocate an obj_handle and fill it up */
    hdl = alloc_handle_ (&child->handle, &stat, link_content, dir_hdl->export);
    if (!hdl) {
        fsal_error = ERR_FSAL_NOMEM;
        goto out;
    }
    *handle = &hdl->obj_handle;

    fsal_error = ERR_FSAL_NO_ERROR;
    retval = 0;

    goto out;


  posixerror:
    fsal_error = posix2fsal_error (retval);

  out:

    xfree (link_content);
    xfree (parent);
    xfree (child);
    xfree (dirpart);
    xfree (path);

    return fsalstat (fsal_error, retval);
}


/* returns -1 on stale, errno on error, and 0 on success */
static int posix_make_file_safe (const char *path, struct file_data *parent, const char *name, const mode_t * unix_mode,
                                 uid_t user, gid_t group, struct handle_data *d, unsigned long long fsid, struct stat *stat)
{
    struct file_data *child;

    if (lchown (path, user, group) < 0)
        return errno;
    if (unix_mode)      /* links have no mode of their own */
        if (chmod (path, *unix_mode) < 0)
            return errno;
    if (lstat (path, stat) < 0)
        return errno;
    if (!(child = nodedb_handle (fsid, stat, &parent->handle, (char *) name)))
        return -1;
    *d = child->handle;
    free (child);
    return 0;
}

static fsal_status_t make_thang (const char *thang, struct fsal_obj_handle *dir_hdl,
                                 const char *name, struct attrlist *attrib,
                                 struct fsal_obj_handle **handle, const char *link_path,
                                 int (*mk_func) (const char *path, void *hook),
                                 void (*rm_func) (const char *path), void *hook)
{
    char *path = NULL;
    struct stat st_;
    char *p = NULL;
    struct posix_fsal_obj_handle *hdl;
    struct file_data *parent = NULL;
    mode_t unix_mode;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;
    uid_t user;
    gid_t group;
    struct handle_data d;
    unsigned long long fsid = 0LL;        /* initialize to 0 to prevent valgrind warning */

    *handle = NULL;             /* poison it */

    if (!dir_hdl->ops->handle_is (dir_hdl, DIRECTORY)) {
        LogCrit (COMPONENT_FSAL, "Parent handle is not a directory. hdl = 0x%p", dir_hdl);
        return fsalstat (ERR_FSAL_NOTDIR, 0);
    }

    user = attrib->owner;
    group = attrib->group;
    unix_mode = fsal2unix_mode (attrib->mode) & ~dir_hdl->export->ops->fs_umask (dir_hdl->export);

    if (!(parent = get_dir_path (dir_hdl, &p, &retval, &fsid, &st_))) {
        fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
        goto out;
    }

    if (st_.st_mode & S_ISGID)
        group = -1;
    path = dir_entry_name_cat (p, name);
    xfree (p);

    if ((*mk_func) (path, hook)) {
        retval = errno;
        fsal_error = posix2fsal_error (retval);
        goto out;
    }

    memset (&st_, '\0', sizeof (st_));
    retval = posix_make_file_safe (path, parent, name, link_path ? NULL : &unix_mode, user, group, &d, fsid, &st_);
    if (retval) {
        if (retval == -1) {
            fsal_error = ERR_FSAL_STALE;
            retval = 0;
        }
        goto fileerr;
    }

    hdl = alloc_handle_ (&d, &st_, link_path, dir_hdl->export);
    if (hdl != NULL) {
        *handle = &hdl->obj_handle;
    } else {
        fsal_error = ERR_FSAL_NOMEM;
        goto out;
    }
    fsal_error = ERR_FSAL_NO_ERROR;
    retval = 0;

    *attrib = hdl->obj_handle.attributes;

    goto out;

  fileerr:
    fsal_error = posix2fsal_error (retval);
    unlink (path);              /*  cleanup */
    (*rm_func) (path);

  out:
    xfree (path);
    xfree (parent);
    return fsalstat (fsal_error, retval);
}

static int create_mk_func (const char *path, void *hook)
{
    int fd;
    fd = open (path, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0000);
    if (fd < 0)
        return 1;
    close (fd);
    return 0;
}

static void create_rm_func (const char *path)
{
    unlink (path);
}

static fsal_status_t create (struct fsal_obj_handle *dir_hdl,
			     const struct req_op_context *opctx,
			     const char *name,
			     struct attrlist *attrib,
                             struct fsal_obj_handle **handle)
{
    return make_thang ("file", dir_hdl, name, attrib, handle, NULL, create_mk_func, create_rm_func, NULL);
}


static int makedir_mk_func (const char *path, void *hook)
{
    return mkdir (path, 0000);
}

static void makedir_rm_func (const char *path)
{
    rmdir (path);
}

static fsal_status_t makedir (struct fsal_obj_handle *dir_hdl,
			      const struct req_op_context *opctx,
			      const char *name,
			      struct attrlist *attrib,
                              struct fsal_obj_handle **handle)
{
    return make_thang ("directory", dir_hdl, name, attrib, handle, NULL, makedir_mk_func, makedir_rm_func, NULL);
}

struct makenode_hook {
    object_file_type_t nodetype;
    fsal_dev_t *dev;
};

static int makenode_mk_func (const char *path, void *hook)
{
    struct makenode_hook *h;
    mode_t create_mode;
    dev_t unix_dev = 0;

    h = (struct makenode_hook *) hook;

    switch (h->nodetype) {
    case BLOCK_FILE:
        create_mode = S_IFBLK;
        unix_dev = makedev (h->dev->major, h->dev->minor);
        break;
    case CHARACTER_FILE:
        create_mode = S_IFCHR;
        unix_dev = makedev (h->dev->major, h->dev->minor);
        break;
    case FIFO_FILE:
        create_mode = S_IFIFO;
        break;
    case SOCKET_FILE:
        create_mode = S_IFSOCK;
        break;
    default:
        assert (!"bad nodetype in makenode");
    }

    return mknod (path, create_mode, unix_dev);
}

static void makenode_rm_func (const char *path)
{
    unlink (path);
}

static fsal_status_t makenode (struct fsal_obj_handle *dir_hdl,
			       const struct req_op_context *opctx,
			       const char *name,
			       object_file_type_t nodetype,
                               fsal_dev_t * dev,
			       struct attrlist *attrib,
			       struct fsal_obj_handle **handle)
{
    struct makenode_hook hook;

    switch (nodetype) {
    case BLOCK_FILE:
        if (!dev)
            return fsalstat (ERR_FSAL_INVAL, 0);
    case CHARACTER_FILE:
        if (!dev)
            return fsalstat (ERR_FSAL_INVAL, 0);
    case FIFO_FILE:
    case SOCKET_FILE:
        break;
    default:
        LogMajor (COMPONENT_FSAL, "Invalid node type in FSAL_mknode: %d", nodetype);
        return fsalstat (ERR_FSAL_INVAL, 0);
    }

    hook.nodetype = nodetype;
    hook.dev = dev;

    return make_thang ("device", dir_hdl, name, attrib, handle, NULL, makenode_mk_func, makenode_rm_func, &hook);
}


struct makesymlink_hook {
    const char *link_path;
};

static int makesymlink_mk_func (const char *path, void *hook)
{
    struct makesymlink_hook *h;

    h = (struct makesymlink_hook *) hook;

    return symlink (h->link_path, path);
}

static void makesymlink_rm_func (const char *path)
{
    unlink (path);
}

static fsal_status_t makesymlink (struct fsal_obj_handle *dir_hdl,
				  const struct req_op_context *opctx,
				  const char *name, const char *link_path,
                                  struct attrlist *attrib, struct fsal_obj_handle **handle)
{
    struct makesymlink_hook hook;

    hook.link_path = link_path;

    return make_thang ("symlink", dir_hdl, name, attrib, handle, link_path, makesymlink_mk_func, makesymlink_rm_func,
                       &hook);
}

static fsal_status_t readsymlink (struct fsal_obj_handle *obj_hdl,
                                  const struct req_op_context *opctx,
                                  struct gsh_buffdesc *link_content, bool refresh)
{
    struct posix_fsal_obj_handle *myself = NULL;
    int retval = 0;
    char *path = NULL;
    struct file_data *child = NULL;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

    if (obj_hdl->type != SYMBOLIC_LINK) {
        fsal_error = ERR_FSAL_FAULT;
        goto out;
    }
    myself = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);
    if (refresh) {              /* lazy load or LRU'd storage */
        ssize_t retlink;
        char link_buff[1024];

        xfree (myself->u.symlink.link_content);
        myself->u.symlink.link_size = 0;

        child = MARSHAL_nodedb_clean_stale_paths (connpool, myself->handle, &path, &retval, NULL, NULL);
        if (!child) {
            fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
            goto out;
        }

        retlink = readlink (path, link_buff, 1024);
        if (retlink < 0) {
            retval = errno;
            fsal_error = posix2fsal_error (retval);
            goto out;
        }

        myself->u.symlink.link_content = malloc (retlink + 1);
        if (myself->u.symlink.link_content == NULL) {
            fsal_error = ERR_FSAL_NOMEM;
            goto out;
        }
        memcpy (myself->u.symlink.link_content, link_buff, retlink);
        myself->u.symlink.link_content[retlink] = '\0';
        myself->u.symlink.link_size = retlink + 1;
    }
    if (myself->u.symlink.link_content == NULL) {
        fsal_error = ERR_FSAL_FAULT;    /* probably a better error?? */
        goto out;
    }
    link_content->addr = gsh_malloc(myself->u.symlink.link_size);
    if (link_content->addr == NULL) {
        fsal_error = ERR_FSAL_NOMEM;
        goto out;
    }
    memcpy(link_content->addr, myself->u.symlink.link_content, myself->u.symlink.link_size);
    link_content->len = myself->u.symlink.link_size;

  out:
    xfree (child);
    xfree (path);
    return fsalstat (fsal_error, retval);
}


struct dirlist {
    struct dirlist *next;
    char *name;
    int filetype;
};

static struct dirlist *new_dirent (const char *dirpath, const char *name)
{
    struct dirlist *n = NULL;
    char *path;
    struct stat st;
    path = dir_entry_name_cat (dirpath, name);
    if (!lstat (path, &st)) {
        n = malloc (sizeof (*n));
        memset (n, '\0', sizeof (*n));
        n->name = malloc (strlen (name) + 1);
        strcpy (n->name, name);
        n->filetype = nodedb_stat_to_file_type (&st);
    }
    free (path);
    return n;
}

static fsal_status_t read_dirents(struct fsal_obj_handle *dir_hdl,
                                  const struct req_op_context *opctx,
                                  fsal_cookie_t *whence,
                                  void *dir_state,
                                  fsal_readdir_cb cb,
                                  bool *eof)
{
    struct file_data *parent = NULL;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    fsal_status_t status = {0, 0};
    int retval = 0;
    char *p = NULL;
    DIR *dir = NULL;
    struct dirent *d = NULL, *dir_buf = NULL;
    struct dirlist *first = NULL, *last = NULL, *i, *next;
    long offset = 0;

    if (whence) {
        offset = (long)*whence;
    }

    if (!(parent = get_dir_path (dir_hdl, &p, &retval, NULL, NULL))) {
        fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
        goto out;
    }

    dir = opendir (p);
    seekdir (dir, offset);

    dir_buf = malloc (sizeof (*dir_buf) + 256); /* more or less */

    for (;;) {
        struct dirlist *n;
        int r;
        memset (dir_buf, '\0', sizeof (sizeof (*dir_buf) + 256));
        r = readdir_r (dir, dir_buf, &d);
        if (r) {
            retval = r;
            goto out;
        }
        if (!d)
            break;

        if (!strcmp (dir_buf->d_name, ".") || !strcmp (dir_buf->d_name, ".."))
            continue;

        if ((n = new_dirent (p, dir_buf->d_name))) {
            if (!last) {
                first = last = n;
            } else {
                assert (!last->next);
                last->next = n;
                last = last->next;
            }
        }
    }

    offset = telldir(dir);
    xclosedir (dir);

    for (i = first; i; i = i->next) {
        if (!cb(opctx, i->name, dir_state, (fsal_cookie_t)offset))
        if (FSAL_IS_ERROR (status)) {
            fsal_error = 0;
            retval = 0;
            goto out;
        }
    }

    *eof = 1;

  out:

    xfree (dir_buf);
    xfree (parent);
    xclosedir (dir);

    for (i = first; i; i = next) {
        next = i->next;
        free (i->name);
        free (i);
    }

    return fsalstat (fsal_error, retval);
}


static fsal_status_t renamefile (struct fsal_obj_handle *olddir_hdl,
                                 const struct req_op_context *opctx,
                                 const char *old_name, struct fsal_obj_handle *newdir_hdl, const char *new_name)
{
    struct posix_fsal_obj_handle *olddir_handle;
    struct posix_fsal_obj_handle *newdir_handle;
    int retval = 0;

    olddir_handle = container_of (olddir_hdl, struct posix_fsal_obj_handle, obj_handle);
    newdir_handle = container_of (newdir_hdl, struct posix_fsal_obj_handle, obj_handle);

    retval = MARSHAL_nodedb_rename (connpool, olddir_handle->handle, (char *) old_name, newdir_handle->handle, (char *) new_name);
    if (retval == 1)
        return fsalstat (ERR_FSAL_STALE, 0);
    if (retval)
        return fsalstat (posix2fsal_error (-retval), -retval);
    return fsalstat (ERR_FSAL_NO_ERROR, 0);
}

static fsal_status_t linkfile (struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx,
			       struct fsal_obj_handle *destdir_hdl, const char *name)
{
    struct posix_fsal_obj_handle *child_handle;
    struct posix_fsal_obj_handle *newdir_handle;
    int retval = 0;

    if (!obj_hdl->export->ops->fs_supports (obj_hdl->export, fso_link_support))
        return fsalstat (ERR_FSAL_NOTSUPP, 0);

    child_handle = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);
    newdir_handle = container_of (destdir_hdl, struct posix_fsal_obj_handle, obj_handle);

    retval = MARSHAL_nodedb_link (connpool, child_handle->handle, newdir_handle->handle, (char *) name);
    if (retval == 1)
        return fsalstat (ERR_FSAL_STALE, 0);
    if (retval)
        return fsalstat (posix2fsal_error (-retval), -retval);
    return fsalstat (ERR_FSAL_NO_ERROR, 0);
}


static fsal_status_t getattrs (struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx)
{
    struct file_data *parent = NULL;
    char *path = NULL;
    struct posix_fsal_obj_handle *myself;
    struct stat stat;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    fsal_status_t st;
    int retval = 0;

    myself = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);

    if (obj_hdl->type == REGULAR_FILE && myself->u.file.fd >= 0) {
        retval = fstat (myself->u.file.fd, &stat);
    } else {
        parent = MARSHAL_nodedb_clean_stale_paths (connpool, myself->handle, &path, &retval, NULL, &stat);
        if (!parent) {
            fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
            goto out;
        }
        xfree (parent);
    }

    if (retval < 0)
        goto errout;

    /* convert attributes */
    st = posix2fsal_attributes (&stat, &obj_hdl->attributes);
    if (FSAL_IS_ERROR (st)) {
        FSAL_CLEAR_MASK(obj_hdl->attributes.mask);
        FSAL_SET_MASK(obj_hdl->attributes.mask, ATTR_RDATTR_ERR);
        fsal_error = st.major;
        retval = st.minor;
        goto out;
    }
    goto out;

  errout:
    retval = errno;
    if (retval == ENOENT) {
        fsal_error = ERR_FSAL_STALE;
    } else {
        fsal_error = posix2fsal_error (retval);
    }
  out:
    xfree (path);
    return fsalstat (fsal_error, retval);
}


static fsal_status_t setattrs (struct fsal_obj_handle *obj_hdl,
			       const struct req_op_context *opctx,
			       struct attrlist *attrs)
{
    struct file_data *parent = NULL;
    char *path = NULL;
    struct posix_fsal_obj_handle *myself;
    struct stat stat;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;

    /* apply umask, if mode attribute is to be changed */
    if (FSAL_TEST_MASK (attrs->mask, ATTR_MODE)) {
        attrs->mode &= ~obj_hdl->export->ops->fs_umask (obj_hdl->export);
    }

    myself = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);

    parent = MARSHAL_nodedb_clean_stale_paths (connpool, myself->handle, &path, &retval, NULL, &stat);
    if (!parent) {
        fsal_error = retval ? posix2fsal_error (retval) : ERR_FSAL_STALE;
        goto out;
    }
    xfree (parent);

    if (retval < 0) {
        retval = errno;
        fsal_error = posix2fsal_error (retval);
        goto out;
    }
    /** TRUNCATE **/
    if (FSAL_TEST_MASK (attrs->mask, ATTR_SIZE)) {
        if (obj_hdl->type != REGULAR_FILE) {
            fsal_error = ERR_FSAL_INVAL;
            goto fileerr;
        }
        retval = truncate (path, attrs->filesize);
        if (retval != 0) {
            goto fileerr;
        }
    }
    /** CHMOD **/
    if (FSAL_TEST_MASK (attrs->mask, ATTR_MODE)) {
        /* The POSIX chmod call doesn't affect the symlink object, but
         * the entry it points to. So we must ignore it.
         */
        if (!S_ISLNK (stat.st_mode)) {
            retval = chmod (path, fsal2unix_mode (attrs->mode));
            if (retval != 0) {
                goto fileerr;
            }
        }
    }

        /**  CHOWN  **/
    if (FSAL_TEST_MASK (attrs->mask, ATTR_OWNER | ATTR_GROUP)) {
        uid_t user = FSAL_TEST_MASK (attrs->mask, ATTR_OWNER)
            ? (int) attrs->owner : -1;
        gid_t group = FSAL_TEST_MASK (attrs->mask, ATTR_GROUP)
            ? (int) attrs->group : -1;

        retval = lchown (path, user, group);
        if (retval) {
            goto fileerr;
        }
    }

        /**  UTIME  **/
    if (FSAL_TEST_MASK (attrs->mask, ATTR_ATIME | ATTR_MTIME | ATTR_MTIME_SERVER | ATTR_ATIME_SERVER)) {
        struct timeval timebuf[2];
        struct timeval *ptimebuf = timebuf;

        /* Atime */
        timebuf[0].tv_sec = (FSAL_TEST_MASK (attrs->mask, ATTR_ATIME) ? (time_t) attrs->atime.tv_sec : stat.st_atime);
        timebuf[0].tv_usec = 0;

        /* Mtime */
        timebuf[1].tv_sec = (FSAL_TEST_MASK (attrs->mask, ATTR_MTIME) ? (time_t) attrs->mtime.tv_sec : stat.st_mtime);
        timebuf[1].tv_usec = 0;
        if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER) &&
                FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER))
        {
            /* If both times are set to server time, we can shortcut and
             * use the utimes interface to set both times to current time.
             */
            ptimebuf = NULL;
        }
        else
        {
            if(FSAL_TEST_MASK(attrs->mask, ATTR_ATIME_SERVER))
            {
                /* Since only one time is set to server time, we must
                 * get time of day to set it.
                 */
                gettimeofday(&timebuf[0], NULL);
            }
            if(FSAL_TEST_MASK(attrs->mask, ATTR_MTIME_SERVER))
            {
                /* Since only one time is set to server time, we must
                 * get time of day to set it.
                 */
                gettimeofday(&timebuf[1], NULL);
            }
        }

        retval = utimes (path, ptimebuf);
        if (retval != 0) {
            goto fileerr;
        }
    }
    xfree (path);
    return fsalstat (fsal_error, retval);

  fileerr:
    retval = errno;
    fsal_error = posix2fsal_error (retval);
  out:
    xfree (path);
    return fsalstat (fsal_error, retval);
}


static fsal_status_t file_unlink (struct fsal_obj_handle *dir_hdl,
				  const struct req_op_context *opctx,
				  const char *name)
{
    struct posix_fsal_obj_handle *myself;
    int retval = 0;

    myself = container_of (dir_hdl, struct posix_fsal_obj_handle, obj_handle);
    retval = MARSHAL_nodedb_unlink (connpool, myself->handle, (char *) name);
    if (retval == 1)
        return fsalstat (ERR_FSAL_STALE, 0);
    if (retval)
        return fsalstat (posix2fsal_error (-retval), -retval);
    return fsalstat (ERR_FSAL_NO_ERROR, 0);
}


static fsal_status_t handle_digest (const struct fsal_obj_handle *obj_hdl,
                                    fsal_digesttype_t output_type, struct gsh_buffdesc *fh_desc)
{
    uint32_t ino32;
    uint64_t ino64;
    const struct posix_fsal_obj_handle *myself;
    const struct handle_data *fh;
    size_t fh_size;

    /* sanity checks */
    if (!fh_desc)
        return fsalstat (ERR_FSAL_FAULT, 0);
    myself = container_of (obj_hdl, const struct posix_fsal_obj_handle, obj_handle);
    fh = myself->handle;

    switch (output_type) {
    case FSAL_DIGEST_NFSV2:
    case FSAL_DIGEST_NFSV3:
    case FSAL_DIGEST_NFSV4:
        fh_size = sizeof (struct handle_data);
        if (fh_desc->len < fh_size)
            goto errout;
        memcpy (fh_desc->addr, fh, fh_size);
        break;
    case FSAL_DIGEST_FILEID2:
        return fsalstat (ERR_FSAL_SERVERFAULT, 0);      /* NFSv2 no longer supported */
    case FSAL_DIGEST_FILEID3:
        fh_size = FSAL_DIGEST_SIZE_FILEID3;
        if (fh_desc->len < fh_size)
            goto errout;
        ino64 = fh->inode;
        ino32 = fh->inode;
        if (fh_size == sizeof(ino64))
            memcpy (fh_desc->addr, &ino64, sizeof(ino64));
        else
            memcpy (fh_desc->addr, &ino32, sizeof(ino32));
        break;
    case FSAL_DIGEST_FILEID4:
        fh_size = FSAL_DIGEST_SIZE_FILEID4;
        if (fh_desc->len < fh_size)
            goto errout;
        ino64 = fh->inode;
        ino32 = fh->inode;
        if (fh_size == sizeof(ino64))
            memcpy (fh_desc->addr, &ino64, sizeof(ino64));
        else
            memcpy (fh_desc->addr, &ino32, sizeof(ino32));
        break;
    default:
        return fsalstat (ERR_FSAL_SERVERFAULT, 0);
    }
    fh_desc->len = fh_size;
    return fsalstat (ERR_FSAL_NO_ERROR, 0);

  errout:
    LogMajor (COMPONENT_FSAL, "Space too small for handle.  need %lu, have %lu", fh_size, fh_desc->len);
    return fsalstat (ERR_FSAL_TOOSMALL, 0);
}


static void handle_to_key (struct fsal_obj_handle *obj_hdl, struct gsh_buffdesc *fh_desc)
{
    struct posix_fsal_obj_handle *myself;

    myself = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);
    fh_desc->addr = myself->handle;
    fh_desc->len = sizeof (struct handle_data);
}


static fsal_status_t release (struct fsal_obj_handle *obj_hdl)
{
    struct posix_fsal_obj_handle *myself;
    int retval = 0;
    object_file_type_t type = obj_hdl->type;

    myself = container_of (obj_hdl, struct posix_fsal_obj_handle, obj_handle);

    if(type == REGULAR_FILE &&
       (myself->u.file.fd >=0 || myself->u.file.openflags != FSAL_O_CLOSED)) {
            LogCrit(COMPONENT_FSAL,
                    "Tried to release busy handle, "
                    "hdl = 0x%p, fd = %d, openflags = 0x%x",
                    obj_hdl,
                    myself->u.file.fd, myself->u.file.openflags);
            return fsalstat(posix2fsal_error(EINVAL), EINVAL);
    }

    retval = fsal_obj_handle_uninit(obj_hdl);
    if (retval != 0) {
            LogCrit(COMPONENT_FSAL,
                    "Tried to release busy handle, "
                    "hdl = 0x%p->refs = %d",
                    obj_hdl, obj_hdl->refs);
            return fsalstat(posix2fsal_error(retval), retval);
    }

    if (type == SYMBOLIC_LINK) {
        xfree (myself->u.symlink.link_content);
        myself->u.symlink.link_size = 0;
    }
    free (myself);
    return fsalstat (ERR_FSAL_NO_ERROR, 0);
}

void posix_handle_ops_init (struct fsal_obj_ops *ops)
{
    ops->release = release;
    ops->lookup = lookup;
    ops->readdir = read_dirents;
    ops->create = create;
    ops->mkdir = makedir;
    ops->mknode = makenode;
    ops->symlink = makesymlink;
    ops->readlink = readsymlink;
    ops->test_access = fsal_test_access;
    ops->getattrs = getattrs;
    ops->setattrs = setattrs;
    ops->link = linkfile;
    ops->rename = renamefile;
    ops->unlink = file_unlink;
    ops->open = posix_open;
    ops->status = posix_status;
    ops->read = posix_read;
    ops->write = posix_write;
    ops->commit = posix_commit;
    ops->lock_op = posix_lock_op;
    ops->close = posix_close;
    ops->lru_cleanup = posix_lru_cleanup;
    ops->handle_digest = handle_digest;
    ops->handle_to_key = handle_to_key;
}


fsal_status_t posix_lookup_path (struct fsal_export *exp_hdl,
				 const struct req_op_context *opctx,
				 const char *path,
				 struct fsal_obj_handle **handle)
{
    char *link_content = NULL;
    struct file_data *parent = NULL, *child = NULL;
    char *basepart = NULL;
    char *dirpart = NULL;
    struct posix_fsal_obj_handle *hdl = NULL;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;

    char **s = NULL;
    char *p = NULL;
    int i;
    int retval = 0;
    struct stat stat;

    *handle = NULL;

    if (path == NULL || path[0] != '/' || strlen (path) > PATH_MAX || strlen (path) < 2) {
        fsal_error = ERR_FSAL_INVAL;
        goto out;
    }

    s = nodedb_strsplit (path, '/', 1024000);

    for (i = 0; s[i]; i++) {
        unsigned long long fsid = 0LL;    /* initialize to 0 to prevent valgrind warning */
        char *t;
        if (!s[i][0])
            continue;
        t = dirpart;
        dirpart = p;
        p = dir_entry_name_cat (p, s[i]);
        xfree (t);
        if ((retval = lstat (p, &stat)) < 0) {
            retval = errno;
            goto posixerror;
        }
        MARSHAL_nodedb_get_fsid (connpool, p, &fsid);
        if (s[i + 1] && !S_ISDIR (stat.st_mode)) {
            retval = ENOTDIR;
            goto posixerror;
        }
        xfree (parent);
        parent = child;
        basepart = s[i];
        if (!(child = nodedb_handle (fsid, &stat, &parent->handle, basepart))) {
            retval = errno;
            goto posixerror;
        }
    }
    assert (child);
    assert (basepart);

    if (S_ISLNK (stat.st_mode)) {
        ssize_t retlink;
        link_content = malloc (PATH_MAX + 1);
        retlink = readlink (path, link_content, PATH_MAX);
        if (retlink < 0 || retlink == PATH_MAX) {
            retval = errno;
            if (retlink == PATH_MAX)
                retval = ENAMETOOLONG;
            goto posixerror;
        }
        link_content[retlink] = '\0';
    }

    /* allocate an obj_handle and fill it up */
    hdl = alloc_handle_ (&child->handle, &stat, link_content, exp_hdl);
    if (!hdl) {
        fsal_error = ERR_FSAL_NOMEM;
        goto out;
    }
    *handle = &hdl->obj_handle;


    fsal_error = ERR_FSAL_NO_ERROR;
    retval = 0;

    goto out;


  posixerror:
    fsal_error = posix2fsal_error (retval);

  out:

    xfree (link_content);
    xfree (parent);
    xfree (child);
    xfree (dirpart);
    xfree (s);

    return fsalstat (fsal_error, retval);
}


fsal_status_t posix_create_handle (struct fsal_export * exp_hdl,
				   const struct req_op_context *opctx,
                                   struct gsh_buffdesc * hdl_desc,
				   struct fsal_obj_handle ** handle)
{
    struct posix_fsal_obj_handle *hdl;
    struct stat stat;
    struct handle_data *fh, fh_;
    struct file_data g, *child = NULL;
    fsal_errors_t fsal_error = ERR_FSAL_NO_ERROR;
    int retval = 0;
    char *link_content = NULL, *p = NULL;
    ssize_t retlink;
    char link_buff[PATH_MAX + 1];
    unsigned long long fsid = 0LL;        /* initialize to 0 to prevent valgrind warning */

    *handle = NULL;             /* poison it first */
    if (hdl_desc->len != sizeof (struct handle_data))
        return fsalstat (ERR_FSAL_FAULT, 0);

    memcpy (&fh_, hdl_desc->addr, hdl_desc->len);
    fh = &fh_;

    child = MARSHAL_nodedb_get_first_path_from_handle (connpool, fh, &p);
    if (!child) {
        fsal_error = ERR_FSAL_STALE;
        retval = 0;
        goto errout;
    }
    MARSHAL_nodedb_get_fsid (connpool, p, &fsid);

    retval = lstat (p, &stat);
    if (retval < 0) {
        retval = errno;
        fsal_error = posix2fsal_error (retval);
        goto errout;
    }

    nodedb_stat_to_file_data (fsid, &stat, &g);
    if (!FILE_DATA_EQUAL (&g, child)) {
        fsal_error = ERR_FSAL_STALE;
        retval = 0;
        goto errout;
    }

    if (S_ISLNK (stat.st_mode)) {       /* I could lazy eval this... */
        retlink = readlink (p, link_buff, PATH_MAX);
        if (retlink < 0 || retlink == PATH_MAX) {
            retval = errno;
            if (retlink == PATH_MAX)
                retval = ENAMETOOLONG;
            fsal_error = posix2fsal_error (retval);
            goto errout;
        }
        link_buff[retlink] = '\0';
        link_content = &link_buff[0];
    }

    hdl = alloc_handle_ (&child->handle, &stat, link_content, exp_hdl);
    if (hdl == NULL) {
        fsal_error = ERR_FSAL_NOMEM;
        goto errout;
    }
    *handle = &hdl->obj_handle;

  errout:
    xfree (p);
    xfree (child);
    return fsalstat (fsal_error, retval);
}

