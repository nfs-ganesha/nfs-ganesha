/*
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
 * ---------------------------------------
 */

/**
 * @addtogroup fsal_up
 * @{
 */

/**
 * @file fsal_up_async.c
 * @author Adam C. Emerson <aemerson@linuxbox.com>
 * @brief Asynchrony wrappers for FSAL Upcall system
 *
 * This is not the most elegant design in history, but should be
 * reasonably efficient.  At present, we have to copy the key supplied
 * rather than saving a pointer.  Once version 2.1 comes out and we
 * can from the FSAL object to the cache entry with container of,
 * we'll be able to jump up, grab a ref on the cache entry, and just
 * store the pointer.
 *
 * Every async call requires one allocation and one queue into the
 * thread fridge.  We make the thread fridge a parameter, so an FSAL
 * that's expecting to shoot out lots and lots of upcalls can make one
 * holding several threads wide.
 *
 * Every async call takes a callback function and an argument, to
 * allow it to receive errors.  The callback function may be NULL if
 * the caller doesn't care.  This doesn't affect methods that may be
 * called asynchronously by upcall handlers like @c layoutreturn.
 *
 * Every async call takes a reference on the export, the queued action
 * returns it after execution.
 *
 * Every async call returns 0 on success and a POSIX error code on error.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_lru.h"
#include "cache_inode_hash.h"
#include "fsal_up.h"
#include "sal_functions.h"
#include "pnfs_utils.h"

/* Invalidate */

struct invalidate_args {
	struct fsal_export *export;
	struct gsh_buffdesc obj;
	uint32_t flags;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char key[];
};

static void queue_invalidate(struct fridgethr_context *ctx)
{
	struct invalidate_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->invalidate(args->export, &args->obj,
					     args->flags);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_invalidate(struct fridgethr *fr, struct fsal_export *export,
			const struct gsh_buffdesc *obj, uint32_t flags,
			void (*cb) (void *, cache_inode_status_t), void *cb_arg)
{
	struct invalidate_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct invalidate_args) + obj->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->flags = flags;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, obj->addr, obj->len);
	args->obj.addr = args->key;
	args->obj.len = obj->len;

	rc = fridgethr_submit(fr, queue_invalidate, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Update */

struct update_args {
	struct fsal_export *export;
	struct gsh_buffdesc obj;
	struct attrlist attr;
	uint32_t flags;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char key[];
};

static void queue_update(struct fridgethr_context *ctx)
{
	struct update_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->update(args->export, &args->obj, &args->attr,
					 args->flags);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_update(struct fridgethr *fr, struct fsal_export *export,
		    const struct gsh_buffdesc *obj, struct attrlist *attr,
		    uint32_t flags, void (*cb) (void *, cache_inode_status_t),
		    void *cb_arg)
{
	struct update_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct update_args) + obj->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->attr = *attr;
	args->flags = flags;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, obj->addr, obj->len);
	args->obj.addr = args->key;
	args->obj.len = obj->len;

	rc = fridgethr_submit(fr, queue_update, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Lock grant */

struct lock_grant_args {
	struct fsal_export *export;
	struct gsh_buffdesc file;
	void *owner;
	fsal_lock_param_t lock_param;
	void (*cb) (void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_lock_grant(struct fridgethr_context *ctx)
{
	struct lock_grant_args *args = ctx->arg;
	state_status_t status;

	status =
	    args->export->up_ops->lock_grant(args->export, &args->file,
					     args->owner, &args->lock_param);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_lock_grant(struct fridgethr *fr, struct fsal_export *export,
			const struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t * lock_param, void (*cb) (void *,
								    state_status_t),
			void *cb_arg)
{
	struct lock_grant_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct lock_grant_args) + file->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->owner = owner;
	args->lock_param = *lock_param;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, file->addr, file->len);
	args->file.addr = args->key;
	args->file.len = file->len;

	rc = fridgethr_submit(fr, queue_lock_grant, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Lock avail */

struct lock_avail_args {
	struct fsal_export *export;
	struct gsh_buffdesc file;
	void *owner;
	fsal_lock_param_t lock_param;
	void (*cb) (void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_lock_avail(struct fridgethr_context *ctx)
{
	struct lock_avail_args *args = ctx->arg;
	state_status_t status;

	status =
	    args->export->up_ops->lock_avail(args->export, &args->file,
					     args->owner, &args->lock_param);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_lock_avail(struct fridgethr *fr, struct fsal_export *export,
			const struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t * lock_param, void (*cb) (void *,
								    state_status_t),
			void *cb_arg)
{
	struct lock_avail_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct lock_avail_args) + file->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->owner = owner;
	args->lock_param = *lock_param;
	args->cb = cb;
	args->cb_arg = cb_arg;
	memcpy(args->key, file->addr, file->len);
	args->file.addr = args->key;
	args->file.len = file->len;

	rc = fridgethr_submit(fr, queue_lock_avail, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Link */

struct link_args {
	struct fsal_export *export;
	struct gsh_buffdesc dir;
	char *name;
	struct gsh_buffdesc target;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char data[];
};

static void queue_link(struct fridgethr_context *ctx)
{
	struct link_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->link(args->export, &args->dir, args->name,
				       (args->target.addr ? &args->
					target : NULL));

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_link(struct fridgethr *fr, struct fsal_export *export,
		  const struct gsh_buffdesc *dir, const char *name,
		  const struct gsh_buffdesc *target, void (*cb) (void *,
								 cache_inode_status_t),
		  void *cb_arg)
{
	struct link_args *args = NULL;
	int rc = 0;
	size_t namelen = strlen(name) + 1;
	size_t cursor = 0;

	export->ops->get(export);

	args =
	    gsh_malloc(sizeof(struct link_args) + dir->len + namelen +
		       (target ? target->len : 0));
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, dir->addr, dir->len);
	args->dir.addr = args->data;
	args->dir.len = dir->len;
	cursor += dir->len;

	memcpy(args->data + cursor, name, namelen);
	args->name = args->data + cursor;
	cursor += namelen;

	if (target) {
		memcpy(args->data + cursor, target->addr, target->len);
		args->target.addr = args->data + cursor;
		args->target.len = target->len;
	} else {
		args->target.addr = NULL;
		args->target.len = 0;
	}

	rc = fridgethr_submit(fr, queue_link, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Unlink */

struct unlink_args {
	struct fsal_export *export;
	void (*cb) (void *, cache_inode_status_t);
	struct gsh_buffdesc dir;
	char *name;
	void *cb_arg;
	char data[];
};

static void queue_unlink(struct fridgethr_context *ctx)
{
	struct unlink_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->unlink(args->export, &args->dir, args->name);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_unlink(struct fridgethr *fr, struct fsal_export *export,
		    const struct gsh_buffdesc *dir, const char *name,
		    void (*cb) (void *, cache_inode_status_t), void *cb_arg)
{
	struct unlink_args *args = NULL;
	int rc = 0;
	size_t namelen = strlen(name) + 1;
	size_t cursor = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct unlink_args) + dir->len + namelen);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, dir->addr, dir->len);
	args->dir.addr = args->data;
	args->dir.len = dir->len;
	cursor += dir->len;

	memcpy(args->data + cursor, name, namelen);
	args->name = args->data + cursor;
	cursor += namelen;

	rc = fridgethr_submit(fr, queue_unlink, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Move from */

struct move_from_args {
	struct fsal_export *export;
	struct gsh_buffdesc dir;
	char *name;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char data[];
};

static void queue_move_from(struct fridgethr_context *ctx)
{
	struct move_from_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->move_from(args->export, &args->dir,
					    args->name);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_move_from(struct fridgethr *fr, struct fsal_export *export,
		       const struct gsh_buffdesc *dir, const char *name,
		       void (*cb) (void *, cache_inode_status_t), void *cb_arg)
{
	struct move_from_args *args = NULL;
	int rc = 0;
	size_t namelen = strlen(name) + 1;
	size_t cursor = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct move_from_args) + dir->len + namelen);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, dir->addr, dir->len);
	args->dir.addr = args->data;
	args->dir.len = dir->len;
	cursor += dir->len;

	memcpy(args->data + cursor, name, namelen);
	args->name = args->data + cursor;
	cursor += namelen;

	rc = fridgethr_submit(fr, queue_move_from, args);

 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Move to */

struct move_to_args {
	struct fsal_export *export;
	struct gsh_buffdesc dir;
	char *name;
	struct gsh_buffdesc target;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char data[];
};

static void queue_move_to(struct fridgethr_context *ctx)
{
	struct move_to_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->move_to(args->export, &args->dir, args->name,
					  (args->target.addr ? &args->
					   target : NULL));

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_move_to(struct fridgethr *fr, struct fsal_export *export,
		     const struct gsh_buffdesc *dir, const char *name,
		     const struct gsh_buffdesc *target, void (*cb) (void *,
								    cache_inode_status_t),
		     void *cb_arg)
{
	struct move_to_args *args = NULL;
	int rc = 0;
	size_t namelen = strlen(name) + 1;
	size_t cursor = 0;

	export->ops->get(export);

	args =
	    gsh_malloc(sizeof(struct move_to_args) + dir->len + namelen +
		       (target ? target->len : 0));
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, dir->addr, dir->len);
	args->dir.addr = args->data;
	args->dir.len = dir->len;
	cursor += dir->len;

	memcpy(args->data + cursor, name, namelen);
	args->name = args->data + cursor;
	cursor += namelen;

	if (target) {
		memcpy(args->data + cursor, target->addr, target->len);
		args->target.addr = args->data + cursor;
		args->target.len = target->len;
	} else {
		args->target.addr = NULL;
		args->target.len = 0;
	}

	rc = fridgethr_submit(fr, queue_move_to, args);

 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Rename */

struct rename_args {
	struct fsal_export *export;
	struct gsh_buffdesc dir;
	char *old;
	char *new;
	void (*cb) (void *, cache_inode_status_t);
	void *cb_arg;
	char data[];
};

static void queue_rename(struct fridgethr_context *ctx)
{
	struct rename_args *args = ctx->arg;
	cache_inode_status_t status;

	status =
	    args->export->up_ops->rename(args->export, &args->dir, args->old,
					 args->new);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_rename(struct fridgethr *fr, struct fsal_export *export,
		    const struct gsh_buffdesc *dir, const char *old,
		    const char *new, void (*cb) (void *, cache_inode_status_t),
		    void *cb_arg)
{
	struct rename_args *args = NULL;
	int rc = 0;
	size_t oldlen = strlen(old) + 1;
	size_t newlen = strlen(new) + 1;
	size_t cursor = 0;

	export->ops->get(export);

	args =
	    gsh_malloc(sizeof(struct rename_args) + dir->len + oldlen + newlen);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, dir->addr, dir->len);
	args->dir.addr = args->data;
	args->dir.len = dir->len;
	cursor += dir->len;

	memcpy(args->data + cursor, old, oldlen);
	args->old = args->data + cursor;
	cursor += oldlen;

	memcpy(args->data + cursor, new, newlen);
	args->new = args->data + cursor;
	cursor += newlen;

	rc = fridgethr_submit(fr, queue_rename, args);

 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Layoutrecall */

struct layoutrecall_args {
	struct fsal_export *export;
	struct gsh_buffdesc handle;
	layouttype4 layout_type;
	bool changed;
	struct pnfs_segment segment;
	void *cookie;
	struct layoutrecall_spec spec;
	void (*cb) (void *, state_status_t);
	void *cb_arg;
	char data[];
};

static void queue_layoutrecall(struct fridgethr_context *ctx)
{
	struct layoutrecall_args *args = ctx->arg;
	state_status_t status;

	status =
	    args->export->up_ops->layoutrecall(args->export, &args->handle,
					       args->layout_type, args->changed,
					       &args->segment, args->cookie,
					       (args->spec.how ==
						layoutrecall_not_specced ? NULL
						: &args->spec));

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_layoutrecall(struct fridgethr *fr, struct fsal_export *export,
			  const struct gsh_buffdesc *handle,
			  layouttype4 layout_type, bool changed,
			  const struct pnfs_segment *segment, void *cookie,
			  struct layoutrecall_spec *spec, void (*cb) (void *,
								      state_status_t),
			  void *cb_arg)
{
	struct layoutrecall_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct layoutrecall_args) + handle->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->data, handle->addr, handle->len);
	args->handle.addr = args->data;
	args->handle.len = handle->len;

	args->layout_type = layout_type;
	args->changed = changed;
	args->segment = *segment;
	args->cookie = cookie;
	if (spec) {
		args->spec = *spec;
	} else {
		args->spec.how = layoutrecall_not_specced;
	}

	rc = fridgethr_submit(fr, queue_layoutrecall, args);

 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Notify Device */

struct notify_device_args {
	struct fsal_export *export;
	notify_deviceid_type4 notify_type;
	layouttype4 layout_type;
	uint64_t devid;
	bool immediate;
	void (*cb) (void *, state_status_t);
	void *cb_arg;
	char data[];
};

static void queue_notify_device(struct fridgethr_context *ctx)
{
	struct notify_device_args *args = ctx->arg;
	state_status_t status;

	status =
	    args->export->up_ops->notify_device(args->export, args->notify_type,
						args->layout_type, args->devid,
						args->immediate);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_notify_device(struct fridgethr *fr, struct fsal_export *export,
			   notify_deviceid_type4 notify_type,
			   layouttype4 layout_type, uint64_t devid,
			   bool immediate, void (*cb) (void *, state_status_t),
			   void *cb_arg)
{
	struct notify_device_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct notify_device_args));
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;
	args->notify_type = notify_type;
	args->layout_type = layout_type;
	args->devid = devid;
	args->immediate = immediate;

	rc = fridgethr_submit(fr, queue_notify_device, args);

 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/* Delegrecall */

struct delegrecall_args {
	struct fsal_export *export;
	struct gsh_buffdesc handle;
	void (*cb) (void *, state_status_t);
	void *cb_arg;
	char key[];
};

static void queue_delegrecall(struct fridgethr_context *ctx)
{
	struct delegrecall_args *args = ctx->arg;
	state_status_t status;

	status = args->export->up_ops->delegrecall(args->export, &args->handle);

	if (args->cb) {
		args->cb(args->cb_arg, status);
	}

	args->export->ops->put(args->export);
	gsh_free(args);
}

int up_async_delegrecall(struct fridgethr *fr, struct fsal_export *export,
			 const struct gsh_buffdesc *handle, void (*cb) (void *,
									state_status_t),
			 void *cb_arg)
{
	struct delegrecall_args *args = NULL;
	int rc = 0;

	export->ops->get(export);

	args = gsh_malloc(sizeof(struct delegrecall_args) + handle->len);
	if (!args) {
		rc = ENOMEM;
		goto out;
	}

	args->export = export;
	args->cb = cb;
	args->cb_arg = cb_arg;

	memcpy(args->key, handle->addr, handle->len);
	args->handle.addr = args->key;
	args->handle.len = handle->len;

	rc = fridgethr_submit(fr, queue_delegrecall, args);
 out:

	if (rc != 0) {
		if (args) {
			gsh_free(args);
		}
		export->ops->put(export);
	}

	return rc;
}

/** @} */
