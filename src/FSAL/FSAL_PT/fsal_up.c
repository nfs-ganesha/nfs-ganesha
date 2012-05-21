// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_up.c
// Description: FSAL upcall operation implementations
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------

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
 *
 * \file    fsal_up.c
 * \brief   FSAL Upcall Interface
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_up.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <sys/time.h>

#ifdef _USE_FSAL_UP

fsal_status_t PTFSAL_UP_Init( fsal_up_event_bus_parameter_t * pebparam,      /* IN */
                                   fsal_up_event_bus_context_t * pupebcontext     /* OUT */)
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_init);
}

fsal_status_t PTFSAL_UP_AddFilter( fsal_up_event_bus_filter_t * pupebfilter,  /* IN */
                                        fsal_up_event_bus_context_t * pupebcontext /* INOUT */ )
{
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_addfilter);
}

fsal_status_t PTFSAL_UP_GetEvents( fsal_up_event_t ** pevents,                  /* OUT */
                                     fsal_count_t * event_nb,                     /* IN */
                                     fsal_time_t timeout,                         /* IN */
                                     fsal_count_t * peventfound,                  /* OUT */
                                     fsal_up_event_bus_context_t * pupebcontext   /* IN */ )
{
  struct stat64 buf;
  struct glock fl;
  struct callback_arg callback;
  cache_inode_fsal_data_t pfsal_data;
  fsal_handle_t *tmp_handlep;
  ptfsal_handle_t *phandle;
  int reason = 0;

  tmp_handlep = malloc(sizeof(fsal_handle_t));
  memset((char *)tmp_handlep, 0, sizeof(fsal_handle_t)) ;

  memset((char *)&pfsal_data, 0, sizeof(pfsal_data));
  pfsal_data.fh_desc.start = (caddr_t)tmp_handlep;
  pfsal_data.fh_desc.len = sizeof(*tmp_handlep);
  phandle = (ptfsal_handle_t *) pfsal_data.fh_desc.start;

  if (pupebcontext == NULL || event_nb == NULL)
    {
      LogDebug(COMPONENT_FSAL, "Error: PTFSAL_UP_GetEvents() received"
               " unexpectedly NULL arguments.");
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_UP_getevents);
    }

  memset(&pfsal_data, 0, sizeof(cache_inode_fsal_data_t));
  //code is too different.  Compare to:
  //https://github.com/phdeniel/nfs-ganesha/commit/66e19fe4e96f4b068b9ab63572215706e74109c0#L1R315

  ptfsal_export_context_t *p_export_context =
    (ptfsal_export_context_t *)&pupebcontext->FS_export_context;

  //pfsal_data.cookie = 0;
  phandle->data.handle.handle_size = OPENHANDLE_HANDLE_LEN;
  phandle->data.handle.handle_key_size = 0;
  callback.mountdirfd = p_export_context->mount_root_fd;
  callback.handle = (struct ptfs_file_handle *) &phandle->data.handle;
  callback.reason = &reason;
  callback.buf = &buf;
  callback.fl = &fl;

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_UP_getevents);
}

#endif /* _USE_FSAL_UP */
