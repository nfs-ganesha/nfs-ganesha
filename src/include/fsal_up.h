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
 *---------------------------------------
 */

/**
 * @file    fsal_up.h
 */

#ifndef _FSAL_UP_H
#define _FSAL_UP_H

#include "fsal_types.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "fridgethr.h"

struct fsal_up_vector;

/**
 * @brief An enumeration of supported events
 *
 * @note This must not become discontiguous or terrible things will
 * happen.  If you add new events, update the check in @c
 * fsal_up_submit and the array bound in the @c fsal_up_vector
 * structure.
 */

typedef enum {
	FSAL_UP_EVENT_LOCK_GRANT,
	FSAL_UP_EVENT_LOCK_AVAIL,
	FSAL_UP_EVENT_INVALIDATE,
	FSAL_UP_EVENT_UPDATE,
	FSAL_UP_EVENT_LINK,
	FSAL_UP_EVENT_UNLINK,
	FSAL_UP_EVENT_MOVE_FROM,
	FSAL_UP_EVENT_MOVE_TO,
	FSAL_UP_EVENT_RENAME,
	FSAL_UP_EVENT_LAYOUTRECALL,
	FSAL_UP_EVENT_RECALL_ANY,
	FSAL_UP_EVENT_NOTIFY_DEVICE,
	FSAL_UP_EVENT_DELEGATION_RECALL
} fsal_up_event_type_t;

/**
 * @brief A structure letting the FSAL identify a file
 */

struct fsal_up_file
{
	struct gsh_buffdesc key; /*< Hash key identifying the file.
                                     This buffer must be allocated
                                     with gsh_malloc and will be
                                     freed after the event is
                                     processed.  Maybe {NULL, 0}. */
	struct fsal_export *export; /*< The FSAL export object.  A
                                        reference will be taken on the
                                        export when the event is
                                        queued and released when it is
                                        disposed. */
};

/**
 * @brief Structure identifying a lock grant
 */

struct fsal_up_event_lock_grant
{
	void *lock_owner; /*< The lock owner */
	fsal_lock_param_t lock_param; /*< A description of the lock */
};

/**
 * @brief Structure identifying a newly available lock
 */

struct fsal_up_event_lock_avail
{
	void *lock_owner; /*< The lock owner */
	fsal_lock_param_t lock_param; /*< A description of the lock */
};

/**
 * @brief A structure for cache invalidation
 */

struct fsal_up_event_invalidate
{
	uint32_t flags; /*< Flags governing invalidate. */
};

/**
 * @brief A structure for updating the attributes of a file
 */

struct fsal_up_event_update
{
	struct attrlist attr; /*< List of attributes to update.  Note
                                  that the @c supported_attributes, @c
                                  type, @c fsid, @c fileid, @c rawdev,
                                  @c mounted_on_fileid, and @c
                                  generation fields must not be
                                  updated and the corresponding bits
                                  in the mask must not be set, nor may
                                  the ATTR_RDATA_ERR bit be set. */
	uint32_t flags; /*< Flags indicating special handling for
                            some attributes. */
};

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
 * @brief A structure for representing the creation of a new name in a
 * directory.
 */

struct fsal_up_event_link
{
	char *name; /*< The name of the newly created link.  This is
                        allocated by the FSAL and freed by the FSAL
                        upcall thread. */
	struct fsal_up_file target; /*< The target of the link, may
                                        be {{0, 0}, 0} if unknown. */
};

/**
 * @brief A structure for representing the deletion of a name from a
 * directory.
 */

struct fsal_up_event_unlink
{
	char *name; /*< The name of the newly created link.  This is
                        allocated by the FSAL and freed by the FSAL
                        upcall thread. */
};

/**
 * @brief A structure for representing a move of a name out of a
 * directory. (This does not decrement the cached link count.)
 */

struct fsal_up_event_move_from
{
	char *name; /*< The name of the newly created link.  This is
                        allocated by the FSAL and freed by the FSAL
                        upcall thread. */
};

/**
 * @brief A structure for representing the movement of a name into a
 * directory (this does not change the cached link count.)
 */

struct fsal_up_event_move_to
{
	char *name; /*< The name of the newly created link.  This is
                        allocated by the FSAL and freed by the FSAL
                        upcall thread. */
	struct fsal_up_file target; /*< The target of the link, may
                                        be {{0, 0}, 0} if unknown. */
};

/**
 * @brief A structure for representing a rename of a file within a
 * directory
 */

struct fsal_up_event_rename
{
	char *old; /*< The original name.  This is allocated by the
                       FSAL and freed by the upcall thread. */
	char *new; /*< The new name.  This is allocated by the FSAL
                       and freed by the upcall thread. */
};

/**
 * A structure for recalling a layout
 */

struct fsal_up_event_layoutrecall
{
	layouttype4 layout_type; /*< The type of layout to recall */
	layoutrecall_type4 recall_type; /*< Type of recall.
                                            LAYOUTRECALL4_ALL is
                                            disallowed, if you wish to
                                            recall all layouts, use
                                            LAYOUTRECALL4_FSID from
                                            each export. */
	bool changed; /*< Whether the layout has changed and the
                          client ought to finish writes through MDS. */
	struct pnfs_segment segment; /*< Segment to recall */
	void *cookie; /*< A cookie returned with the return that
                          completely satisfies a recall. */
};

/**
 * A structure for recalling any layout
 */

struct fsal_up_event_recallany
{
	uint32_t objects_to_keep;
	struct bitmap4 type_mask;
	void *cookie; /*< A cookie returned with the return that
                          completely satisfies a recall. */
};

/**
 * A structure for notify device
 */

struct fsal_up_event_notifydevice
{
	notify_deviceid_type4 notify_type;
	layouttype4 layout_type;
	deviceid4 device_id;
	bool immediate;
	void *cookie; /*< A cookie returned with the return that
                          completely satisfies a recall. */
};

/**
 * A structure for delegation recall
 */

struct fsal_up_event_delegrecall
{
	int flags; // recall all ???
};

/**
 * @brief A single up-call event.
 */

struct fsal_up_event
{
	struct glist_head event_link; /*< Link in the event queue */
	const struct fsal_up_vector *functions; /*< Vector of upcall
                                                    functions.  Should
                                                    be filled in by
                                                    the FSAL with the
                                                    vector
                                                    supplied. */
	fsal_up_event_type_t type; /*< The type of event reported */
	union {
		struct fsal_up_event_lock_grant lock_grant;
		struct fsal_up_event_lock_avail lock_avail;
		struct fsal_up_event_invalidate invalidate;
		struct fsal_up_event_update update;
		struct fsal_up_event_link link;
		struct fsal_up_event_unlink unlink;
		struct fsal_up_event_move_from move_from;
		struct fsal_up_event_move_to move_to;
		struct fsal_up_event_rename rename;
		struct fsal_up_event_layoutrecall layoutrecall;
		struct fsal_up_event_recallany recallany;
		struct fsal_up_event_notifydevice notifydevice;
		struct fsal_up_event_delegrecall delegrecall;
	} data; /*< Type specific event data */
	struct fsal_up_file file; /*< File upon which the event takes
                                      place.  Interpetation varies by
                                      type and might not be used at
                                      all. */
	void *private; /*< This is private event data shared between
                           the immediate and queued function.  If you
                           override one function, you should override
                           the other, too, so this is neither left
                           dangling nor expected.  If you pass on to
                           one function, you should pass on to the
                           other and ensure this pointer is not
                           damaged.  If you want your own
                           event-private data, store whatever pointer
                           you find here in it so you can pass it
                           on.  This is NOT private data for the
                           producer of the event.  Producers of
                           events should initialize it to NULL (but
                           the immediate functions for events
                           shouldn't really care if they don't.)  The
                           queue function is responsible for freeing
                           anything allocated by the immediate
                           function. */
};

typedef int (*fsal_up_immfunc_t)(struct fsal_up_event *);
typedef void (*fsal_up_queuefunc_t)(struct fridgethr_context *);

/**
 * @brief The vector for up-call operations
 *
 * This structure contains two arrays of function for each operation.
 * One is immediate (for validation or quick dispatch.  It has the
 * ability to signal failure to the FSAL when the event is queued) and
 * the second which is executed by the up-call thread when an event is
 * de-queued.  Either may be NULL.
 *
 * Stacked FSAL authors may override functions completely or cascade
 * into them.  An FSAL should save the vector passed to it and pass a
 * vector of its own functions down to FSALs it initializes.  To
 * cascade, the FSAL must call the function in the supplied vector
 * initially.  To disable a function completely (for example to do all
 * processing in the immediate function and have no queued function at
 * all) simply set it to NULL in your vector.
 *
 * If the immediate function for an operation returns non-zero, the
 * event is not queued.
 */

struct fsal_up_vector {
	fsal_up_immfunc_t imm[FSAL_UP_EVENT_DELEGATION_RECALL + 1];
	fsal_up_queuefunc_t queue[FSAL_UP_EVENT_DELEGATION_RECALL + 1];
};

extern struct fsal_up_vector fsal_up_top;

/****************************
 * FSAL UP utility functions
 ****************************/

int fsal_up_init(void);
int fsal_up_shutdown(void);
int fsal_up_pause(void);
int fsal_up_resume(void);
int fsal_up_submit(struct fsal_up_event *event);

int up_get(const struct gsh_buffdesc *key,
	   cache_entry_t **entry);


struct fsal_up_event *fsal_up_alloc_event(void);
void fsal_up_free_event(struct fsal_up_event *event);

#endif /* _FSAL_UP_H */
