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

/**
 * @brief The actual state used to manage the thread
 */

struct fsal_up_state fsal_up_state;

/**
 * @brief An enumeration of supported events
 */

typedef enum {
        FSAL_UP_EVENT_LOCK_GRANT = 1,
        FSAL_UP_EVENT_INVALIDATE = 2,
        FSAL_UP_EVENT_LAYOUTRECALL = 3
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
 * @brief A structure for cache invalidation
 */

struct fsal_up_event_invalidate
{
        uint32_t flags; /*< Flags governing invalidate. */
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
                              struct fsal_up_file *);
        void (*lock_grant_queue)(struct fsal_up_event_lock_grant *,
                                 struct fsal_up_file *);

        int (*invalidate_imm)(struct fsal_up_event_invalidate *,
                              struct fsal_up_file *);
        void (*invalidate_queue)(struct fsal_up_event_invalidate *,
                                 struct fsal_up_file *);

        int (*layoutrecall_imm)(struct fsal_up_event_layoutrecall *,
                                struct fsal_up_file *);
        void (*layoutrecall_queue)(struct fsal_up_event_layoutrecall *,
                                   struct fsal_up_file *);
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
                struct fsal_up_event_invalidate invalidate;
                struct fsal_up_event_layoutrecall layoutrecall;
        } data; /*< Type specific event data */
        struct fsal_up_file file; /*< File upon which the event takes
                                      place.  Interpetation varies by
                                      type and might not be used at
                                      all. */
};



/****************************
 * FSAL UP utility functions
 ****************************/

void init_FSAL_up(void);
int shutdown_FSAL_up(void);
int fsal_up_submit(struct fsal_up_event *event);


int up_get(const struct gsh_buffdesc *key,
           cache_entry_t **entry);



static inline struct fsal_up_event *
fsal_up_alloc_event(void)
{
        return pool_alloc(fsal_up_state.pool, NULL);
}

static inline void
fsal_up_free_event(struct fsal_up_event *event)
{
        if (event->file.key.addr) {
                gsh_free(event->file.key.addr);
                event->file.key.addr = NULL;
        }
        if (event->file.export) {
                event->file.export->ops->put(event->file.export);
                event->file.export = NULL;
        }
        pool_free(fsal_up_state.pool, event);
}
#endif /* _FSAL_UP_H */
