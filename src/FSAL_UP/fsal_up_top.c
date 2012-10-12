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
#include "HashTable.h"
#include "fsal_up.h"
#include "sal_functions.h"

/**
 * @brief Invalidate cached attributes and content
 *
 * We call into the cache and invalidate at once, since the operation
 * is inexpensive by design.
 *
 * @param[in] invalidate Invalidation parameters
 * @param[in] file       The file to invalidate
 *
 * @retval 0 on success.
 * @retval ENOENT if the entry is not in the cache.  (Harmless, since
 *         if it's not cached, there's nothing to invalidate.)
 */

static int
invalidate_imm(struct fsal_up_event_invalidate *invalidate,
               struct fsal_up_file *file)
{
        cache_entry_t *entry = NULL;
        int rc = 0;

        LogDebug(COMPONENT_FSAL_UP,
                 "Calling cache_inode_invalidate()");

        rc = up_get(&file->key,
                    &entry);
        if (rc == 0) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }

        return rc;
}

/**
 * @brief Immediate attribute function
 *
 * This function just performs basic validation of the parameters.
 *
 * @param[in] update Update parameters
 * @param[in] file   File to update (unused)
 *
 * @retval 0 on success.
 * @retval EINVAL if the update data are invalid.
 */

static int
update_imm(struct fsal_up_event_update *update,
           struct fsal_up_file *file)
{
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

        return 0;
}

/**
 * @brief Execute the delayed attribute update
 *
 * Update the entry attributes in accord with the supplied attributes
 * and control flags.
 *
 * @param[in] update Update data
 * @param[in] file   File to update
 */

static void
update_queue(struct fsal_up_event_update *update,
             struct fsal_up_file *file)
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
 * @brief Signal a lock grant
 *
 * Since the SAL has its own queue for such operations, we simply
 * queue there.
 *
 * @param[in] grant Details of the granted lock
 * @param[in] file  File on which the lock is granted
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int
lock_grant_imm(struct fsal_up_event_lock_grant *grant,
               struct fsal_up_file *file)
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
 * @param[in] avail Details of the available lock
 * @param[in] file  File on which the lock has become available
 *
 * @retval 0 on success.
 * @retval ENOENT if the file isn't in the cache (this shouldn't
 *         happen, since the SAL should have files awaiting locks
 *         pinned.)
 */

static int
lock_avail_imm(struct fsal_up_event_lock_avail *avail,
               struct fsal_up_file *file)
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


struct fsal_up_vector fsal_up_top = {
        .lock_grant_imm = lock_grant_imm,
        .lock_grant_queue = NULL,

        .lock_avail_imm = lock_avail_imm,
        .lock_avail_queue = NULL,

        .invalidate_imm = invalidate_imm,
        .invalidate_queue = NULL,

        .update_imm = update_imm,
        .update_queue = update_queue,

        .layoutrecall_imm = NULL,
        .layoutrecall_queue = NULL
};
