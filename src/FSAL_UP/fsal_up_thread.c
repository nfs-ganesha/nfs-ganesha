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
 * @file    fsal_up_thread.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#define FSAL_UP_THREAD_C

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "nfs_core.h"
#include "log.h"
#include "fsal_up.h"
#include "err_fsal.h"
#include "nfs_tcb.h"
#include "cache_inode_lru.h"

struct fsal_up_state fsal_up_state = {
        .stop = false,
        .running = false,
        .pool = NULL,
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
};

/**
 * @brief Submit an upcall event
 *
 * This function submits an upcall event.  The event type, event data,
 * and file must be filled out as appropriate, and the upcall function
 * vector must be set to upcall vector supplied to create_export.
 *
 * @param[in] event The event to submit
 *
 * @retval 0 Operation submitted successfully.
 * @retval EINVAL Operation malformed.
 * @retval EPIPE Upcall thread not running/shutting down.
 * @retval Other codes as specified by _imm call.
 */

int
fsal_up_submit(struct fsal_up_event *event)
{
        int rc = 0;

        if (!event->functions ||
            !event->file.export) {
                return EINVAL;
        }

        pthread_mutex_lock(&fsal_up_state.lock);
        if (!fsal_up_state.running ||
            fsal_up_state.shutdown) {
                pthread_mutex_unlock(&fsal_up_state.lock);
                return EPIPE;
        }

        switch (event->type) {
        case FSAL_UP_EVENT_LOCK_GRANT:
                if (event->functions->lock_grant_imm) {
                        rc = event->functions->lock_grant_imm(
                                &event->data.lock_grant,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_LOCK_AVAIL:
                if (event->functions->lock_avail_imm) {
                        rc = event->functions->lock_avail_imm(
                                &event->data.lock_avail,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_INVALIDATE:
                if (event->functions->invalidate_imm) {
                        rc = event->functions->invalidate_imm(
                                &event->data.invalidate,
                                &event->file,
                                &event->private);
                        if (rc == ENOENT)
                          rc = 0;
                }
                break;

        case FSAL_UP_EVENT_UPDATE:
                if (event->functions->update_imm) {
                        rc = event->functions->update_imm(
                                &event->data.update,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_LINK:
                if (event->functions->link_imm) {
                        rc = event->functions->link_imm(
                                &event->data.link,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_UNLINK:
                if (event->functions->unlink_imm) {
                        rc = event->functions->unlink_imm(
                                &event->data.unlink,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_MOVE_FROM:
                if (event->functions->move_from_imm) {
                        rc = event->functions->move_from_imm(
                                &event->data.move_from,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_MOVE_TO:
                if (event->functions->move_to_imm) {
                        rc = event->functions->move_to_imm(
                                &event->data.move_to,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_RENAME:
                if (event->functions->rename_imm) {
                        rc = event->functions->rename_imm(
                                &event->data.rename,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_LAYOUTRECALL:
                if (event->functions->layoutrecall_imm) {
                        rc = event->functions->layoutrecall_imm(
                                &event->data.layoutrecall,
                                &event->file,
                                &event->private);
                }
                break;

        case FSAL_UP_EVENT_RECALL_ANY:
                if (event->functions->recallany_imm) {
                        rc = event->functions->recallany_imm(
                                &event->data.recallany,
                                event->private);
                        }
		break;

        case FSAL_UP_EVENT_NOTIFY_DEVICE:
                if (event->functions->notifydevice_imm) {
                        rc = event->functions->notifydevice_imm(
                                &event->data.notifydevice,
                                event->private);
		}
		break;

        case FSAL_UP_EVENT_DELEGATION_RECALL:
                if (event->functions->delegrecall_imm) {
                        rc = event->functions->delegrecall_imm(
                                &event->data.delegrecall,
				&event->file,
				&event->private);
		}
                break;
	}

        if (rc != 0) {
                pthread_mutex_unlock(&fsal_up_state.lock);
                return rc;
        }

        glist_add_tail(&fsal_up_state.queue,
                       &event->event_link);
        pthread_cond_signal(&fsal_up_state.cond);
        pthread_mutex_unlock(&fsal_up_state.lock);
        return 0;
}

/**
 * @brief Run function for the FSAL UP thread
 *
 * This function pulls each event off the event queue and dispatches
 * to its delayed action function.  When instructed to, it shuts
 * itself down in an orderly fashion.
 *
 * @param[in] dummy Ignored
 *
 * @returns NULL.
 */

static void *
fsal_up_process_thread(void *dummy __attribute__((unused)))
{
        struct fsal_up_event *event;

        pthread_mutex_lock(&fsal_up_state.lock);

        fsal_up_state.running = true;

        SetNameFunction("fsal_up_process_thread");

next_event:

        /* We expect to have the fsal_up_state.lock at this point. */

        /* If we've been asked to stop, set shutdown so we can finish
           off pending events and not accept any more. */
        if (fsal_up_state.stop) {
                fsal_up_state.shutdown = true;
        }

        event = glist_first_entry(&fsal_up_state.queue,
                                  struct fsal_up_event,
                                  event_link);
        if (event != NULL) {
                glist_del(&event->event_link);
                pthread_mutex_unlock(&fsal_up_state.lock);
                /* Process the event */
                switch (event->type) {
                case FSAL_UP_EVENT_LOCK_GRANT:
                        if (event->functions->lock_grant_queue) {
                                event->functions->lock_grant_queue(
                                        &event->data.lock_grant,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_LOCK_AVAIL:
                        if (event->functions->lock_avail_queue) {
                                event->functions->lock_avail_queue(
                                        &event->data.lock_avail,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_INVALIDATE:
                        if (event->functions->invalidate_queue) {
                                event->functions->invalidate_queue(
                                        &event->data.invalidate,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_UPDATE:
                        if (event->functions->update_queue) {
                                event->functions->update_queue(
                                        &event->data.update,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_LINK:
                        if (event->functions->link_queue) {
                                event->functions->link_queue(
                                        &event->data.link,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_UNLINK:
                        if (event->functions->unlink_queue) {
                                event->functions->unlink_queue(
                                        &event->data.unlink,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_MOVE_FROM:
                        if (event->functions->move_from_queue) {
                                event->functions->move_from_queue(
                                        &event->data.move_from,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_MOVE_TO:
                        if (event->functions->move_to_queue) {
                                event->functions->move_to_queue(
                                        &event->data.move_to,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_RENAME:
                        if (event->functions->rename_queue) {
                                event->functions->rename_queue(
                                        &event->data.rename,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_LAYOUTRECALL:
                        if (event->functions->layoutrecall_queue) {
                                event->functions->layoutrecall_queue(
                                        &event->data.layoutrecall,
                                        &event->file,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_RECALL_ANY:
                        if (event->functions->recallany_queue) {
                                event->functions->recallany_queue(
                                        &event->data.recallany,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_NOTIFY_DEVICE:
                        if (event->functions->notifydevice_queue) {
                                event->functions->notifydevice_queue(
                                        &event->data.notifydevice,
                                        event->private);
                        }
                        break;

                case FSAL_UP_EVENT_DELEGATION_RECALL:
                        if (event->functions->delegrecall_queue) {
                                event->functions->delegrecall_queue(
                                        &event->data.delegrecall,
					&event->file,
					event->private);
                        }
                        break;
                }

                fsal_up_free_event(event);
                pthread_mutex_lock(&fsal_up_state.lock);
                goto next_event;
        } else if (!fsal_up_state.shutdown) {
                /* Wait for more */
                pthread_cond_wait(&fsal_up_state.cond,
                                  &fsal_up_state.lock);
                goto next_event;
        } else {
                pool_destroy(fsal_up_state.pool);
                fsal_up_state.running = false;
                pthread_mutex_unlock(&fsal_up_state.lock);
                pthread_exit(NULL);
        }

        return NULL;
}

/**
 * @brief Initialize the FSAL up-call system
 *
 * This function initializes the FSAL up-call state and starts the
 * thread.
 */

void
init_FSAL_up(void)
{
        /* The attributes governing the FSAL upcall thread */
        pthread_attr_t attr_thr;
        /* Return code from pthread operations */
        int code = 0;

        pthread_mutex_lock(&fsal_up_state.lock);
        /* Allocation of the FSAL UP pool */
        fsal_up_state.pool = pool_init("FSAL UP Data Pool",
                                       sizeof(struct fsal_up_event),
                                       pool_basic_substrate,
                                       NULL,
                                       NULL,
                                       NULL);

        if (fsal_up_state.pool == NULL) {
                LogFatal(COMPONENT_INIT,
                         "Error while initializing FSAL UP event pool");
        }

        init_glist(&fsal_up_state.queue);

        if (pthread_attr_init(&attr_thr) != 0) {
                LogCrit(COMPONENT_INIT,
                        "can't init FSAL UP thread's attributes");
        }

        if (pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM)
            != 0) {
                LogCrit(COMPONENT_INIT,
                        "can't set FSAL UP thread's scope");
        }

        if (pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE)
            != 0) {
                LogCrit(COMPONENT_INIT,
                        "can't set FSAL UP thread's join state");
        }

        if (pthread_attr_setstacksize(&attr_thr, THREAD_STACK_SIZE)
            != 0) {
                LogCrit(COMPONENT_INIT,
                        "can't set FSAL UP thread's stack size");
        }

        /* spawn LRU background thread */
        code = pthread_create(&fsal_up_state.thread_id,
                              &attr_thr,
                              fsal_up_process_thread,
                              NULL);
        if (code != 0) {
                code = errno;
                LogFatal(COMPONENT_CACHE_INODE_LRU,
                         "Unable to start FSAL UP thread, error code %d.",
                         code);
        }

        pthread_mutex_unlock(&fsal_up_state.lock);
        return;
}

/**
 * @brief Shut down the FSAL upcall thread
 *
 * This function shuts down the FSAL upcall thread, returning when it
 * has exited.  The thread is shut down in an orderly fashion and
 * allowed to queued tasks.
 *
 * @retval 0 if the thread is shut down successfully.
 * @retval EBUSY if someone else has already signalled for the thread
 *         to shut down.
 * @retval EPIPE if the thread is not running.
 * @retval Errors from pthread_join.
 */

int
shutdown_FSAL_up(void)
{
        int rc = 0;

        pthread_mutex_lock(&fsal_up_state.lock);
        if (fsal_up_state.stop) {
                /* Someone else has already requested shutdown */
                pthread_mutex_unlock(&fsal_up_state.lock);
                return EBUSY;
        }
        if (!fsal_up_state.running) {
                /* Thread isn't running */
                pthread_mutex_unlock(&fsal_up_state.lock);
                return EPIPE;
        }
        fsal_up_state.stop = true;
        pthread_cond_signal(&fsal_up_state.cond);
        fsal_up_state.stop = false;
        pthread_mutex_unlock(&fsal_up_state.lock);
        rc = pthread_join(fsal_up_state.thread_id, NULL);
        if (rc) {
                LogCrit(COMPONENT_FSAL_UP,
                        "pthread_join failed with %d.",
                        rc);
                return rc;
        }
        return 0;
}

struct fsal_up_event *
fsal_up_alloc_event(void)
{
        if (fsal_up_state.pool)
          return pool_alloc(fsal_up_state.pool, NULL);
        else
         return NULL;
}

void
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
