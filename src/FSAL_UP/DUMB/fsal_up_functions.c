/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    fsal_up_thread.c
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

fsal_status_t dumb_fsal_up_invalidate_step1(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_invalidate()");

  /* Lock the entry */
  cache_inode_invalidate(&pevdata->event_context.fsal_data,
                         &cache_status,
                         CACHE_INODE_INVALIDATE_CLEARBITS);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t dumb_fsal_up_invalidate_step2(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_invalidate()");

  /* Lock the entry */
  cache_inode_invalidate(&pevdata->event_context.fsal_data,
                         &cache_status,
                         CACHE_INODE_INVALIDATE_CLOSE);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

fsal_status_t dumb_fsal_up_update(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: Entered dumb_fsal_up_update");
  if ((pevdata->type.update.upu_flags & FSAL_UP_NLINK) &&
      (pevdata->type.update.upu_stat_buf.st_nlink == 0) )
    {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: nlink has become zero; close fds");
      cache_inode_invalidate(&pevdata->event_context.fsal_data,
                             &cache_status,
                             (CACHE_INODE_INVALIDATE_CLEARBITS |
                              CACHE_INODE_INVALIDATE_CLOSE));
    }
  else
    cache_inode_invalidate(&pevdata->event_context.fsal_data,
                           &cache_status,
                           CACHE_INODE_INVALIDATE_CLEARBITS);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

#define INVALIDATE_STUB {                     \
    return dumb_fsal_up_invalidate_step1(pevdata);  \
  } while(0);

fsal_status_t dumb_fsal_up_create(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_unlink(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_rename(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_commit(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_write(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_link(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_lock_grant(fsal_up_event_data_t * pevdata)
{
#ifdef _USE_BLOCKING_LOCKS
  cache_inode_status_t   cache_status;
  cache_entry_t        * pentry = NULL;
  fsal_attrib_list_t     attr;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_get()");
  pentry = cache_inode_get(&pevdata->event_context.fsal_data,
                           &attr, NULL, NULL,
                           &cache_status);
  if(pentry == NULL)
    {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: cache inode get failed.");
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  LogDebug(COMPONENT_FSAL_UP,
           "FSAL_UP_DUMB: Lock Grant found entry %p",
           pentry);

  grant_blocked_lock_upcall(pentry,
                            pevdata->type.lock_grant.lock_owner,
                            &pevdata->type.lock_grant.lock_param);


  if(pentry)
    cache_inode_put(pentry);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
#else
  INVALIDATE_STUB;
#endif
}

fsal_status_t dumb_fsal_up_lock_avail(fsal_up_event_data_t * pevdata)
{
#ifdef _USE_BLOCKING_LOCKS
  cache_inode_status_t   cache_status;
  cache_entry_t        * pentry = NULL;
  fsal_attrib_list_t     attr;

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_get()");
  pentry = cache_inode_get(&pevdata->event_context.fsal_data,
                           &attr, NULL, NULL, &cache_status);
  if(pentry == NULL)
    {
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: cache inode get failed.");
      /* Not an error. Expecting some nodes will not have it in cache in
       * a cluster. */
      ReturnCode(ERR_FSAL_NO_ERROR, 0);
    }

  LogFullDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: Lock Available found entry %p",
               pentry);

  available_blocked_lock_upcall(pentry,
                                pevdata->type.lock_grant.lock_owner,
                                &pevdata->type.lock_grant.lock_param);

  if(pentry)
    cache_inode_put(pentry);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
#else
  INVALIDATE_STUB;
#endif
}

fsal_status_t dumb_fsal_up_open(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_close(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_setattr(fsal_up_event_data_t * pevdata)
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

fsal_up_event_functions_t *get_fsal_up_dumb_functions()
{
  return &dumb_event_func;
}
