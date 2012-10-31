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
 * @file fsal_up_thread.c
 *
 * @brief Top level FSAL Upcall handlers
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "nfs_core.h"
#include "log.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_inode_avl.h"
#include "cache_inode_weakref.h"
#include "cache_inode_lru.h"
#include "HashTable.h"
#include "fsal_up.h"
#include "sal_functions.h"
#include "pnfs_utils.h"
#include "nfs_rpc_callback.h"

static int32_t cb_completion_func(rpc_call_t* call, rpc_call_hook hook,
                                 void* arg, uint32_t flags)
{
  char *fh;

  LogDebug(COMPONENT_NFS_CB, "%p %s", call,
           (hook == RPC_CALL_ABORT) ?
           "RPC_CALL_ABORT" :
           "RPC_CALL_COMPLETE");
  switch (hook) {
    case RPC_CALL_COMPLETE:
        /* potentially, do something more interesting here */
        LogDebug(COMPONENT_NFS_CB, "call result: %d", call->stat);
        fh = call->cbt.v_u.v4.args.argarray.argarray_val->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val;
        gsh_free(fh);
        cb_compound_free(&call->cbt);
        break;
    default:
        LogDebug(COMPONENT_NFS_CB, "%p unknown hook %d", call, hook);
        break;
  }
  return (0);
}

/**
 * @brief Invalidate cached attributes and content
 *
 * We call into the cache and invalidate at once, since the operation
 * is inexpensive by design.
 *
 * @param[in]  invalidate Invalidation parameters
 * @param[in]  file       The file to invalidate
 * @param[out] private    Unused
 *
 * @retval 0 on success.
 * @retval ENOENT if the entry is not in the cache.  (Harmless, since
 *         if it's not cached, there's nothing to invalidate.)
 */

static int
invalidate_imm(struct fsal_up_event_invalidate *invalidate,
               struct fsal_up_file *file,
               void **private)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "Calling cache_inode_invalidate()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_ATTRS |
                                       CACHE_INODE_INVALIDATE_CONTENT);
                cache_inode_put(entry);
        }

        return rc;
}

/**
 * @brief Immediate attribute function
 *
 * This function just performs basic validation of the parameters.
 *
 * @param[in]  update  Update parameters
 * @param[in]  file    File to update (unused)
 * @param[out] private Unused
 *
 * @retval 0 on success.
 * @retval EINVAL if the update data are invalid.
 */

static int
update_imm(struct fsal_up_event_update *update,
           struct fsal_up_file *file,
           void **private)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        /* These cannot be updated, changing any of them is
           tantamount to destroying and recreating the file. */
        if (FSAL_TEST_MASK(update->attr.mask,
                           ATTR_SUPPATTR   | ATTR_TYPE        |
                           ATTR_FSID       | ATTR_FILEID      |
                           ATTR_RAWDEV     | ATTR_MOUNTFILEID |
                           ATTR_RDATTR_ERR | ATTR_GENERATION)) {
                return EINVAL;
        }

        /* Filter out garbage flags */

        if (update->flags & ~(fsal_up_update_filesize_inc |
                              fsal_up_update_atime_inc    |
                              fsal_up_update_creation_inc |
                              fsal_up_update_ctime_inc    |
                              fsal_up_update_mtime_inc    |
                              fsal_up_update_chgtime_inc  |
                              fsal_up_update_spaceused_inc)) {
                return EINVAL;
        }

        LogDebug(COMPONENT_FSAL_UP,
                 "Calling cache_inode_invalidate()");

        rc = up_get(&file->key, &entry);
        if (rc == 0) {

           if ((update->flags & fsal_up_nlink) &&
               (update->attr.numlinks == 0) )
           {
              LogDebug(COMPONENT_FSAL_UP,
                  "nlink has become zero; close fds");
              cache_inode_invalidate(entry,
                             (CACHE_INODE_INVALIDATE_ATTRS |
                              CACHE_INODE_INVALIDATE_CLOSE));
           }
           else
              cache_inode_invalidate(entry,
                                     CACHE_INODE_INVALIDATE_ATTRS);

           cache_inode_put(entry);
        }
        return 0;
}

/**
 * @brief Execute the delayed attribute update
 *
 * Update the entry attributes in accord with the supplied attributes
 * and control flags.
 *
 * @param[in] update  Update data
 * @param[in] file    File to update
 * @param[in] private Unused
 */

static void
update_queue(struct fsal_up_event_update *update,
             struct fsal_up_file *file,
             void *private)
{
        /* The cache entry upon which to operate */
        cache_entry_t *entry = NULL;
        /* Have necessary changes been made? */
        bool mutatis_mutandis = false;

        if (up_get(&file->key, &entry) == 0) {
                pthread_rwlock_wrlock(&entry->attr_lock);

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_SIZE) &&
                    ((update->flags & ~fsal_up_update_filesize_inc) ||
                     (entry->obj_handle->attributes.filesize <=
                      update->attr.filesize))) {
                        entry->obj_handle->attributes.filesize =
                                update->attr.filesize;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_ACL)) {
                        /**
                         * @todo Someone who knows the ACL code,
                         * please look over this.  We assume that the
                         * FSAL takes a reference on the supplied ACL
                         * that we can then hold onto.  This seems the
                         * most reasonable approach in an asynchronous
                         * call.
                         */

                        /* This idiom is evil. */
                        fsal_acl_status_t acl_status;

                        nfs4_acl_release_entry(
                                entry->obj_handle->attributes.acl,
                                &acl_status);

                        entry->obj_handle->attributes.acl =
                                update->attr.acl;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_MODE)) {
                        entry->obj_handle->attributes.mode =
                                update->attr.mode;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_NUMLINKS)) {
                        entry->obj_handle->attributes.numlinks =
                                update->attr.numlinks;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_OWNER)) {
                        entry->obj_handle->attributes.owner =
                                update->attr.owner;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_GROUP)) {
                        entry->obj_handle->attributes.group =
                                update->attr.group;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_ATIME) &&
                    ((update->flags & ~fsal_up_update_atime_inc) ||
                     (gsh_time_cmp(
                             update->attr.atime,
                             entry->obj_handle->attributes.atime) == 1))) {
                        entry->obj_handle->attributes.atime =
                                update->attr.atime;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_CREATION) &&
                    ((update->flags & ~fsal_up_update_creation_inc) ||
                     (gsh_time_cmp(
                             update->attr.creation,
                             entry->obj_handle->attributes.creation) == 1))) {
                        entry->obj_handle->attributes.creation =
                                update->attr.creation;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_CTIME) &&
                    ((update->flags & ~fsal_up_update_ctime_inc) ||
                     (gsh_time_cmp(
                             update->attr.ctime,
                             entry->obj_handle->attributes.ctime) == 1))) {
                        entry->obj_handle->attributes.ctime =
                                update->attr.ctime;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_MTIME) &&
                    ((update->flags & ~fsal_up_update_mtime_inc) ||
                     (gsh_time_cmp(
                             update->attr.mtime,
                             entry->obj_handle->attributes.mtime) == 1))) {
                        entry->obj_handle->attributes.mtime =
                                update->attr.mtime;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_SPACEUSED) &&
                    ((update->flags & ~fsal_up_update_spaceused_inc) ||
                     (entry->obj_handle->attributes.spaceused <=
                      update->attr.spaceused))) {
                        entry->obj_handle->attributes.spaceused =
                                update->attr.spaceused;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_CHGTIME) &&
                    ((update->flags & ~fsal_up_update_chgtime_inc) ||
                     (gsh_time_cmp(
                             update->attr.chgtime,
                             entry->obj_handle->attributes.chgtime) == 1))) {
                        entry->obj_handle->attributes.chgtime =
                                update->attr.chgtime;
                        mutatis_mutandis = true;
                }

                if (FSAL_TEST_MASK(update->attr.mask,
                                   ATTR_CHANGE)) {
                        entry->obj_handle->attributes.change =
                                update->attr.change;
                        mutatis_mutandis = true;
                }

                if (mutatis_mutandis) {
                        cache_inode_fixup_md(entry);
                }

                pthread_rwlock_unlock(&entry->attr_lock);
                cache_inode_put(entry);
        }
}

/**
 * @brief Initiate a lock grant
 *
 * This function calls out to the SAL to grant a lock.  This is
 * handled in the immediate phase, because NSM operations have their
 * own queue.
 *
 * @param[in]  grant   Event data
 * @param[in]  file    File on which to grant the lock
 * @param[out] private Unused
 *
 * @retval 0 if successfully queued.
 * @retval ENOENT if the entry is not in the cache (probably can't
 *         happen).
 */

static int
lock_grant_imm(struct fsal_up_event_lock_grant *grant,
               struct fsal_up_file *file,
               void **private)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "calling cache_inode_get()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                LogDebug(COMPONENT_FSAL_UP,
                         "Lock Grant found entry %p",
                         entry);

                grant_blocked_lock_upcall(entry,
                                          grant->lock_owner,
                                          &grant->lock_param);

                if (entry) {
                        cache_inode_put(entry);
                }
        }

        return rc;
}

/**
 * @brief Signal lock availability
 *
 * Since the SAL has its own queue for such operations, we simply
 * queue there.
 *
 * @param[in]  avail   Details of the available lock
 * @param[in]  file    File on which the lock has become available
 * @param[out] private Unused
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int
lock_avail_imm(struct fsal_up_event_lock_avail *avail,
               struct fsal_up_file *file,
               void **private)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                LogDebug(COMPONENT_FSAL_UP,
                         "Lock Grant found entry %p",
                         entry);

                available_blocked_lock_upcall(entry,
                                              avail->lock_owner,
                                              &avail->lock_param);

                if (entry) {
                        cache_inode_put(entry);
                }
        }

        return rc;
}

/**
 * @brief Execute delayed link
 *
 * Add a link to a directory and, if the entry's attributes are valid,
 * increment the link count by one.
 *
 * @param[in] link    Link parameters
 * @param[in] file    Directory in which the link was created
 * @param[in] private Unused
 */

static void
link_queue(struct fsal_up_event_link *link,
           struct fsal_up_file *file,
           void *private)
{
        /* The cache entry for the parent directory */
        cache_entry_t *parent = NULL;
        /* Fake root credentials for caching lookup */
        struct user_cred synthetic_creds = {
                .caller_uid = 0,
                .caller_gid = 0,
                .caller_glen = 0,
                .caller_garray = NULL
        };

        /* Synthetic request context */
        struct req_op_context synthetic_context = {
                .creds = &synthetic_creds,
                .caller_addr = NULL,
                .clientid = NULL
        };


        if (up_get(&file->key, &parent) == 0) {
                /* The entry to look up */
                cache_entry_t *entry = NULL;
                /* Cache inode status */
                cache_inode_status_t status = CACHE_INODE_SUCCESS;

                if (!link->target.key.addr) {
                        /* If the FSAL didn't specify a target, just
                           do a lookup and let it cache. */
                        if ((entry = cache_inode_lookup(parent,
                                                        link->name,
                                                        &synthetic_context,
                                                        &status)) == NULL) {
                                cache_inode_invalidate(
                                        parent,
                                        CACHE_INODE_INVALIDATE_CONTENT);
                        } else {
                                cache_inode_put(entry);
                        }
                } else {
                        if (up_get(&link->target.key, &entry) == 0) {
                                cache_inode_add_cached_dirent(parent,
                                                              link->name,
                                                              entry,
                                                              NULL,
                                                              &status);
                                pthread_rwlock_wrlock(&entry->attr_lock);
                                if (entry->flags &
                                    CACHE_INODE_TRUST_ATTRS) {
                                        ++entry->obj_handle
                                                ->attributes.numlinks;
                                }
                                pthread_rwlock_unlock(&entry->attr_lock);
                                cache_inode_put(entry);
                        } else {
                                cache_inode_invalidate(
                                        parent,
                                        CACHE_INODE_INVALIDATE_CONTENT);
                        }
                }
        }
        gsh_free(link->name);
}

/**
 * @brief Delayed unlink action
 *
 * Remove the name from the directory, and if the entry is cached,
 * decrement its link count.
 *
 * @param[in] unlink  Unlink parameters
 * @param[in] file    Directory from we unlinked
 * @param[in] private Unused
 */

static void
unlink_queue(struct fsal_up_event_unlink *unlink,
             struct fsal_up_file *file,
             void *private)
{
        /* The cache entry for the parent directory */
        cache_entry_t *parent = NULL;

        if (up_get(&file->key, &parent) == 0) {
                /* Cache inode status */
                cache_inode_status_t status = CACHE_INODE_SUCCESS;
                /* The looked up directory entry */
                cache_inode_dir_entry_t *dirent;

                pthread_rwlock_wrlock(&parent->content_lock);
                dirent = cache_inode_avl_qp_lookup_s(parent, unlink->name, 1);
                if (dirent &&
                    ~(dirent->flags & DIR_ENTRY_FLAG_DELETED)) {
                        /* The entry to ding */
                        cache_entry_t *entry = NULL;
                        if ((entry = cache_inode_weakref_get(&dirent->entry,
                                                             0))) {
                                pthread_rwlock_wrlock(&entry->attr_lock);
                                if (entry->flags &
                                    CACHE_INODE_TRUST_ATTRS) {
                                        if (--entry->obj_handle
                                            ->attributes.numlinks
                                            == 0) {
                                                pthread_rwlock_unlock(
                                                        &entry->attr_lock);
                                                cache_inode_lru_kill(
                                                        entry);
                                        } else {
                                                pthread_rwlock_unlock(
                                                        &entry->attr_lock);
                                        }
                                }
                                cache_inode_put(entry);
                        }
                        cache_inode_remove_cached_dirent(parent,
                                                         unlink->name,
                                                         &status);
                }
                pthread_rwlock_unlock(&parent->content_lock);
                cache_inode_put(parent);
        }
        gsh_free(unlink->name);
}

/**
 * @brief Delayed move-from action
 *
 * Remove the name from the directory, do not modify the link count.
 *
 * @param[in] move_from move-from parameters
 * @param[in] file      Directory from we unlinked
 * @param[in] private   Unused
 */

static void
move_from_queue(struct fsal_up_event_move_from *move_from,
                struct fsal_up_file *file,
                void *private)
{
        /* The cache entry for the parent directory */
        cache_entry_t *parent = NULL;

        if (up_get(&file->key, &parent) == 0) {
                /* Cache inode status */
                cache_inode_status_t status = CACHE_INODE_SUCCESS;

                pthread_rwlock_wrlock(&parent->content_lock);
                cache_inode_remove_cached_dirent(parent,
                                                 move_from->name,
                                                 &status);
                pthread_rwlock_unlock(&parent->content_lock);
                cache_inode_put(parent);
        }
        gsh_free(move_from->name);
}

/**
 * @brief Execute delayed move-to
 *
 * Add a link to a directory, do not touch the number of links.
 *
 * @param[in] move_to move-to parameters
 * @param[in] file    Directory in which the link was created
 * @param[in] private Unused
 */

static void
move_to_queue(struct fsal_up_event_move_to *move_to,
              struct fsal_up_file *file,
              void *private)
{
        /* The cache entry for the parent directory */
        cache_entry_t *parent = NULL;
        /* Fake root credentials for caching lookup */
        struct user_cred synthetic_creds = {
                .caller_uid = 0,
                .caller_gid = 0,
                .caller_glen = 0,
                .caller_garray = NULL
        };

        /* Synthetic request context */
        struct req_op_context synthetic_context = {
                .creds = &synthetic_creds,
                .caller_addr = NULL,
                .clientid = NULL
        };

        if (up_get(&file->key, &parent) == 0) {
                /* The entry to look up */
                cache_entry_t *entry = NULL;
                /* Cache inode status */
                cache_inode_status_t status = CACHE_INODE_SUCCESS;

                if (!move_to->target.key.addr) {
                        /* If the FSAL didn't specify a target, just
                           do a lookup and let it cache. */
                        if ((entry = cache_inode_lookup(parent,
                                                        move_to->name,
                                                        &synthetic_context,
                                                        &status)) == NULL) {
                                cache_inode_invalidate(
                                        parent,
                                        CACHE_INODE_INVALIDATE_CONTENT);
                        } else {
                                cache_inode_put(entry);
                        }
                } else {
                        if (up_get(&move_to->target.key, &entry) == 0) {
                                cache_inode_add_cached_dirent(parent,
                                                              move_to->name,
                                                              entry,
                                                              NULL,
                                                              &status);
                                cache_inode_put(entry);
                        } else {
                                cache_inode_invalidate(
                                        parent,
                                        CACHE_INODE_INVALIDATE_CONTENT);
                        }
                }
                cache_inode_put(parent);
        }
        gsh_free(move_to->name);
}

/**
 * @brief Delayed rename operation
 *
 * If a parent directory is in the queue, rename the given entry.  On
 * error, invalidate the whole thing.
 *
 * @param[in] rename  Rename parameters
 * @param[in] file    The parent directory
 * @param[in] private Unused
 */

static void
rename_queue(struct fsal_up_event_rename *rename,
             struct fsal_up_file *file,
             void *private)
{
        /* The cache entry for the parent directory */
        cache_entry_t *parent = NULL;

        if (up_get(&file->key, &parent) == 0) {
                /* Cache inode status */
                cache_inode_status_t status = CACHE_INODE_SUCCESS;

                pthread_rwlock_wrlock(&parent->content_lock);
                if (cache_inode_rename_cached_dirent(parent,
                                                     rename->old,
                                                     rename->new,
                                                     &status)
                    != CACHE_INODE_SUCCESS) {
                        cache_inode_invalidate(parent,
                                               CACHE_INODE_INVALIDATE_CONTENT);
                }
                pthread_rwlock_unlock(&parent->content_lock);
                cache_inode_put(parent);
        }
        gsh_free(rename->old);
        gsh_free(rename->new);
}

/**
 * @brief Create layout recall state
 *
 * This function creates the layout recall state and work queue for a
 * LAYOUTRECALL operation on a file.  The state lock on the entry must
 * be held for write when this function is called.
 *
 * This is made somewhat more problematic by the fact that every
 * layout state that has some number of matching segments should
 * receive a single LAYOUTRECALL for the entire range, while each
 * segment matching a recall should be returned individually to the
 * FSAL.
 *
 * LAYOUTRECALL event MUST NOT be initiated from the layoutreturn or
 * layoutcommit functions.
 *
 * @param[in,out] entry   The entry on which to send the recall
 * @param[in]     type    The layout type
 * @param[in]     offset  The offset of the interval to recall
 * @param[in]     length  The length of the interval to recall
 * @param[in]     cookie  The recall cookie (to be returned to the FSAL
 *                        on the final return satisfying this recall.)
 * @param[out]    private The work queue
 *
 * @retval 0 if successfully queued.
 * @retval EINVAL if the range is zero or overflows.
 * @retval ENOENT if no layouts satisfying the range exist.
 * @retval ENOMEM if there was insufficient memory to construct the
 *         recall state.
 */

static int
create_file_recall(cache_entry_t *entry,
                   layouttype4 type,
                   const struct pnfs_segment *segment,
                   void *cookie,
                   void **private)
{
        /* True if no layouts matching the request have been found */
        bool none = true;
        /* Head of the work queue */
        struct glist_head *queue
                = gsh_malloc(sizeof(struct glist_head));
        /* Iterator over all states on the cache entry */
        struct glist_head *state_iter = NULL;
        /* Error return code */
        int rc = 0;

        if (!queue) {
                rc = ENOMEM;
                goto out;
        }

        if ((segment->length == 0) ||
            ((segment->length != UINT64_MAX) &&
             (segment->offset <= UINT64_MAX - segment->length))) {
                rc = EINVAL;
                goto out;
        }

        glist_for_each(state_iter,
                       &entry->state_list) {
                struct recall_work_queue *work_entry = NULL;;
                /* Iterator over segments on this state */
                struct glist_head *seg_iter = NULL;
                /* The state under examination */
                state_t *s = glist_entry(state_iter,
                                         state_t,
                                         state_list);
                bool match = false;

                if ((s->state_type != STATE_TYPE_LAYOUT) ||
                    (s->state_data.layout.state_layout_type !=
                     type)) {
                        continue;
                }
                glist_for_each(seg_iter,
                               &entry->state_list) {
                        state_layout_segment_t *g
                                = glist_entry(seg_iter,
                                              state_layout_segment_t,
                                              sls_state_segments);
                        pthread_mutex_lock(&g->sls_mutex);
                        if (pnfs_segments_overlap(segment,
                                                  &g->sls_segment)) {
                                match = true;
                                pthread_mutex_unlock(&g->sls_mutex);
                        }
                }
                if (match) {
                        work_entry = gsh_malloc(
                                sizeof(struct recall_work_queue));
                        if (!work_entry) {
                                rc = ENOMEM;
                                goto out;
                        }
                        init_glist(&work_entry->link);
                        work_entry->state = s;
                        work_entry->recalled = false;
                        glist_add_tail(queue, &work_entry->link);
                        none = false;
                }
        }

        if (none) {
                rc = ENOENT;
        }

out:

        if (rc != 0) {
                if (queue) {
                        /* Entry in the queue we're disposing */
                        struct glist_head *queue_iter = NULL;
                        /* Placeholder so we can delete entries without
                           facing untold misery */
                        struct glist_head *holder = NULL;
                        glist_for_each_safe(queue_iter,
                                            holder,
                                            queue) {
                                struct recall_work_queue *g
                                        = glist_entry(queue_iter,
                                                      struct recall_work_queue,
                                                      link);
                                glist_del(queue_iter);
                                gsh_free(g);
                        }
                        gsh_free(queue);
                }
        } else {
                struct state_layout_recall_file *recall = gsh_malloc(
                        sizeof(struct state_layout_recall_file));

                if (!recall) {
                        rc = ENOMEM;
                        goto out;
                }
                init_glist(&recall->entry_link);
                recall->entry = entry;
                recall->type = type;
                recall->segment = *segment;
                recall->state_list = queue;
                recall->recall_cookie = cookie;
                glist_add_tail(&entry->layoutrecall_list,
                               &recall->entry_link);
                *private = queue;
        }

        return rc;
}

/**
 * @brief Initiate layout recall
 *
 * This function validates the recall, creates the recall object, and
 * produces a work queue of layout states to which to send a
 * CB_LAYOUTRECALL.
 *
 * @param[in]  layoutrecall Event data
 * @param[in]  file         File on which to issue the recall
 * @param[out] private      Layout recall work queue
 *
 * @retval 0 if scheduled.
 * @retval ENOENT if no matching layouts exist.
 * @retval ENOTSUP if an unsupported recall type has been provided.
 * @retval EINVAL if a nonsensical layout recall has been specified.
 */

static int
layoutrecall_imm(struct fsal_up_event_layoutrecall *layoutrecall,
                 struct fsal_up_file *file,
                 void **private)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        if (!file->export) {
                return EINVAL;
        }

        switch (layoutrecall->recall_type) {
        case LAYOUTRECALL4_ALL:
                LogCrit(COMPONENT_FSAL_UP,
                        "LAYOUTRECALL4_ALL is not supported as a "
                        "recall type and never will be.  Called from "
                        "export %d.", file->export->exp_entry->id);
                return ENOTSUP;

        case LAYOUTRECALL4_FSID:
                LogCrit(COMPONENT_FSAL_UP,
                        "LAYOUTRECALL4_FSID is not currently supported.  "
                        "Called from export %d.", file->export->exp_entry->id);
                return ENOTSUP;

        case LAYOUTRECALL4_FILE:
                rc = up_get(&file->key,
                            &entry);
                if (rc != 0) {
                        return rc;
                }
                pthread_rwlock_wrlock(&entry->state_lock);
                /* We create the file recall state here and link it
                   to the cache entry, but actually send out the
                   messages from the queued function.  We do the
                   build here so that the FSAL can be notified if no
                   layouts matching the recall exist. */
                rc = create_file_recall(entry,
                                        layoutrecall->layout_type,
                                        &layoutrecall->segment,
                                        layoutrecall->cookie,
                                        private);
                pthread_rwlock_unlock(&entry->state_lock);
                cache_inode_put(entry);
                break;

        default:
                LogCrit(COMPONENT_FSAL_UP,
                        "Invalid recall type %d. Called from export %d.",
                        layoutrecall->recall_type,
                        file->export->exp_entry->id);
                return EINVAL;
        }

        return rc;
}

static void
layoutrecall_queue(struct fsal_up_event_layoutrecall *layoutrecall,
                   struct fsal_up_file *file,
                   void *private)
{
        struct glist_head *queue
                = (struct glist_head *)private;
        /* Entry in the queue we're disposing */
        struct glist_head *queue_iter = NULL;

        if (glist_empty(queue)) {
                /* One or more LAYOUTRETURNs raced us and emptied out
                   the queue */
                gsh_free(queue);
                return;
        }

        glist_for_each(queue_iter,
                       queue) {
                /* The current entry in the queue */
                struct recall_work_queue *g
                        = glist_entry(queue_iter,
                                      struct recall_work_queue,
                                      link);
                struct state_t *s = g->state;
                cache_entry_t  *entry = s->state_pentry;
                pthread_rwlock_wrlock(&entry->state_lock);
                /* Do something or other to recall the layout.  We
                   might want some specialty code in the lease timer
                   to treat a clientid with layouts that has had the
                   back channel down for more than the lease timer as
                   having gone dead, so that if the back channel goes
                   out to lunch we'll synthesize returns and free up
                   both the layout and the recall state. */
                /**
                 * @todo ACE: Hook into backchannel.  Don't forget to
                 * bump the seqid.
                 */
                g->recalled = true;
                pthread_rwlock_unlock(&entry->state_lock);
        }
};

static void
delegrecall_one(state_lock_entry_t * found_entry, cache_entry_t *pentry)
{
  char *maxfh;
  compound_data_t data;
  int32_t code = 0;
  rpc_call_channel_t *chan;
  rpc_call_t *call;
  nfs_client_id_t *clid = NULL;
  nfs_cb_argop4 argop[1];

  maxfh = gsh_malloc(NFS4_FHSIZE);     // free in cb_completion_func()
  if(maxfh == NULL)
  {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DELEG: no mem, failed.");
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      return;
  }
  code  = nfs_client_id_get_confirmed(found_entry->sle_owner->so_owner.so_nfs4_owner.so_clientid, &clid);
  if (code != CLIENT_ID_SUCCESS) {
      LogCrit(COMPONENT_NFS_CB,
              "No clid record  code %d", code);
      return;
  }
  chan = nfs_rpc_get_chan(clid, NFS_RPC_FLAG_NONE);
  if (! chan) {
      LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed");
      return;
  }
  if (! chan->clnt) {
      LogCrit(COMPONENT_NFS_CB, "nfs_rpc_get_chan failed (no clnt)");
      return;
  }
  /* allocate a new call--freed in completion hook */
  call = alloc_rpc_call();
  call->chan = chan;

  /* setup a compound */
  cb_compound_init_v4(&call->cbt, 6, clid->cid_cb.cb_u.v40.cb_callback_ident,
                      "brrring!!!", 10);

  memset(argop, 0, sizeof(nfs_cb_argop4));
  argop->argop = NFS4_OP_CB_RECALL;
  argop->nfs_cb_argop4_u.opcbrecall.stateid.seqid = found_entry->sle_state->state_seqid;
  memcpy(argop->nfs_cb_argop4_u.opcbrecall.stateid.other,
         found_entry->sle_state->stateid_other, OTHERSIZE);
  argop->nfs_cb_argop4_u.opcbrecall.truncate = TRUE;

  /* Convert it to a file handle */
  argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_len = 0;
  argop->nfs_cb_argop4_u.opcbrecall.fh.nfs_fh4_val = maxfh;

  data.pexport = found_entry->sle_pexport;

  /* Building a new fh */
  if (!nfs4_FSALToFhandle(&argop->nfs_cb_argop4_u.opcbrecall.fh,
                          pentry->obj_handle, &data))
     return;

  /* add ops, till finished (dont exceed count) */
  cb_compound_add_op(&call->cbt, argop);

  /* set completion hook */
  call->call_hook = cb_completion_func;

  /* call it (here, in current thread context) */
  code = nfs_rpc_submit_call(call, NFS_RPC_FLAG_NONE /* NFS_RPC_CALL_INLINE */);

  return;
};

static void
delegrecall_queue(struct fsal_up_event_delegrecall *deleg,
                                struct fsal_up_file *file)
{
  cache_entry_t *pentry = NULL;
  struct glist_head  *glist;
  state_lock_entry_t *found_entry = NULL;
  int rc = 0;

  rc = up_get(&file->key, &pentry);
  if (rc != 0 || pentry == NULL)
  {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DELEG: cache inode get failed, rc %d", rc);
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      return;
  }

  LogDebug(COMPONENT_FSAL_UP,
          "FSAL_UP_DELEG: Invalidate cache found entry %p type %u",
          pentry, pentry->type);

  pthread_rwlock_wrlock(&pentry->state_lock);

  glist_for_each(glist, &pentry->object.file.lock_list)
  {
      found_entry = glist_entry(glist, state_lock_entry_t, sle_list);

      if (found_entry != NULL && found_entry->sle_state != NULL)
      {
          LogDebug(COMPONENT_NFS_CB,"found_entry %p", found_entry);
          delegrecall_one(found_entry, pentry);
      }
  }
  pthread_rwlock_unlock(&pentry->state_lock);

  cache_inode_put(pentry);

  return;

};

static void
delegrecall_imm(struct fsal_up_event_delegrecall *deleg,
                                   struct fsal_up_file *file)
{
    printf("xxx delegrecall_imm ???\n");
};

struct fsal_up_vector fsal_up_top = {
        .lock_grant_imm = lock_grant_imm,
        .lock_grant_queue = NULL,

        .lock_avail_imm = lock_avail_imm,
        .lock_avail_queue = NULL,

        .invalidate_imm = invalidate_imm,
        .invalidate_queue = NULL,

        .update_imm = update_imm,
        .update_queue = update_queue,

        .link_imm = NULL,
        .link_queue = link_queue,

        .unlink_imm = NULL,
        .unlink_queue = unlink_queue,

        .move_from_imm = NULL,
        .move_from_queue = move_from_queue,

        .move_to_imm = NULL,
        .move_to_queue = move_to_queue,

        .rename_imm = NULL,
        .rename_queue = rename_queue,

        .layoutrecall_imm = layoutrecall_imm,
        .layoutrecall_queue = layoutrecall_queue,

        .delegrecall_imm = delegrecall_imm,
        .delegrecall_queue = delegrecall_queue
};
