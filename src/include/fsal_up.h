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

/**
 * @brief FSAL upcall thread state
 *
 * This structure encapsulate the data used by the FSAL upcall
 * management thread.
 */

struct fsal_up_state
{
        bool stop; /*< Set to true to signal the thread that it
                       should shut down. */
        bool running; /*< Is true when the thread is running. */
        bool shutdown; /*< Set to true to indicate that the thread is
                           shut down.  running && shutdown indicates
                           that the thread is in the process of
                           shutting down. */
        struct glist_head queue; /*< Event queue */
        pool_t *pool; /*< Pool from which to allocate events */
        pthread_t thread_id; /*< Thread ID of FSAL up thread */
        pthread_mutex_t lock; /*< Lock governing access to this
                                  structure. */
        pthread_cond_t cond; /*< Condition variable for thread. */
};

#ifndef FSAL_UP_THREAD_C
extern struct fsal_up_state fsal_up_state;
#endif /* FSAL_UP_THREAD_C */

/**
 * @brief The actual state used to manage the thread
 */

/**
 * @brief An enumeration of supported events
 */

typedef enum {
        FSAL_UP_EVENT_LOCK_GRANT,
        FSAL_UP_EVENT_LOCK_AVAIL,
        FSAL_UP_EVENT_INVALIDATE,
        FSAL_UP_EVENT_UPDATE,
        FSAL_UP_EVENT_LAYOUTRECALL
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
        void              * lock_owner; /*< The lock owner */
        fsal_lock_param_t   lock_param; /*< A description of the lock */
};

/**
 * @brief Structure identifying a newly available lock
 */

struct fsal_up_event_lock_avail
{
        void              * lock_owner; /*< The lock owner */
        fsal_lock_param_t   lock_param; /*< A description of the lock */
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
        uint64_t offset; /*< For LAYOUTRECALL4_FILE, the offset of
                             the region to recall. */
        uint64_t length; /*< For LAYOUTRECALL4_FILE, the length of
                             the region to recall. */
        void *cookie; /*< A cookie returned with the return that
                          completely satisfies a recall. */
};

/**
 * @brief The vector for up-call operations
 *
 * This structure contains two functions for each operation.  One is
 * immediate (for validation or quick dispatch.  It has the ability to
 * signal failure to the FSAL when the event is queued) and the second
 * is executed by the up-call thread when an event is de-queued.
 * Either may be NULL.
 *
 * Stacked FSAL authors may override functions completely or cascade
 * into them.  An FSAL should save the vector passed to it and pass a
 * vector of its own functions down to FSALs it initializes.  To
 * cascade, the FSAL must call the function in the supplied vector
 * initially.  To disable a function completely (for example to do all
 * processing in the immediate function and have no queued function at
 * all) simply set it to NULL in your vector.
 *
 * If the _imm function for an operation returns non-zero, the event
 * is not queued.
 */

struct fsal_up_vector
{
        int (*lock_grant_imm)(struct fsal_up_event_lock_grant *,
                              struct fsal_up_file *,
                              void **);
        void (*lock_grant_queue)(struct fsal_up_event_lock_grant *,
                                 struct fsal_up_file *,
                                 void *);

        int (*lock_avail_imm)(struct fsal_up_event_lock_avail *,
                              struct fsal_up_file *,
                              void **);
        void (*lock_avail_queue)(struct fsal_up_event_lock_avail *,
                                 struct fsal_up_file *,
                                 void *);

        int (*invalidate_imm)(struct fsal_up_event_invalidate *,
                              struct fsal_up_file *,
                              void **);
        void (*invalidate_queue)(struct fsal_up_event_invalidate *,
                                 struct fsal_up_file *,
                                 void *);

        int (*update_imm)(struct fsal_up_event_update *,
                          struct fsal_up_file *,
                          void **);
        void (*update_queue)(struct fsal_up_event_update *,
                             struct fsal_up_file *,
                             void *);

        int (*layoutrecall_imm)(struct fsal_up_event_layoutrecall *,
                                struct fsal_up_file *,
                                void **);
        void (*layoutrecall_queue)(struct fsal_up_event_layoutrecall *,
                                   struct fsal_up_file *,
                                   void *);
};

struct fsal_up_vector fsal_up_top;

/**
 * @brief A single up-call event.
 */

struct fsal_up_event
{
        struct glist_head event_link; /*< Link in the event queue */
        struct fsal_up_vector *functions; /*< Vector of upcall
                                              functions.  Should be
                                              filled in by the FSAL
                                              with the vector
                                              supplied. */
        fsal_up_event_type_t type; /*< The type of event reported */
        union {
                struct fsal_up_event_lock_grant lock_grant;
                struct fsal_up_event_lock_avail lock_avail;
                struct fsal_up_event_invalidate invalidate;
                struct fsal_up_event_update update;
                struct fsal_up_event_layoutrecall layoutrecall;
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



/****************************
 * FSAL UP utility functions
 ****************************/

void init_FSAL_up(void);
int shutdown_FSAL_up(void);
int fsal_up_submit(struct fsal_up_event *event);


int up_get(const struct gsh_buffdesc *key,
           cache_entry_t **entry);




struct fsal_up_event *fsal_up_alloc_event(void);

void fsal_up_free_event(struct fsal_up_event *event);
#endif /* _FSAL_UP_H */
