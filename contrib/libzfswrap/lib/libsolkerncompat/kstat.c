/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Adaptation to zfs-fuse :
 * kstats now appear in a fuse virtual filesystem, mounted in /zfs-kstats for
 * now.
 * Notice that multiple kstats with the same name (which appear as arrays in
 * solaris) are not handled here, in this case only the 1st name appears, the
 * others are discarded. I didn't find any place where it could be useful
 * anyway (taskq kstats are not updated in zfs-fuse !) */

#include <sys/kstat.h>
#include <string.h>
#include <unistd.h>

#if 0 //libzfswrap
#define FUSE_USE_VERSION 26

#include <fuse/fuse_lowlevel.h>
#include "format.h"
#endif //libzfswrap
#include <errno.h>
#include <syslog.h>
#include <sys/mount.h>

#if 0 // libzfswrap
int no_kstat_mount; // used in main.c as argument for zfs-fuse
struct dir_s;
typedef struct dir_s dir_t;

struct dir_s {
    const char *name;
    fuse_ino_t inode;
    int alloc_dirs, nb_dirs;
    dir_t **dirs,*parent;
    int nb_files;
    kstat_named_t **files;
};

static int used_files;
static fuse_ino_t next_inode;

static dir_t *root;
static int mounted;

static dir_t *add_dir(dir_t *root, const char *name) {
    for (int n=0; n<root->nb_dirs; n++) {
	if (!strcmp(root->dirs[n]->name, name))
	    return root->dirs[n];
    }

    if (root->alloc_dirs == root->nb_dirs) {
	root->alloc_dirs += 10;
	root->dirs = realloc(root->dirs,root->alloc_dirs*sizeof(dir_t));
    }
    dir_t *dir = (dir_t*)calloc(1,sizeof(dir_t));
    dir->name = name;
    dir->inode = next_inode++;
    dir->parent = root;
    root->dirs[root->nb_dirs++] = dir;
    return dir;
}

// find the dir with this inode, or the one containing the file with this inode
static dir_t* find_dir(dir_t *dir, fuse_ino_t inode) {
    if (dir->inode == inode || (dir->inode < inode &&
	       	dir->inode+dir->nb_files >= inode))
	return dir;
    int n;
    for (n=0; n<dir->nb_dirs; n++) {
	dir_t *sub = find_dir(dir->dirs[n],inode);
	if (sub) return sub;
    }
    return NULL;
}

#endif // libzfswrap
/*ARGSUSED*/
kstat_t *kstat_create(const char *module, int instance, const char *name, const char *class,
    uchar_t type, uint_t ndata, uchar_t ks_flag)
{
#if 0 // libzfswrap
    if (no_kstat_mount) return NULL;
    if (!root) {
	root = (dir_t*)calloc(1,sizeof(dir_t));
	if (!root) return NULL;
	root->name = "/";
	root->inode = 1;
	next_inode = 2;
    }

    dir_t *dir = add_dir(root,module);
    // class is *always* "misc" in zfs, so we can probably get rid of it !
    // dir = add_dir(dir,class);
    dir = add_dir(dir,name);

    if (dir->nb_files) {
	/* It's not a bug, apparently these kstats create arrays of values
	 * when passed the same name - I'll just ignore this for now, this
	 * is mainly used for taskq, but they don't seem to be updated in
	 * zfs-fuse */
	// printf("kstat_create: already know this dir, discarded\n");
	return NULL;
    }

    kstat_t *kstat = (kstat_t*)calloc(1,sizeof(kstat_t));
    if (kstat) {
	kstat->ks_crtime = gethrtime(); // usefull ?
	kstat->ks_private = dir;
	kstat->ks_kid = dir->inode;
	dir->nb_files = ndata;
	next_inode += ndata; // reserve inodes for future files of this dir
	used_files += ndata;
	dir->files = (kstat_named_t**)calloc(ndata,sizeof(kstat_named_t*));
    }

    return (kstat);
#endif // libzfswrap
    return NULL;
}

#if 0 // libzfswrap
static void mount_kstat();
#endif // libzfswrap

/*ARGSUSED*/
void
kstat_install(kstat_t *ksp)
{
#if 0 // libzfswrap
    kstat_named_t *names = ksp->ks_data;
    if (!names) {
	printf("kstat_install: bad data\n");
	return;
    }
    // taskq.c overwrites ks_private so we must find our dir from ks_kid !!!
    dir_t *dir = find_dir(root,ksp->ks_kid);
    if (!dir) {
	printf("kstat_install: could not find dir !\n");
	exit(1);
    }
    if (dir->inode != ksp->ks_kid) {
	printf("search crazyness\n");
	exit(1);
    }
    int n;
    for (n=0; n<dir->nb_files; n++) {
	dir->files[n] = names;
	names++;
    }
    if (!mounted) {
	mount_kstat();
    }
#endif // libzfswrap
}

#if 0 // libzfswrap
static void umount_kstat();
#endif // libzfswrap

/*ARGSUSED*/
void
kstat_delete(kstat_t *ksp)
{
#if 0 // libzfswrap
    dir_t *dir = find_dir(root,ksp->ks_kid);
    if (!dir) {
	printf("kstat_delete: didn't find dir\n");
	return;
    }
    used_files -= dir->nb_files;
    dir_t *parent = dir->parent;
    free(dir->files);
    if (dir->inode + dir->nb_files + 1 == next_inode) {
	/* Try loosely to recover deleted inodes, it's not supposed to be
	 * very efficient ! */
	next_inode -= dir->nb_files + 1;
    }
    dir->files = NULL;
    int n;
    for (n=0; n<parent->nb_dirs; n++) {
	if (parent->dirs[n] == dir) {
	    if (n < parent->nb_dirs-1) {
		memmove(&parent->dirs[n],&parent->dirs[n+1],
			(parent->nb_dirs-n-1)*sizeof(dir_t*));
	    }
	    parent->nb_dirs--;
	    free(dir);
	    break;
	}
    }
    if (!used_files)
	umount_kstat();
#endif // libzfswrap
}

/* fuse part, heavily inspired from hello_ll.c */
#if 0 //libzfswrap
static char kstat_str[80];

static void get_value(dir_t *dir, fuse_ino_t ino) {
    kstat_named_t *file = dir->files[ino-1-dir->inode];
    switch (file->data_type) {
    case KSTAT_DATA_INT32:
    case KSTAT_DATA_UINT32:
	sprintf(kstat_str,"%d\n",file->value.i32);
	break;
    case KSTAT_DATA_INT64:
	sprintf(kstat_str,FI64 "\n",file->value.i64);
	break;
    case KSTAT_DATA_UINT64:
	sprintf(kstat_str,FU64 "\n",file->value.ui64);
	break;
    default:
	sprintf(kstat_str,"data type %d not handled\n",file->data_type);
    }
}

static int kstat_stat(fuse_ino_t ino, struct stat *stbuf)
{
    dir_t *dir = find_dir(root,ino);
    if (!dir) {
	printf("kstat_stat: could not find inode %ld\n",ino);
	exit(1);
    }
    stbuf->st_ino = ino;
    if (dir->inode == ino) { // Exact match -> this is a dir
	stbuf->st_mode = S_IFDIR | 0755;
	stbuf->st_nlink = 2;
    } else {
	stbuf->st_mode = S_IFREG | 0444;
	stbuf->st_nlink = 1;
	get_value(dir,ino);
	stbuf->st_size = strlen(kstat_str);
    }

    return 0;
}

static void kstat_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (kstat_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void kstat_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    dir_t *dir = find_dir(root,parent);
    if (!dir) {
	printf("kstat_ll_lookup: could not find parent %ld for name %s\n",parent,name);
	fuse_reply_err(req, ENOENT);
	return;
    }
    fuse_ino_t ino = 0;
    int n;
    for (n=0; n<dir->nb_dirs; n++) {
	if (!strcmp(dir->dirs[n]->name,name)) {
	    ino = dir->dirs[n]->inode;
	    break;
	}
    }
    if (!ino) {
	for (n=0; n<dir->nb_files; n++) {
	    if (!strcmp(dir->files[n]->name,name)) {
		ino = dir->inode+n+1;
		break;
	    }
	}
    }
    if (!ino) {
	printf("kstat_ll_lookup: could not find inode for name %s\n",name);
	fuse_reply_err(req, ENOENT);
	return;
    }

    struct fuse_entry_param e;

    memset(&e, 0, sizeof(e));
    e.ino = ino;
    e.attr_timeout = 0.0;
    e.entry_timeout = 0.0;
    kstat_stat(e.ino, &e.attr);

    fuse_reply_entry(req, &e);
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void kstat_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
    (void) fi;
    dir_t *dir = find_dir(root,ino);
    if (!dir) {
	printf("kstat_ll_readdir: could not find dir with inode %ld\n",ino);
	fuse_reply_err(req, ENOTDIR);
	return;
    }

    struct dirbuf b;

    memset(&b, 0, sizeof(b));
    dirbuf_add(req, &b, ".", dir->inode);
    dirbuf_add(req, &b, "..", (dir->parent ? dir->parent->inode : 1));
    int n;
    for (n=0; n<dir->nb_dirs; n++)
	dirbuf_add(req, &b, dir->dirs[n]->name, dir->dirs[n]->inode);
    for (n=0; n<dir->nb_files; n++)
	dirbuf_add(req, &b, dir->files[n]->name, dir->inode+1+n);
    reply_buf_limited(req, b.p, b.size, off, size);
    free(b.p);
}

static void kstat_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void kstat_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void) fi;

	dir_t *dir = find_dir(root,ino);
	get_value(dir,ino);
	reply_buf_limited(req, kstat_str, strlen(kstat_str), off, size);
}

static struct fuse_lowlevel_ops kstat_ll_oper = {
	.lookup		= kstat_ll_lookup,
	.getattr	= kstat_ll_getattr,
	.readdir	= kstat_ll_readdir,
	.open		= kstat_ll_open,
	.read		= kstat_ll_read,
};

// zfs-fuse directory is not included from here, it's faster to copy the
// prototype here then
extern int zfsfuse_newfs(char *mntpoint, struct fuse_chan *ch);
extern int newfs_fd[2];

static struct fuse_chan *ch;
#endif // libzfswrap

static void mount_kstat() {
#if 0 // libzfswrap
    if (!newfs_fd[1]) {
	// printf("zfs-fuse not ready to mount, delaying...\n");
	return;
    }
    char mntdir[PATH_MAX];
    sprintf(mntdir,"/zfs-kstat");
    mkdir(mntdir,0755);

    struct fuse_args args = FUSE_ARGS_INIT(0, NULL);
    int err = -1;
    char my_arg[512];

    sprintf(my_arg,"fsname=kstat,nonempty,allow_other");
    if(fuse_opt_add_arg(&args, "") == -1 ||
	    fuse_opt_add_arg(&args, "-o") == -1 ||
	    fuse_opt_add_arg(&args, my_arg) == -1) {
	fuse_opt_free_args(&args);
	syslog(LOG_WARNING,"kstat: problem in passing arguments to fuse!");
	return;
    }

    int tries = 0;
    do {
	tries++;
	ch = fuse_mount(mntdir, &args);
	if (ch != NULL) {
	    struct fuse_session *se;

	    se = fuse_lowlevel_new(&args, &kstat_ll_oper,
		    sizeof(kstat_ll_oper), NULL);
	    if (se != NULL) {
		fuse_session_add_chan(se, ch);
		if(zfsfuse_newfs(mntdir, ch) != 0) {
		    fuse_session_destroy(se);
		    fuse_unmount(mntdir,ch);
		    fuse_opt_free_args(&args);
		    return;
		}
		mounted = 1;
	    } else
		syslog(LOG_WARNING,"kstat: session creation error");
	} else {
	    syslog(LOG_WARNING,"kstat: fuse_mount error - trying to umount");
	    umount(mntdir);
	}
    } while (ch == NULL && tries < 3);
    fuse_opt_free_args(&args);

#endif // libzfswrap
    return;
}

#if 0 // libzfswrap
static void umount_kstat() {

    if (mounted) {
	/* system_taskq is destroyed after all the vfs have been unmounted
	 * so calling fuse_unmount here becomes useless */
	// fuse_unmount("/zfs-kstat",ch);
	rmdir("/zfs-kstat");
	mounted = 0;
    }
}
#endif // libzfswrap

