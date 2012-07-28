/*
 * Copyright (C) Paul Sheer, 2012
 * Author: Paul Sheer paulsheer@gmail.com
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


#include <pthread.h>



#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "redblack.h"
#include "sockbuf.h"
#include "nodedb.h"
#include "marshal.h"
#include "connection.h"
#include "connectionpool.h"
#include "interface.h"


#define MARSHALATOMIC                   


enum object_file_type_enum {
    NO_FILE_TYPE = 0,		/* sanity check to ignore type */
    REGULAR_FILE = 1,
    CHARACTER_FILE = 2,
    BLOCK_FILE = 3,
    SYMBOLIC_LINK = 4,
    SOCKET_FILE = 5,
    FIFO_FILE = 6,
    DIRECTORY = 7,
    FS_JUNCTION = 8,
    EXTENDED_ATTR = 9
};

static long long global_handle = 1;

struct hardlink_list {
    struct dir_entry *first;             /* must be first member */
    int count;
};

struct inode_entry {
    struct redblack_node rb_handle;
    struct redblack_node rb_inode;
    struct redblack_node rb_accesstime;
    unsigned long long accesstime;
    struct file_data *file_data;
    struct hardlink_list links;
};

struct dir_entry {
    struct dir_entry *next;             /* must be first member */
    struct redblack_node rb_name;
    char *name;
    struct redblack_tree rb_dirlist;
    struct dir_entry *parent;
    struct inode_entry *inode_entry;
};

static void inode_entry_link(struct hardlink_list *l, struct dir_entry *d)
{
    d->next = l->first;
    l->first = d;
    l->count++;
}

static void inode_entry_unlink (struct hardlink_list *l, struct dir_entry *d)
{
    struct dir_entry *hardlink;
    for (hardlink = (struct dir_entry *) l; hardlink->next; hardlink = hardlink->next) {
        if (hardlink->next == d) {
            hardlink->next = hardlink->next->next;
            l->count--;
            return;
        }
    }
    assert (!"hardlink not found");
}

static struct dir_entry *first_hardlink(struct inode_entry *n)
{
    return n->links.first;
}

struct nodedb {
    pthread_mutex_t mutex;
    struct dir_entry *root;
    struct redblack_tree rb_handle;
    struct redblack_tree rb_inode;
    struct redblack_tree rb_accesstime;
};


void nodedb_lock (struct nodedb *db)
{
    pthread_mutex_lock (&db->mutex);
}

void nodedb_unlock (struct nodedb *db)
{
    pthread_mutex_unlock (&db->mutex);
}


/* string number-limit multi-character seperator split */
char **strsplit (const char *s, char c, int max_split)
{
    char **r, *buf, *_buf;
    int i = 0, n = 1, l;
    const char *t;
    for (t = s; *t; t++) {
        if (*t == c)
            n++;
        if (n > max_split)
            break;
    }
    l = sizeof (char *) * (n + 1) + strlen (s) + 1;
    _buf = (char *) malloc (l + 1);
    _buf[l] = '~';
    r = (char **) _buf;
    buf = _buf + sizeof (char *) * (n + 1);
    strcpy (buf, s);
    while (i < n) {
        r[i++] = buf;
        while (*buf && *buf != c)
            buf++;
        if (!*buf)
            break;
        if (i >= max_split + 1)
            break;
        *buf++ = '\0';
    }
    r[i] = NULL;
    assert (_buf[l] == '~');
    return r;
}


#ifndef S_IFMT
#define S_IFMT                  0170000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK                0140000
#endif
#ifndef S_IFLNK
#define S_IFLNK                 0120000
#endif
#ifndef S_IFREG
#define S_IFREG                 0100000
#endif
#ifndef S_IFBLK
#define S_IFBLK                 0060000
#endif
#ifndef S_IFDIR
#define S_IFDIR                 0040000
#endif
#ifndef S_IFCHR
#define S_IFCHR                 0020000
#endif
#ifndef S_IFIFO
#define S_IFIFO                 0010000
#endif


static enum object_file_type_enum type_convert (unsigned long posix_type_in)
{
    switch (posix_type_in & S_IFMT) {
    case S_IFIFO:
	return FIFO_FILE;
    case S_IFCHR:
	return CHARACTER_FILE;
    case S_IFDIR:
	return DIRECTORY;
    case S_IFBLK:
	return BLOCK_FILE;
    case S_IFREG:
    case S_IFMT:
	return REGULAR_FILE;
    case S_IFLNK:
	return SYMBOLIC_LINK;
    case S_IFSOCK:
	return SOCKET_FILE;
    default:
	return 0;
    }
}

void nodedb_stat_to_file_data (const struct stat *st, struct file_data *file_data)
{
    file_data->devid = st->st_dev;
    file_data->inode = st->st_ino;
    file_data->extra.nlinks = st->st_nlink;
    file_data->extra.type = type_convert ((unsigned long) st->st_mode);
    file_data->extra.ctime = st->st_ctime;
}

static char *dir_entry_name_malloc (const char *name)
{
    char *r;
    int l;
    l = strlen (name);
    r = (char *) malloc (l + 1);
    strcpy (r, name);
    return r;
}

char *dir_entry_name_cat (const char *name1, const char *name2)
{
    char *r;
    int l1, l2;
    if (!name1)
	name1 = "";
    l1 = strlen (name1);
    l2 = strlen (name2);
    r = (char *) malloc (l1 + 1 + l2 + 1);
    strcpy (r, name1);
    if (!l1 || r[l1 - 1] != '/') {
	strcpy (r + l1, "/");
	strcpy (r + l1 + 1, name2);
    } else {
	strcpy (r + l1, name2);
    }
    return r;
}

static int rb_name_cmp (const void *a_, const void *b_, void *not_used)
{
    struct dir_entry *a, *b;
    a = (struct dir_entry *) a_;
    b = (struct dir_entry *) b_;

    return strcmp (a->name, b->name);
}

static int rb_handle_cmp (const void *a_, const void *b_, void *not_used)
{
    struct inode_entry *a, *b;
    a = (struct inode_entry *) a_;
    b = (struct inode_entry *) b_;

    if (a->file_data->handle.handleid < b->file_data->handle.handleid)
        return -1;
    if (a->file_data->handle.handleid > b->file_data->handle.handleid)
        return 1;

    if (a->file_data->handle.timestamp < b->file_data->handle.timestamp)
        return -1;
    if (a->file_data->handle.timestamp > b->file_data->handle.timestamp)
        return 1;

    return 0;
}

static int rb_inode_cmp (const void *a_, const void *b_, void *not_used)
{
    struct inode_entry *a, *b;
    a = (struct inode_entry *) a_;
    b = (struct inode_entry *) b_;

    if (a->file_data->devid < b->file_data->devid)
        return -1;
    if (a->file_data->devid > b->file_data->devid)
        return 1;

    if (a->file_data->inode < b->file_data->inode)
        return -1;
    if (a->file_data->inode > b->file_data->inode)
        return 1;

    return 0;
}

static int rb_accesstime_cmp (const void *a_, const void *b_, void *not_used)
{
    struct inode_entry *a, *b;
    a = (struct inode_entry *) a_;
    b = (struct inode_entry *) b_;

    if (a->accesstime < b->accesstime)
        return -1;
    if (a->accesstime > b->accesstime)
        return 1;

    return 0;
}

static long long nodedb_current_time (void)
{
    time_t t;
    time (&t);
    return (long long) t;
}

struct dir_entry *dir_entry_new (struct inode_entry *inode_entry)
{
    struct dir_entry *r;
    r = (struct dir_entry *) malloc (sizeof(*r));
    memset (r, '\0', sizeof (*r));
    r->inode_entry = inode_entry;
    redblack_tree_new (&r->rb_dirlist, offsetof (struct dir_entry, rb_name), 0, rb_name_cmp, NULL);
    return r;
}

void dir_entry_free (struct dir_entry *n)
{
    assert (n);
    if (n->name)
        free (n->name);
    free (n);
}

struct inode_entry *inode_entry_new(struct file_data *file_data)
{
    struct inode_entry *r;
    r = (struct inode_entry *) malloc (sizeof(*r));
    memset (r, '\0', sizeof (*r));
    r->file_data = file_data;
    r->accesstime = nodedb_current_time();
    return r;
}

#define inode_entry_free(a) _inode_entry_free(__FUNCTION__,a)

void _inode_entry_free (const char *function, struct inode_entry *n)
{
    assert (n);
    assert (n->file_data);

#if 0
printf("%s: freeing %llu:%llu %llu:%llu\n", function,
n->file_data->handle.handleid,
n->file_data->handle.timestamp,
n->file_data->devid,
n->file_data->inode
);
#endif

    free (n->file_data);
    free (n);
}

struct file_data *nodedb_new_file_data (const struct file_data *old_file_data);

struct nodedb *nodedb_new (void)
{
    struct inode_entry *c;
    struct dir_entry *d;
    struct nodedb *db;
    struct file_data the_root;

    db = (struct nodedb *) malloc (sizeof (*db));
    memset (db, '\0', sizeof (*db));

    pthread_mutex_init (&db->mutex, NULL);

    redblack_tree_new (&db->rb_handle, offsetof (struct inode_entry, rb_handle), 0, rb_handle_cmp, NULL);
    redblack_tree_new (&db->rb_inode, offsetof (struct inode_entry, rb_inode), 0, rb_inode_cmp, NULL);
    redblack_tree_new (&db->rb_accesstime, offsetof (struct inode_entry, rb_accesstime), 1, rb_accesstime_cmp, NULL);

/* add a dummy entry for the root node: */

    memset (&the_root, '\0', sizeof (the_root));
    c = inode_entry_new (nodedb_new_file_data (&the_root));
    redblack_tree_add (&db->rb_handle, c);
    redblack_tree_add (&db->rb_inode, c);
    redblack_tree_add (&db->rb_accesstime, c);
    d = dir_entry_new (c);
    d->name = dir_entry_name_malloc ("");
    inode_entry_link (&c->links, d);

    db->root = d;

    return db;
}

struct inode_entry *_nodedb_inode_entry_by_inode (struct nodedb *db, const struct file_data *file_data)
{
    struct inode_entry l, *r;
    struct file_data d;
    memset (&l, '\0', sizeof (l));
    d = *file_data;
    l.file_data = &d;
    r = redblack_tree_find (&db->rb_inode, &l);
    if (r) {
        redblack_tree_delete (&db->rb_accesstime, r);
        r->accesstime = nodedb_current_time ();
        redblack_tree_add (&db->rb_accesstime, r);
    }
    return r;
}

struct inode_entry *_nodedb_inode_entry_by_handle (struct nodedb *db, const struct file_data *file_data)
{
    struct file_data d;
    struct inode_entry l, *r;
    memset (&l, '\0', sizeof (l));
    d = *file_data;
    l.file_data = &d;
    r = redblack_tree_find (&db->rb_handle, &l);
    if (r) {
        redblack_tree_delete (&db->rb_accesstime, r);
        r->accesstime = nodedb_current_time ();
        redblack_tree_add (&db->rb_accesstime, r);
    }
    return r;
}

static char *_nodedb_build_path (struct nodedb *db, struct dir_entry *child)
{
    char *path, *new_path;

    for (path = dir_entry_name_malloc (child->name); child->parent; child = child->parent) {
        new_path = dir_entry_name_cat (child->parent->name, path);
        free (path);
        path = new_path;
    }

    return path;
}

struct file_data *nodedb_new_file_data (const struct file_data *old_file_data)
{
    struct file_data *r;
    r = (struct file_data *) malloc (sizeof (*r));

    if (old_file_data)
        *r = *old_file_data;

    r->handle.handleid = global_handle++;
    r->handle.timestamp = nodedb_current_time ();
    r->p = r;

    return r;
}

static struct dir_entry *find_existing_dir_entry (struct inode_entry *n, struct dir_entry *parent, const char *name)
{
    struct dir_entry *hardlink;
/* the comparison "hardlink->parent == parent" also handles the root node: NULL == NULL */
    for (hardlink = n->links.first; hardlink; hardlink = hardlink->next)
        if (!strcmp (hardlink->name, name) && hardlink->parent == parent)
            return hardlink;
    return NULL;
}

static void insert_dirlist (struct dir_entry *parent, struct dir_entry *child)
{
    int ok;
    assert (!child->parent);
    ok = redblack_tree_add (&parent->rb_dirlist, child);
    assert(ok);
    child->parent = parent;
}

static void remove_dirlist (struct dir_entry *parent, struct dir_entry *child)
{
    if (parent->inode_entry->file_data->extra.nlinks > 0)
        parent->inode_entry->file_data->extra.nlinks--;
    assert (child->parent == parent);
    redblack_tree_delete (&parent->rb_dirlist, child);
    child->parent = NULL;
}

#define _nodedb_recursive_free(a,b,c,d) __nodedb_recursive_free(__FUNCTION__,a,b,c,d)
static void __nodedb_recursive_free (const char *function, struct nodedb *db, struct dir_entry *parent, struct dir_entry *child, struct inode_entry **check_free);

static void _nodedb_recursive_delete (struct nodedb *db, struct dir_entry *child, struct inode_entry **check_free)
{
    struct dir_entry *grandchild;

    for (grandchild = redblack_tree_first (&child->rb_dirlist); grandchild;
         grandchild = redblack_tree_next (&child->rb_dirlist))
        _nodedb_recursive_free (db, child, grandchild, check_free);
}

static void __nodedb_recursive_free (const char *function, struct nodedb *db, struct dir_entry *parent, struct dir_entry *child, struct inode_entry **check_free)
{
    if (parent)                 /* so that this can be used to free the root node */
        remove_dirlist (parent, child);
    _nodedb_recursive_delete (db, child, check_free);
    if (child->inode_entry) {
        inode_entry_unlink (&child->inode_entry->links, child);

        if (!child->inode_entry->links.count) {
            redblack_tree_delete (&db->rb_handle, child->inode_entry);
            redblack_tree_delete (&db->rb_inode, child->inode_entry);
            redblack_tree_delete (&db->rb_accesstime, child->inode_entry);
            if (check_free && *check_free == child->inode_entry)
                *check_free = NULL;
            _inode_entry_free (function, child->inode_entry);
            child->inode_entry = NULL;
        }
    }
    dir_entry_free (child);
}

void nodedb_make_empty (struct nodedb *db)
{
    _nodedb_recursive_delete (db, db->root, NULL);
}

void nodedb_free (struct nodedb *db)
{
    _nodedb_recursive_free (db, NULL, db->root, NULL);
    pthread_mutex_destroy (&db->mutex);
    memset (db, '\0', sizeof (*db));
    free (db);
}

static struct dir_entry *_nodedb_dir_entry_by_name (struct nodedb *db, struct dir_entry *parent, const char *name)
{
    struct dir_entry l_direntry, *child;

    memset (&l_direntry, '\0', sizeof (l_direntry));
    l_direntry.name = dir_entry_name_malloc (name);
    child = (struct dir_entry *) redblack_tree_find (&parent->rb_dirlist, &l_direntry);
    free (l_direntry.name);

    return child;
}

/* returns 1 on child not found */
static int _nodedb_dir_entry_delete (struct nodedb *db, struct dir_entry *parent, const char *name)
{
    struct dir_entry *child;

    if ((child = _nodedb_dir_entry_by_name (db, parent, name))) {
        _nodedb_recursive_free (db, parent, child, NULL);
        return 0;
    }

    return 1;
}

static struct dir_entry *_nodedb_first_dir_entry_from_handle (struct nodedb *db, const struct file_data *file_data)
{
    struct inode_entry *p;
    if (!file_data)
        return db->root;
    if ((p = _nodedb_inode_entry_by_handle (db, file_data)))
        return first_hardlink (p);
    return NULL;
}


MARSHALATOMIC struct file_data *nodedb_clean_stale_paths (struct nodedb *db, const struct handle_data *f_handle, char **path_, struct stat *st_)
{
    struct inode_entry *p;
    struct dir_entry *hardlink, *next;
    struct file_data f;
    const struct file_data *file_data_;

    file_data_ = (const struct file_data *) f_handle;

    p = _nodedb_inode_entry_by_handle (db, file_data_);
    if (!p)
        return NULL;

    for (hardlink = p->links.first; hardlink; hardlink = next) {
        struct stat st;
        char *path;
        next = hardlink->next;
        path = _nodedb_build_path (db, hardlink);
        if (!lstat (path, &st)) {
            nodedb_stat_to_file_data (&st, &f);
            if (FILE_DATA_EQUAL (&f, p->file_data)) {
                *path_ = path;
                *st_ = st;
                break;
            }
        }
        _nodedb_recursive_free (db, hardlink->parent, hardlink, &p);
        free (path);
    }

    return p ? p->file_data : NULL;
}

#if 0
// printf("devid=%llu %llu\n", f.devid, p->file_data->devid);
// printf("inode=%llu %llu\n", f.inode, p->file_data->inode);
// printf("type=%d %d\n", f.extra.type, p->file_data->extra.type);
// printf("ctime=%llu %llu\n", f.extra.ctime, p->file_data->extra.ctime);
// 
#endif


MARSHALATOMIC struct file_data *nodedb_get_first_path_from_handle (struct nodedb *db, const struct handle_data *f_handle, char **path)
{
    struct dir_entry *c;
    const struct file_data *child;

    child = (const struct file_data *) f_handle;

    *path = NULL;

    if (!(c = _nodedb_first_dir_entry_from_handle (db, child)))
        return NULL;

    *path = _nodedb_build_path (db, c);

    return c->inode_entry->file_data;
}

MARSHALATOMIC struct file_data *nodedb_lookup_by_name (struct nodedb *db, const struct handle_data *f_handle, const char *name, char **path)
{
    struct dir_entry *parent, *child;
    const struct file_data *parent_;

    parent_ = (const struct file_data *) f_handle;

    *path = NULL;

    if (!(parent = _nodedb_first_dir_entry_from_handle (db, parent_)))
        return NULL;

    if (!(child = _nodedb_dir_entry_by_name (db, parent, name)))
        return NULL;

    *path = _nodedb_build_path (db, child);

    return child->inode_entry->file_data;
}

MARSHALATOMIC struct file_data *nodedb_get_parent (struct nodedb *db, const struct handle_data *f_handle)
{
    struct dir_entry *child;
    const struct file_data *child_;

    child_ = (const struct file_data *) f_handle;

    if (!(child = _nodedb_first_dir_entry_from_handle (db, child_)))
        return NULL;

    return child->parent->inode_entry->file_data;
}

static void nodedb_delete_inode_entry (struct nodedb *db, struct inode_entry *c)
{
    struct dir_entry *hardlink, *next;
    for (hardlink = c->links.first; hardlink; hardlink = next) {
        next = hardlink->next;
        _nodedb_recursive_free (db, hardlink->parent, hardlink, &c);
    }
    assert (!c);
}

/* Returns NULL on parent not found. Note that ganesha-nfs posixdb
sql version of this function can find an inconsistency between an
old entry in the database and a new entry of the same (devid,inode).
The inconsistency would be a directory type entry versus a file type
entry. in this case the sql version would return an error. The function
below simply does a recursive delete in case of this type of inconsistency.
So the parent-not-found is the only error that is possible. */ 
MARSHALATOMIC struct file_data *nodedb_add (struct nodedb *db, const struct file_data *child, const struct handle_data *f_handle_parent, const char *name)
{
    struct inode_entry *c;
    struct dir_entry *parent_dir_entry, *child_dir_entry = NULL;
    const struct file_data *parent_;

    parent_ = (const struct file_data *) f_handle_parent;

/* 1. obtain the parent directory entry */
    if (!(parent_dir_entry = _nodedb_first_dir_entry_from_handle (db, parent_)))
        return NULL;

    if (!strcmp(name, "..")) {
        if (!parent_dir_entry->parent)
            return NULL;
        return parent_dir_entry->parent->inode_entry->file_data;
    }

    if (!strcmp(name, "."))
        return parent_dir_entry->inode_entry->file_data;

/* 2. try reuse an inode for the child */
    if ((c = _nodedb_inode_entry_by_inode (db, child))) {
        if (c->file_data->extra.type == child->extra.type) {
            c->file_data->extra = child->extra;         /* set nlinks and ctime */
        } else {        /* we can't reuse if the type has changed */
            nodedb_delete_inode_entry (db, c);
            c = NULL;
        }
    }

/* 3. if not, create an inode for the child */
    if (!c) {
        c = inode_entry_new (nodedb_new_file_data (child));
        redblack_tree_add (&db->rb_handle, c);
        redblack_tree_add (&db->rb_inode, c);
        redblack_tree_add (&db->rb_accesstime, c);
    }

#define ONE_THANG(extra)        (((extra)->nlinks == 1 && (extra)->type != DIRECTORY) || (extra)->type == DIRECTORY)
/* 4. create or reuse a name entry for the child */
    if (ONE_THANG(&child->extra) && c->links.first) {       /* subtle optimization */
        child_dir_entry = c->links.first;
        assert(child_dir_entry);
        assert(!child_dir_entry->next);
    } else if (!(child_dir_entry = find_existing_dir_entry (c, parent_dir_entry, name))) {
        child_dir_entry = dir_entry_new (c);
        inode_entry_link (&c->links, child_dir_entry);
    }

    assert (!child_dir_entry->name == !child_dir_entry->parent);

/* 5. handle a reused inode that *merely* underwent a name change: */
    if (child_dir_entry->name && strcmp (child_dir_entry->name, name) && child_dir_entry->parent == parent_dir_entry) {
        free (child_dir_entry->name);
        child_dir_entry->name = dir_entry_name_malloc (name);
    }

/* 6. handle a created inode or something wrong with the inode: */
    if (!child_dir_entry->name || strcmp (child_dir_entry->name, name) || child_dir_entry->parent != parent_dir_entry) {

/* 6.1 clean child and unlink from its parent. this will preserve grandchildren: */
        if (child_dir_entry->parent)
            remove_dirlist (child_dir_entry->parent, child_dir_entry);
        if (child_dir_entry->name)
            free (child_dir_entry->name);
        child_dir_entry->name = dir_entry_name_malloc (name);

/* 6.2 delete from the directory list of the parent any file of the same name: */
        _nodedb_dir_entry_delete (db, parent_dir_entry, name);

/* 6.3 add to the directory list of the parent: */
        insert_dirlist (parent_dir_entry, child_dir_entry);
    }

    return c->file_data;
}

#if 0
// __RSHALATOMIC struct file_data *nodedb_get_parent_child (struct nodedb *db, const struct file_data *child, struct file_data *parent_, char **path_, const char *name)
// {
//     char *p = NULL, *path = NULL;
//     struct file_data *parent = NULL, *child = NULL, f;
// 
//     parent = nodedb_get_first_path_from_handle (db, parent_, &p);
//     if (parent)
//         *parent_ = parent;
// 
//     path = dir_entry_name_cat (p, name);
//     free (p);
// 
//     memset (&f, '\0', sizeof (f));
//     if ((retval = lstat (path, &stat)) < 0)
//         return NULL;
// 
//     nodedb_stat_to_file_data (&stat, &f);
// 
//     if (!(child = MARSHAL_nodedb_add (conn, &f, parent, name)))
//         return NULL;
// 
//     return NULL;
// }
#endif


/* returns 1 on parent or child not found */
MARSHALATOMIC int nodedb_delete (struct nodedb *db, const struct handle_data *f_handle_parent, const char *name)
{
    struct inode_entry *p;
    const struct file_data *parent;

    parent = (const struct file_data *) f_handle_parent;

    p = _nodedb_inode_entry_by_handle (db, parent);
    if (!p)
        return 1;

    return _nodedb_dir_entry_delete (db, first_hardlink (p), name);
}

/* ganesha-nfs only cares that the handle is gone, not whether it was there, so return is void */
MARSHALATOMIC void nodedb_delete_by_handle (struct nodedb *db, const struct file_data *child)
{
    struct inode_entry *c;
    c = _nodedb_inode_entry_by_handle (db, child);
    if (!c)
        return;
    nodedb_delete_inode_entry (db, c);
}

/* returns 1 on stale, -errno on error and 0 on success */
MARSHALATOMIC int nodedb_unlink (struct nodedb *db, const struct handle_data *f_handle_parent, const char *name)
{
    int r;
    char *path, *t;
    struct dir_entry *p, *child;
    const struct file_data *parent;

    parent = (const struct file_data *) f_handle_parent;

    if (!(p = _nodedb_first_dir_entry_from_handle (db, parent)))
        return 1;

    if (!(child = _nodedb_dir_entry_by_name (db, p, name)))
        return 1;

    child->inode_entry->file_data->extra.nlinks--;

/* FIXME: test what happens if file changes into a dir. this change done by something local and not using NFS */
    path = dir_entry_name_cat (t = _nodedb_build_path (db, p), name);
    free (t);
    if (child->inode_entry->file_data->extra.type == DIRECTORY)
        r = rmdir (path);
    else
        r = unlink (path);
    r = r ? -errno : 0;
    if (!r)
        nodedb_delete (db, &parent->handle, name);
    free (path);
    return r;
}

/* returns 1 on stale, -errno on error and 0 on success */
MARSHALATOMIC int nodedb_rename (struct nodedb *db, const struct handle_data *f_handle_parent_old, const char *name_old, const struct handle_data *f_handle_parent_new, const char *name_new)
{
    int r = 0;
    char *path_old, *path_new;
    char *t;
    struct dir_entry *p_old, *p_new, *child;
    const struct file_data *parent_old;
    const struct file_data *parent_new;

    parent_old = (const struct file_data *) f_handle_parent_old;
    parent_new = (const struct file_data *) f_handle_parent_new;

    if (!(p_old = _nodedb_first_dir_entry_from_handle (db, parent_old)))
        return 1;

    if (!(child = _nodedb_dir_entry_by_name (db, p_old, name_old)))
        return 1;

    if (!(p_new = _nodedb_first_dir_entry_from_handle (db, parent_new)))
        return 1;

/* FIXME: check if file type changed under me */

    path_old = dir_entry_name_cat (t = _nodedb_build_path (db, p_old), name_old);
    free (t);
    path_new = dir_entry_name_cat (t = _nodedb_build_path (db, p_new), name_new);
    free (t);

    if ((r = rename (path_old, path_new))) {
        r = -errno;
    } else {
        struct file_data new_child;
        new_child = *child->inode_entry->file_data;
        new_child.extra.nlinks++;

/* we add before we delete. if we delete first and the hardlink list is
empty, then nodedb_delete will recursively delete children. not what you
want for a rename of a directory: */
        nodedb_add (db, &new_child, &parent_new->handle, name_new);
        nodedb_delete (db, &parent_old->handle, name_old);
    }
    
    free (path_old);
    free (path_new);
    return r;
}


/* returns 1 on stale, -errno on error and 0 on success */
MARSHALATOMIC int nodedb_link (struct nodedb *db, const struct handle_data *f_handle_child_old, const struct handle_data *f_handle_parent_new, const char *name_new)
{
    int r = 0;
    char *path_old, *path_new;
    char *t;
    struct dir_entry *p_new;
    const struct file_data *parent_new, *child_old, *child;

    child_old = (const struct file_data *) f_handle_child_old;
    parent_new = (const struct file_data *) f_handle_parent_new;

/* FIXME: check if file type changed under me */

    if (!(child = nodedb_get_first_path_from_handle (db, &child_old->handle, &path_old)))
        return 1;

    if (!(p_new = _nodedb_first_dir_entry_from_handle (db, parent_new)))
        return 1;

    path_new = dir_entry_name_cat (t = _nodedb_build_path (db, p_new), name_new);
    free (t);

    if ((r = link (path_old, path_new))) {
        r = -errno;
    } else {
        struct file_data new_child;
        new_child = *child;
        new_child.extra.nlinks++;
        nodedb_add (db, &new_child, &parent_new->handle, name_new);
    }

    free (path_old);
    free (path_new);
    return r;
}






#include <stdio.h>

static int find_direntry_in_inode (struct inode_entry *n, struct dir_entry *e)
{
    struct dir_entry *hardlink;
    for (hardlink = n->links.first; hardlink; hardlink = hardlink->next)
        if (hardlink == e)
            return 1;
    return 0;
}

static void _nodedb_recursive_check (struct nodedb *db, struct dir_entry *parent)
{
    struct dir_entry *child;

    assert (parent->inode_entry);
    assert (find_direntry_in_inode (parent->inode_entry, parent));

    for (child = redblack_tree_first (&parent->rb_dirlist); child; child = redblack_tree_next (&parent->rb_dirlist)) {
        _nodedb_recursive_check (db, child);
    }
}

static void _nodedb_recursive_print (struct nodedb *db, const char *path, struct dir_entry *parent)
{
    struct dir_entry *child;
    char *new_path;
    char lnk[32] = "0:0";
    char ino[32] = "0";
    char dir[32] = "";

    new_path = dir_entry_name_cat (path, parent->name);

    sprintf(lnk, "%d:%d", parent->inode_entry->file_data->extra.nlinks, parent->inode_entry->links.count);
    sprintf(ino, "%llu", parent->inode_entry->file_data->inode);
    if (parent->inode_entry->file_data->extra.type == DIRECTORY)
        sprintf(dir, "%ld", parent->rb_dirlist.count);

    printf ("%-2s %-9s %-5s %s\n", dir, ino, lnk, new_path);

    for (child = redblack_tree_first (&parent->rb_dirlist); child; child = redblack_tree_next (&parent->rb_dirlist)) {
        _nodedb_recursive_print (db, new_path, child);
    }

    free (new_path);
}

void _nodedb_print (struct nodedb *db)
{
    struct dir_entry *child;

    printf ("%-2s %-9s %-5s\n", "dl", "inode", "n:ll");
    printf ("-----------------\n/\n");
    for (child = redblack_tree_first (&db->root->rb_dirlist); child; child = redblack_tree_next (&db->root->rb_dirlist)) {
        _nodedb_recursive_print (db, "", child);
    }
}

static void *_marshal_run (void *m)
{
    marshal_run ((struct marshal *) m);
    return NULL;
}

void marshal_create_thread (void)
{
    pthread_t thread;
    pthread_attr_t attr;
    struct nodedb *db;
    static struct marshal *m = NULL;

    if (m)
        return;

    db = nodedb_new ();

    m = marshal_new (db);

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create (&thread, &attr, _marshal_run, m))
	exit (1);

    usleep(200000);
}

#ifdef UNIT_TEST

int main (int argc, char **argv)
{
    struct nodedb *db;
    char *t;
    struct connection_pool *connpool;
    struct marshal *m;

    printf("start\n");

    db = nodedb_new ();

    sleep (1);

    m = marshal_new (db);

    {
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
        if (pthread_create (&thread, &attr, _marshal_run, m))
            exit (1);
    }

    connpool = connection_pool_new ();

    global_handle = 1;

    {
        struct file_data *g, *f_usr, *f_local, *f_bin;
        struct file_data child;
        struct dir_entry *e;
        int r;

        memset(&child, '\0', sizeof(child));

        child.devid = 100;
        child.extra.nlinks = 1;
        child.extra.type = 99;
        child.extra.ctime = 1234567890;


        assert(db->rb_handle.count == 1);
        assert(db->rb_inode.count == 1);
        assert(db->rb_accesstime.count == 1);

/* 1. insert a 3-deep tree */
        child.inode = 123456;

        f_usr = MARSHAL_nodedb_add (connpool, &child, NULL, "usr");
        assert(f_usr->handle.handleid == global_handle - 1);
        assert(global_handle == 2);

        assert(db->rb_handle.count == 1 + 1);
        assert(db->rb_inode.count == 1 + 1);
        assert(db->rb_accesstime.count == 1 + 1);

        child.inode = 123457;
        f_local = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "local");
        assert(f_local->handle.handleid == global_handle - 1);
        assert(global_handle == 3);

        assert(db->rb_handle.count == 1 + 2);
        assert(db->rb_inode.count == 1 + 2);
        assert(db->rb_accesstime.count == 1 + 2);

        child.inode = 123458;
        free (MARSHAL_nodedb_add (connpool, &child, &f_local->handle, "share"));

        assert(db->rb_handle.count == 1 + 3);
        assert(db->rb_inode.count == 1 + 3);
        assert(db->rb_accesstime.count == 1 + 3);

        child.inode = 123459;
        child.extra.type = DIRECTORY;
        f_bin = MARSHAL_nodedb_add (connpool, &child, &f_local->handle, "bin");
        assert(f_bin->handle.handleid == global_handle - 1);
        assert(global_handle == 5);
        child.extra.type = 99;
        
        assert(db->rb_handle.count == 1 + 4);
        assert(db->rb_inode.count == 1 + 4);
        assert(db->rb_accesstime.count == 1 + 4);


/* 1a. check lookup by name */
        {
            struct file_data *v, w;
            char *path;
            v = MARSHAL_nodedb_lookup_by_name (connpool, &f_local->handle, "bin", &path);
            assert(!strcmp("/usr/local/bin", path));
            assert(v->inode == 123459);
            free (path);

            memset (&w, '\0', sizeof(w));
            w.handle.handleid = v->handle.handleid;
            w.handle.timestamp = v->handle.timestamp;

            free (v);

            v = MARSHAL_nodedb_get_first_path_from_handle (connpool, &w.handle, &path);
            assert(v->inode == 123459);
            assert(!strcmp("/usr/local/bin", path));
            free (path);

            free (v);
        }



        _nodedb_print (db);

        r = MARSHAL_nodedb_unlink (connpool, &f_local->handle, "bin");
        assert(r == -ENOTEMPTY);



/* 2. verify 3-deep tree */
        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "usr"));
        assert(e->inode_entry->file_data->inode == 123456);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "local"));
        assert(e->inode_entry->file_data->inode == 123457);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 2);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "bin") || !strcmp(e->name, "share"));
        assert(e->inode_entry->file_data->inode == 123458 || e->inode_entry->file_data->inode == 123459);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);




/* 3a. update extra data of a node half-way deep */
        child.inode = 123457;
        child.extra.ctime = 1234567891;

        g = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "local");
        assert(g->p == f_local->p);   /* no inode change, so no new entry */
        assert(global_handle == 5);

        assert(db->rb_handle.count == 1 + 4);
        assert(db->rb_inode.count == 1 + 4);
        assert(db->rb_accesstime.count == 1 + 4);




/* 4a. verify half-way deep update */
        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "usr"));
        assert(e->inode_entry->file_data->inode == 123456);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "local"));
        assert(e->inode_entry->file_data->inode == 123457);
        assert(e->inode_entry->file_data->extra.ctime == 1234567891);

        assert(e->rb_dirlist.count == 2);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "bin") || !strcmp(e->name, "share"));
        assert(e->inode_entry->file_data->inode == 123458 || e->inode_entry->file_data->inode == 123459);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);




        printf ("path=%s\n", t = _nodedb_build_path (db, e));
        free (t);

/* 3b. update name of a node half-way deep */
        child.inode = 123457;
        child.extra.ctime = 1234567891;
        free (g);
        g = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "local-");
        assert(g->p == f_local->p);   /* no inode change, so no new entry */
        assert(global_handle == 5);

        assert(db->rb_handle.count == 1 + 4);
        assert(db->rb_inode.count == 1 + 4);
        assert(db->rb_accesstime.count == 1 + 4);

        printf("[\n");
        _nodedb_print (db);
        printf("]\n");



/* 4b. verify half-way deep update */
        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "usr"));
        assert(e->inode_entry->file_data->inode == 123456);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "local-"));
        assert(e->inode_entry->file_data->inode == 123457);
        assert(e->inode_entry->file_data->extra.ctime == 1234567891);

        assert(e->rb_dirlist.count == 2);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "bin") || !strcmp(e->name, "share"));
        assert(e->inode_entry->file_data->inode == 123458 || e->inode_entry->file_data->inode == 123459);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);





/* 5. update half-way deep, changing the inode */
        child.inode = 130001;
        child.extra.ctime = 1234567891;
        free (g);
        g = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "local-");
        assert(g != f_local);   /* FIXME: this check needs to be redone -- under marshalling it will always be true */
        free (f_local);
        f_local = g;
        assert(f_local->handle.handleid == global_handle - 1);
        assert(global_handle == 6);

        assert(db->rb_handle.count == 1 + 2);
        assert(db->rb_inode.count == 1 + 2);
        assert(db->rb_accesstime.count == 1 + 2);



/* 6a. verify inode change deletes previous 'local' and it's children */
        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "usr"));
        assert(e->inode_entry->file_data->inode == 123456);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "local-"));
        assert(e->inode_entry->file_data->inode == 130001);
        assert(e->inode_entry->file_data->extra.ctime == 1234567891);

        assert(e->rb_dirlist.count == 0);


/* 6b. check delete_by_handle */
        {
            struct file_data *v, w;
            char *path;
            v = nodedb_lookup_by_name (db, &f_usr->handle, "local-", &path);
            free (path);

            w.handle.handleid = v->handle.handleid;
            w.handle.timestamp = v->handle.timestamp;
            nodedb_delete_by_handle(db, &w);

            v = nodedb_lookup_by_name (db, &f_usr->handle, "local-", &path);
            assert(!v);
        }

        free (f_usr);
        free (f_local);
        free (f_bin);
    }

    nodedb_make_empty (db);


    global_handle = 1;

/* 7. create hardlinks below root level */
    {
        struct file_data *f_usr, *f_local;
        struct file_data child;
        struct dir_entry *e;

        memset(&child, '\0', sizeof(child));

        child.devid = 100;
        child.extra.nlinks = 1;
        child.extra.type = 99;
        child.extra.ctime = 1234567890;



        child.inode = 123456;
        f_usr = MARSHAL_nodedb_add (connpool, &child, NULL, "usr");
        assert(f_usr->handle.handleid == global_handle - 1);
        assert(global_handle == 2);

        assert(db->rb_handle.count == 1 + 1);
        assert(db->rb_inode.count == 1 + 1);
        assert(db->rb_accesstime.count == 1 + 1);

        child.inode = 123457;
        f_local = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "TMP");
        assert(f_local->handle.handleid == global_handle - 1);
        assert(global_handle == 3);

        assert(db->rb_handle.count == 1 + 2);
        assert(db->rb_inode.count == 1 + 2);
        assert(db->rb_accesstime.count == 1 + 2);

        child.inode = 123457;
        child.extra.nlinks = 2;
        free (f_local);
        f_local = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "TMP-");
        assert(f_local->handle.handleid == global_handle - 1);
        assert(global_handle == 3);

        assert(db->rb_handle.count == 1 + 2);
        assert(db->rb_inode.count == 1 + 2);
        assert(db->rb_accesstime.count == 1 + 2);





        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 1);
        e = ((struct dir_entry *) ((char *) e->rb_dirlist.node - e->rb_dirlist.ofs));
        assert(!strcmp(e->name, "usr"));
        assert(e->inode_entry->file_data->inode == 123456);
        assert(e->inode_entry->file_data->extra.ctime == 1234567890);

        assert(e->rb_dirlist.count == 2);



        _nodedb_print (db);

        free (f_usr);
        free (f_local);

    }


    nodedb_make_empty (db);

    global_handle = 1;

/* 8. create hardlinks at root level */
    {
        struct file_data *f_usr, *f_local;
        struct file_data child;
        struct dir_entry *e;

        memset(&child, '\0', sizeof(child));

        child.devid = 100;
        child.extra.nlinks = 1;
        child.extra.type = 99;
        child.extra.ctime = 1234567890;




        child.inode = 123456;
        f_usr = MARSHAL_nodedb_add (connpool, &child, NULL, "usr");
        assert(f_usr->handle.handleid == global_handle - 1);
        assert(global_handle == 2);

        assert(db->rb_handle.count == 1 + 1);
        assert(db->rb_inode.count == 1 + 1);
        assert(db->rb_accesstime.count == 1 + 1);

        child.inode = 123456;
        child.extra.nlinks = 2;
        f_local = MARSHAL_nodedb_add (connpool, &child, NULL, "usr-");
        assert(f_local->handle.handleid == global_handle - 1);
        assert(global_handle == 2);

        assert(db->rb_handle.count == 1 + 1);
        assert(db->rb_inode.count == 1 + 1);
        assert(db->rb_accesstime.count == 1 + 1);


        e = db->root;
        _nodedb_recursive_check(db, e);

        assert(e->rb_dirlist.count == 2);



        _nodedb_print (db);

        free (f_usr);
        free (f_local);

    }


    nodedb_make_empty (db);

    global_handle = 1;

/* 8. create hardlinks at root level */
    {
        struct file_data *f_usr, *f_local, *f_TMP, *v;
        struct file_data child;
        char *path = NULL;
        int r;

        memset(&child, '\0', sizeof(child));

        child.devid = 100;
        child.extra.nlinks = 1;
        child.extra.type = 99;
        child.extra.ctime = 1234567890;


        child.inode = 123456;
        f_usr = MARSHAL_nodedb_add (connpool, &child, NULL, "usr");
        child.inode = 123457;
        f_local = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "local");
        
        unlink ("/usr/TMP");
        unlink ("/usr/local/TMP2");
        unlink ("/usr/local/TMP3");
        unlink ("/usr/local/TMP4");
        fclose(fopen("/usr/TMP", "w"));
        child.inode = 123458;
        f_TMP = MARSHAL_nodedb_add (connpool, &child, &f_usr->handle, "TMP");

        r = MARSHAL_nodedb_rename (connpool, &f_usr->handle, "TMP", &f_local->handle, "TMP2");
        assert (!r);

        v = nodedb_lookup_by_name (db, &f_local->handle, "TMP2", &path);
        assert (!strcmp (path, "/usr/local/TMP2"));
        assert (v->inode == 123458);

        nodedb_link (db, &v->handle, &f_local->handle, "TMP3");
        nodedb_link (db, &v->handle, &f_local->handle, "TMP4");
        r = unlink ("/usr/local/TMP2");
        assert (!r);
        r = unlink ("/usr/local/TMP3");
        assert (!r);
        r = unlink ("/usr/local/TMP4");
        assert (!r);

        free (path);
        free (f_local);
        free (f_usr);
        free (f_TMP);

    }

    connection_pool_free (connpool);     

    usleep (200000);

    marshal_free (m);

    nodedb_free (db);

    usleep (200000);

    return 0;
}

#endif







