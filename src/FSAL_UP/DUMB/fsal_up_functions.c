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

/* Set the FSAL UP functions that will be used to process events.
 * This is called DUMB_FSAL_UP because it only invalidates cache inode
 * entires ... inode entries are not updated or refreshed through this
 * interface. */

fsal_status_t
dumb_fsal_up_invalidate_step1(fsal_up_event_data_t *evdata)
{
        cache_entry_t *entry = NULL;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_invalidate()");

        if (up_get(&evdata->event_context.fsal_data.fh_desc,
                   &entry) == CACHE_INODE_SUCCESS) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
dumb_fsal_up_invalidate_step2(fsal_up_event_data_t *evdata)
{
        cache_entry_t *entry = NULL;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_invalidate()");

        if (up_get(&evdata->event_context.fsal_data.fh_desc,
                   &entry) == CACHE_INODE_SUCCESS) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t
dumb_fsal_up_update(fsal_up_event_data_t *evdata)
{
        cache_entry_t *entry = NULL;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_invalidate()");

        if (up_get(&evdata->event_context.fsal_data.fh_desc,
                   &entry) == CACHE_INODE_SUCCESS) {
                cache_inode_invalidate(entry,
                                       CACHE_INODE_INVALIDATE_CLEARBITS);
                cache_inode_put(entry);
        }
        return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

#define INVALIDATE_STUB {                     \
    return dumb_fsal_up_invalidate_step1(evdata);  \
  } while(0);

fsal_status_t
dumb_fsal_up_create(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_unlink(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_rename(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_commit(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_write(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_link(fsal_up_event_data_t *evdata)
{
  INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_lock_grant(fsal_up_event_data_t *evdata)
{
#ifdef _USE_BLOCKING_LOCKS
        cache_entry_t        * entry = NULL;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_get()");
        up_get(&evdata->event_context.fsal_data.fh_desc,
               &entry);
        if (entry == NULL) {
                LogDebug(COMPONENT_FSAL_UP,
                         "FSAL_UP_DUMB: cache inode get failed.");
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: Lock Grant found entry %p",
                 entry);

        grant_blocked_lock_upcall(entry,
                                  evdata->type.lock_grant.lock_owner,
                                  &evdata->type.lock_grant.lock_param);

        if (entry) {
                cache_inode_put(entry);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
#else
        INVALIDATE_STUB;
#endif
}

fsal_status_t
dumb_fsal_up_lock_avail(fsal_up_event_data_t *evdata)
{
#ifdef _USE_BLOCKING_LOCKS
        cache_entry_t *entry = NULL;

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: calling cache_inode_get()");
        up_get(&evdata->event_context.fsal_data.fh_desc,
               &entry);
        if (entry == NULL) {
                LogDebug(COMPONENT_FSAL_UP,
                         "FSAL_UP_DUMB: cache inode get failed.");
                return fsalstat(ERR_FSAL_NO_ERROR, 0);
        }

        LogDebug(COMPONENT_FSAL_UP,
                 "FSAL_UP_DUMB: Lock Available found entry %p",
                 entry);

        available_blocked_lock_upcall(entry,
                                      evdata->type.lock_grant.lock_owner,
                                      &evdata->type.lock_grant.lock_param);

        if (entry) {
                cache_inode_put(entry);
        }

        return fsalstat(ERR_FSAL_NO_ERROR, 0);
#else
        INVALIDATE_STUB;
#endif
}

fsal_status_t
dumb_fsal_up_open(fsal_up_event_data_t *evdata)
{
        INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_close(fsal_up_event_data_t *evdata)
{
        INVALIDATE_STUB;
}

fsal_status_t
dumb_fsal_up_setattr(fsal_up_event_data_t *evdata)
{
        INVALIDATE_STUB;
}

fsal_up_event_functions_t dumb_event_func = {
        .fsal_up_create = dumb_fsal_up_create,
        .fsal_up_unlink = dumb_fsal_up_unlink,
        .fsal_up_rename = dumb_fsal_up_rename,
        .fsal_up_commit = dumb_fsal_up_commit,
        .fsal_up_write = dumb_fsal_up_write,
        .fsal_up_link = dumb_fsal_up_link,
        .fsal_up_lock_grant = dumb_fsal_up_lock_grant,
        .fsal_up_lock_avail = dumb_fsal_up_lock_avail,
        .fsal_up_open = dumb_fsal_up_open,
        .fsal_up_close = dumb_fsal_up_close,
        .fsal_up_setattr = dumb_fsal_up_setattr,
        .fsal_up_update = dumb_fsal_up_update,
        .fsal_up_invalidate = dumb_fsal_up_invalidate_step1
};

fsal_up_event_functions_t *
get_fsal_up_dumb_functions()
{
        return &dumb_event_func;
}
