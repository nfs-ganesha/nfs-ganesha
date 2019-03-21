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

#include "gsh_status.h"
#include "fsal_api.h"
#include "sal_data.h"

enum {
	/* empty flags */
	fsal_up_update_null = 0x0000,

	/* Update the filesize only if the new size is greater
	 * than that currently set */
	fsal_up_update_filesize_inc = 0x0001,

	/* Update the atime only if the new time is later than
	 * the currently set time. */
	fsal_up_update_atime_inc = 0x0002,

	/* Update the creation time only if the new time is
	 * later than the currently set time. */
	fsal_up_update_creation_inc = 0x0004,

	/* Update the ctime only if the new time is later
	 * than that currently * set */
	fsal_up_update_ctime_inc = 0x0008,

	/* Update the mtime only if the new time is later
	 * than that currently set. */
	fsal_up_update_mtime_inc = 0x0010,

	/* Update the spaceused only if the new size is greater
	 * than that currently set. */
	fsal_up_update_spaceused_inc = 0x0040,

	/* The file link count is zero. */
	fsal_up_nlink = 0x0080,
};

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

static const uint32_t FSAL_UP_INVALIDATE_ATTRS = 0x01;
static const uint32_t FSAL_UP_INVALIDATE_ACL = 0x02;
static const uint32_t FSAL_UP_INVALIDATE_CONTENT = 0x04;
static const uint32_t FSAL_UP_INVALIDATE_DIR_POPULATED = 0x08;
static const uint32_t FSAL_UP_INVALIDATE_DIR_CHUNKS = 0x10;
static const uint32_t FSAL_UP_INVALIDATE_CLOSE = 0x100;
static const uint32_t FSAL_UP_INVALIDATE_FS_LOCATIONS = 0x200;
static const uint32_t FSAL_UP_INVALIDATE_SEC_LABEL = 0x400;
static const uint32_t FSAL_UP_INVALIDATE_PARENT = 0x800;
#define FSAL_UP_INVALIDATE_CACHE ( \
	FSAL_UP_INVALIDATE_ATTRS | \
	FSAL_UP_INVALIDATE_ACL | \
	FSAL_UP_INVALIDATE_CONTENT | \
	FSAL_UP_INVALIDATE_DIR_POPULATED | \
	FSAL_UP_INVALIDATE_DIR_CHUNKS | \
	FSAL_UP_INVALIDATE_FS_LOCATIONS | \
	FSAL_UP_INVALIDATE_SEC_LABEL | \
	FSAL_UP_INVALIDATE_PARENT)

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
	/** The gsh_export this vector lives in */
	struct gsh_export *up_gsh_export;
	/** The fsal_export this vector lives in */
	struct fsal_export *up_fsal_export;

	/** ready to take upcalls condition */
	bool up_ready;
	bool up_cancel;
	pthread_mutex_t up_mutex;
	pthread_cond_t up_cond;

	/** Invalidate some or all of a cache entry
	 *
	 * @param[in] vec	Up ops vector
	 * @param[in] obj	The file to invalidate
	 * @param[in] flags	FSAL_UP_INVALIDATE_*
	 *
	 * @return FSAL status
	 *
	 */
	fsal_status_t (*invalidate)(const struct fsal_up_vector *vec,
				    struct gsh_buffdesc *obj,
				    uint32_t flags);

	/** Update cached attributes
	 *
	 * @param[in] vec    Up ops vector
	 * @param[in] obj    The file to update
	 * @param[in] attr   List of attributes to update.  Note that the
	 *                   @c type, @c fsid, @c fileid, @c rawdev, and
	 *                   @c generation fields must not be updated and
	 *                   the corresponding bits in the mask must not
	 *                   be set, nor may the ATTR_RDATA_ERR bit be set.
	 * @param[in] flags  Flags requesting special update handling
	 *
	 */
	fsal_status_t (*update)(const struct fsal_up_vector *vec,
				struct gsh_buffdesc *obj,
				struct attrlist *attr,
				uint32_t flags);

	/** Grant a lock to a client
	 *
	 * @param[in] vec	   Up ops vector
	 * @param[in] file         The file in question
	 * @param[in] owner        The lock owner
	 * @param[in] lock_param   A description of the lock
	 *
	 */
	state_status_t (*lock_grant)(const struct fsal_up_vector *vec,
				     struct gsh_buffdesc *file,
				     void *owner,
				     fsal_lock_param_t *lock_param);

	/** Signal lock availability
	 *
	 * @param[in] vec	   Up ops vector
	 * @param[in] file         The file in question
	 * @param[in] owner        The lock owner
	 * @param[in] lock_param   A description of the lock
	 *
	 */
	state_status_t (*lock_avail)(const struct fsal_up_vector *vec,
				     struct gsh_buffdesc *file,
				     void *owner,
				     fsal_lock_param_t *lock_param);

	/** Perform a layoutrecall on a single file
	 *
	 * @param[in] vec	   Up ops vector
	 * @param[in] handle       Handle on which the layout is held
	 * @param[in] layout_type  The type of layout to recall
	 * @param[in] changed      Whether the layout has changed and the
	 *                         client ought to finish writes through MDS
	 * @param[in] segment      Segment to recall
	 * @param[in] cookie       A cookie returned with the return that
	 *                         completely satisfies a recall
	 * @param[in] spec         Lets us be fussy about what clients we send
	 *                         to. May be NULL.
	 *
	 */
	state_status_t (*layoutrecall)(const struct fsal_up_vector *vec,
				       struct gsh_buffdesc *handle,
				       layouttype4 layout_type,
				       bool changed,
				       const struct pnfs_segment *segment,
				       void *cookie,
				       struct layoutrecall_spec *spec);

	/** Remove or change a deviceid
	 *
	 * @param[in] notify_type  Change or remove
	 * @param[in] layout_type  The layout type affected
	 * @param[in] devid        The deviceid
	 * @param[in] immediate    Whether the change is immediate
	 *
	 */
	state_status_t (*notify_device)(notify_deviceid_type4 notify_type,
					layouttype4 layout_type,
					struct pnfs_deviceid devid,
					bool immediate);

	/** Recall a delegation
	 *
	 * @param[in] vec	Up ops vector
	 * @param[in] handle	Handle on which the delegation is held
	 */
	state_status_t (*delegrecall)(const struct fsal_up_vector *vec,
				      struct gsh_buffdesc *handle);

	/** Invalidate some or all of a cache entry and close if open
	 *
	 * This version should NOT be used if an FSAL supports extended
	 * operations, instead, the FSAL may directly close the file as
	 * necessary.
	 *
	 * @param[in] vec	Up ops vector
	 * @param[in] obj	The file to invalidate
	 * @param[in] flags	Flags governing invalidation
	 *
	 * @return FSAL status
	 *
	 */
	fsal_status_t (*invalidate_close)(const struct fsal_up_vector *vec,
					  struct gsh_buffdesc *obj,
					  uint32_t flags);
};

extern struct fsal_up_vector fsal_up_top;

/**
 * @{
 * @brief Asynchronous upcall wrappers
 */

fsal_status_t up_async_invalidate(struct fridgethr *fr,
				  const struct fsal_up_vector *vec,
				  struct gsh_buffdesc *obj, uint32_t flags,
				  void (*cb)(void *, fsal_status_t),
				  void *cb_arg);
fsal_status_t up_async_update(struct fridgethr *fr,
			      const struct fsal_up_vector *vec,
			      struct gsh_buffdesc *obj, struct attrlist *attr,
			      uint32_t flags,
			      void (*cb)(void *, fsal_status_t),
			      void *cb_arg);
fsal_status_t up_async_lock_grant(struct fridgethr *fr,
				  const struct fsal_up_vector *vec,
				  struct gsh_buffdesc *file, void *owner,
				  fsal_lock_param_t *lock_param,
				  void (*cb)(void *, state_status_t),
				  void *cb_arg);
fsal_status_t up_async_lock_avail(struct fridgethr *fr,
				  const struct fsal_up_vector *vec,
				  struct gsh_buffdesc *file, void *owner,
				  fsal_lock_param_t *lock_param,
				  void (*cb)(void *, state_status_t),
				  void *cb_arg);
fsal_status_t up_async_layoutrecall(struct fridgethr *fr,
				    const struct fsal_up_vector *vec,
				    struct gsh_buffdesc *handle,
				    layouttype4 layout_type, bool changed,
				    const struct pnfs_segment *segment,
				    void *cookie,
				    struct layoutrecall_spec *spec,
				    void (*cb)(void *, state_status_t),
				    void *cb_arg);
fsal_status_t up_async_notify_device(struct fridgethr *fr,
				     const struct fsal_up_vector *vec,
				     notify_deviceid_type4 notify_type,
				     layouttype4 layout_type,
				     struct pnfs_deviceid *devid,
				     bool immediate,
				     void (*cb)(void *, state_status_t),
				     void *cb_arg);
fsal_status_t up_async_delegrecall(struct fridgethr *fr,
				   const struct fsal_up_vector *vec,
				   struct gsh_buffdesc *handle,
				   void (*cb)(void *, state_status_t),
				   void *cb_arg);

/** @} */
int async_delegrecall(struct fridgethr *fr, struct fsal_obj_handle *obj);

int async_cbgetattr(struct fridgethr *fr, struct fsal_obj_handle *obj,
		    nfs_client_id_t *client);

void up_ready_init(struct fsal_up_vector *up_ops);
void up_ready_set(struct fsal_up_vector *up_ops);
void up_ready_wait(struct fsal_up_vector *up_ops);
void up_ready_cancel(struct fsal_up_vector *up_ops);

#endif /* FSAL_UP_H */
/** @} */
