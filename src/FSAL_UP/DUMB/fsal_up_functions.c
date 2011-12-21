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
#include "stuff_alloc.h"
#include "log_macros.h"
#include "fsal.h"
#include "cache_inode.h"
#include "HashTable.h"
#include "fsal_up.h"

/* Set the FSAL UP functions that will be used to process events.
 * This is called DUMB_FSAL_UP because it only invalidates cache inode
 * entires ... inode entries are not updated or refreshed through this
 * interface. */

fsal_status_t dumb_fsal_up_invalidate(fsal_up_event_data_t * pevdata)
{
  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  fsal_attrib_list_t attr;

  /* Avoid dir cont check in cache_inode_get() */
  pevdata->event_context.fsal_data.cookie = DIR_START;
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: calling cache_inode_get()");
  pentry = cache_inode_get(&pevdata->event_context.fsal_data,
                           &attr, pevdata->event_context.ht, NULL, NULL,
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
          "FSAL_UP_DUMB: Invalidate cache found entry %p type %u",
          pentry, pentry->internal_md.type);

  /* Lock the entry */
  P_w(&pentry->lock);
      LogDebug(COMPONENT_FSAL_UP,
               "FSAL_UP_DUMB: setting state to STALE");
  pentry->internal_md.valid_state = STALE;
  if(pentry->internal_md.type == DIRECTORY)
    {
      pentry->object.dir.has_been_readdir = CACHE_INODE_RENEW_NEEDED;
      LogDebug(COMPONENT_FSAL_CB,
              "FSAL_CB_DUMB: Invalidate reset directory.");
    }
  V_w(&pentry->lock);

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}

#define INVALIDATE_STUB {                     \
    return dumb_fsal_up_invalidate(pevdata);  \
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

fsal_status_t dumb_fsal_up_lock(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
}

fsal_status_t dumb_fsal_up_locku(fsal_up_event_data_t * pevdata)
{
  INVALIDATE_STUB;
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
  .fsal_up_lock = dumb_fsal_up_lock,
  .fsal_up_locku = dumb_fsal_up_locku,
  .fsal_up_open = dumb_fsal_up_open,
  .fsal_up_close = dumb_fsal_up_close,
  .fsal_up_setattr = dumb_fsal_up_setattr,
  .fsal_up_invalidate = dumb_fsal_up_invalidate
};

fsal_up_event_functions_t *get_fsal_up_dumb_functions()
{
  return &dumb_event_func;
}
