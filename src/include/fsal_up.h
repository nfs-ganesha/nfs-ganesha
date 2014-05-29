/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 *---------------------------------------
 */

/**
 * @defgroup fsal_up Upcalls for the FSAL
 * @{
 *
 * These functions allow a filesystem realization to modify the cache
 * and trigger recalls without it having to gain personal, intimate
 * knowledge of the rest of Ganesha.
 *
 * These calls are *synchronous*, meaning they immediately do whatever
 * they're going to do and return to the caller.  They are intended to
 * be called from a notification or other thread.  Specifically, this
 * means that you *must not* call layoutrecall from within layoutget.
 *
 * If you need to call one of these methods from within an FSAL
 * method, use the delayed executor interface in delayed_exec.h with a
 * delay of 0.	If you don't want to share, you could have the FSAL
 * spawn a thread fridge of its own.
 *
 * If people find themselves needing it, generally, I'll rebuild the
 * asynchronous upcall interface on top of this synchronous one.
 */

/**
 * @file fsal_up.h
 * @brief Definitions for FSAL upcalls
 */

#ifndef FSAL_UP_H
#define FSAL_UP_H

#include "fsal_types.h"
#include "fsal_api.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "sal_data.h"

/**
 * Empty flags.
 */
static const uint32_t fsal_up_update_null = 0x0000;

/**
 * Update the filesize only if the new size is greater than that
 * currently set.
 */
static const uint32_t fsal_up_update_filesize_inc = 0x0001;

/**
 * Update the atime only if the new time is later than the currently
 * set time.
 */
static const uint32_t fsal_up_update_atime_inc = 0x0002;

/**
 * Update the creation time only if the new time is later than the
 * currently set time.
 */
static const uint32_t fsal_up_update_creation_inc = 0x0004;

/**
 * Update the ctime only if the new time is later than that currently
 * set.
 */
static const uint32_t fsal_up_update_ctime_inc = 0x0008;

/**
 * Update the mtime only if the new time is later than that currently
 * set.
 */
static const uint32_t fsal_up_update_mtime_inc = 0x0010;

/**
 * Update the chgtime only if the new time is later than that
 * currently set.
 */
static const uint32_t fsal_up_update_chgtime_inc = 0x0020;

/**
 * Update the spaceused only if the new size is greater than that
 * currently set.
 */
static const uint32_t fsal_up_update_spaceused_inc = 0x0040;

/**
 * The file link count is zero.
 */
static const uint32_t fsal_up_nlink = 0x0080;

/**
 * @brief Optional stuff for layoutreturn
 * @{
 */
enum layoutrecall_howspec {
	layoutrecall_howspec_exactly,
	layoutrecall_howspec_complement,
	layoutrecall_not_specced
};

struct layoutrecall_spec {
	enum layoutrecall_howspec how;
	union {
		clientid4 client;
	} u;
};

/** @} */

/**
 * @brief Possible upcall functions
 *
 * This structure holds pointers to upcall functions.  Each FSAL
 * should call through the vector in its export.
 *
 * For FSAL stacking, the 'higher' FSAL should copy its vector,
 * over-ride whatever methods it wishes, and pass the new vector to
 * the lower FSAL.  It may then pass through, surround, or override as
 * it wishes.
 *
 * Note that all these functions take keys, not fsal object handles.
 * This is because the FSAL will always, by definition, be able to
 * know the key by which it identifies an object, but cannot know the
 * address of the handle stored in the cache.
 */

struct fsal_up_vector {
	/** Invalidate some or all of a cache entry */
	cache_inode_status_t(*invalidate)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *obj,	/*< The file to invalidate */
		uint32_t flags /*< Flags governing invalidation */
		);

	/** Update cached attributes */
	cache_inode_status_t(*update)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *obj,	/*< The file to update */
		struct attrlist *attr, /*< List of attributes to
					   update.  Note that the @c
					   type, @c fsid, @c fileid,
					   @c rawdev, and @c generation
					   fields must not be updated
					   and the corresponding bits in the
					   mask must not be set, nor may the
					   ATTR_RDATA_ERR bit be set. */
		uint32_t flags	/*< Flags requesting special update handling */
		);

	/** Grant a lock to a client */
	state_status_t(*lock_grant)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *file, /*< The file in question */
		void *owner, /*< The lock owner */
		fsal_lock_param_t *lock_param /*< A description of the lock */
		);

	/** Signal lock availability */
	state_status_t(*lock_avail)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *file, /*< The file in question */
		void *owner, /*< The lock owner */
		fsal_lock_param_t *lock_param /*< A description of the lock */
		);

	/** Perform a layoutrecall on a single file */
	state_status_t(*layoutrecall)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *handle, /*< Handle on which the
						       layout is held */
		layouttype4 layout_type, /*< The type of layout to recall */
		bool changed, /*< Whether the layout has changed and the
				  client ought to finish writes through MDS */
		const struct pnfs_segment *segment, /*< Segment to recall */
		void *cookie, /*< A cookie returned with the return that
				  completely satisfies a recall */
		struct layoutrecall_spec *spec	/*< Lets us be fussy about what
						    clients we send to. May be
						    NULL. */
		);

	/** Remove or change a deviceid */
	state_status_t(*notify_device)(
		notify_deviceid_type4 notify_type, /*< Change or remove */
		layouttype4 layout_type, /*< The layout type affected */
		struct pnfs_deviceid devid, /*< The deviceid */
		bool immediate /*< Whether the change is immediate
				   (in the case of a change.) */
		);

	/** Recall a delegation */
	state_status_t(*delegrecall)(
		struct fsal_module *fsal,
		struct gsh_buffdesc *handle /*< Handle on which the
						    delegation is held */
		);

	/** Invalidate some or all of a cache entry and close if open */
	cache_inode_status_t(*invalidate_close)(
		struct fsal_module *fsal,
		const struct fsal_up_vector *up_ops,
		struct gsh_buffdesc *obj,	/*< The file to invalidate */
		uint32_t flags /*< Flags governing invalidation */
		);


};

extern struct fsal_up_vector fsal_up_top;

/**
 * @{
 * @brief Asynchronous upcall wrappers
 */

int up_async_invalidate(struct fridgethr *fr,
			const struct fsal_up_vector *up_ops,
			struct fsal_module *fsal,
			struct gsh_buffdesc *obj, uint32_t flags,
			void (*cb) (void *, cache_inode_status_t),
			void *cb_arg);
int up_async_update(struct fridgethr *fr,
		    const struct fsal_up_vector *up_ops,
		    struct fsal_module *fsal,
		    struct gsh_buffdesc *obj, struct attrlist *attr,
		    uint32_t flags, void (*cb) (void *, cache_inode_status_t),
		    void *cb_arg);
int up_async_lock_grant(struct fridgethr *fr,
			const struct fsal_up_vector *up_ops,
			struct fsal_module *fsal,
			struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t *lock_param,
			void (*cb)(void *, state_status_t),
			void *cb_arg);
int up_async_lock_avail(struct fridgethr *fr,
			const struct fsal_up_vector *up_ops,
			struct fsal_module *fsal,
			struct gsh_buffdesc *file, void *owner,
			fsal_lock_param_t *lock_param,
			void (*cb) (void *, state_status_t),
			void *cb_arg);
int up_async_layoutrecall(struct fridgethr *fr,
			  const struct fsal_up_vector *up_ops,
			  struct fsal_module *fsal,
			  struct gsh_buffdesc *handle,
			  layouttype4 layout_type, bool changed,
			  const struct pnfs_segment *segment, void *cookie,
			  struct layoutrecall_spec *spec,
			  void (*cb) (void *, state_status_t),
			  void *cb_arg);
int up_async_notify_device(struct fridgethr *fr,
			   const struct fsal_up_vector *up_ops,
			   notify_deviceid_type4 notify_type,
			   layouttype4 layout_type,
			   struct pnfs_deviceid *devid,
			   bool immediate, void (*cb) (void *, state_status_t),
			   void *cb_arg);
int up_async_delegrecall(struct fridgethr *fr,
			 const struct fsal_up_vector *up_ops,
			 struct fsal_module *fsal,
			 struct gsh_buffdesc *handle,
			 void (*cb)(void *, state_status_t),
			 void *cb_arg);

/** @} */

cache_inode_status_t fsal_invalidate(struct fsal_module *fsal,
				     struct gsh_buffdesc *handle,
				     uint32_t flags);

cache_inode_status_t up_get(struct fsal_module *fsal,
			    struct gsh_buffdesc *handle,
			    cache_entry_t **entry);

#endif /* FSAL_UP_H */
/** @} */
