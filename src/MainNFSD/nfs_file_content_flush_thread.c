/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
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
 * \file    nfs_file_content_gc_thread.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   The file that contain the 'file_content_gc_thread' routine for the nfsd.
 *
 * nfs_file_content_gc_thread.c : The file that contain the 'admin_thread' routine for the nfsd.
 *
 *
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
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"
#include "nfs_init.h"

/* Structures from another module */
extern nfs_start_info_t nfs_start_info;

/**
 * nfs_file_content_flush_thread: thead used for RPC dispatching.
 *
 * @param flush_data_arg contains the index of the flush thread
 *                       and it must be filled with flush stats.
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */

fsal_op_context_t fsal_context[NB_MAX_FLUSHER_THREAD];

void *nfs_file_content_flush_thread(void *flush_data_arg)
{
  fsal_status_t fsal_status;
  char cache_sub_dir[MAXPATHLEN];
  cache_content_status_t content_status;
#ifndef _NO_BUDDY_SYSTEM
  int rc = 0;
#endif
  nfs_flush_thread_data_t *p_flush_data = NULL;
  exportlist_t *pexport;
  char function_name[MAXNAMLEN];
#ifdef _USE_XFS
  xfsfsal_export_context_t export_context ;
  fsal_path_t export_path ;
#endif

  p_flush_data = (nfs_flush_thread_data_t *) flush_data_arg;

  sprintf(function_name, "nfs_file_content_flush_thread #%u", p_flush_data->thread_index);

  SetNameFunction(function_name);

  LogDebug(COMPONENT_MAIN,
           "NFS DATACACHE FLUSHER THREAD #%u : Starting",
           p_flush_data->thread_index);

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_MAIN,
               "NFS DATACACHE FLUSHER THREAD #%u : Memory manager could not be initialized",
               p_flush_data->thread_index);
    }
  LogInfo(COMPONENT_MAIN,
          "NFS DATACACHE FLUSHER THREAD #%u : Memory manager successfully initialized",
          p_flush_data->thread_index);
#endif

  /* Initialisation of credential for current thread */
  LogInfo(COMPONENT_MAIN,
          "NFS DATACACHE FLUSHER THREAD #%u : Initialization of thread's credential",
          p_flush_data->thread_index);
  if(FSAL_IS_ERROR(FSAL_InitClientContext(&(fsal_context[p_flush_data->thread_index]))))
    {
      /* Failed init */
      LogFatal(COMPONENT_MAIN,
               "NFS DATACACHE FLUSHER THREAD #%u : Error initializing thread's credential",
               p_flush_data->thread_index);
    }

  /* check for each pexport entry to get those who are data cached */
  for(pexport = nfs_param.pexportlist; pexport != NULL; pexport = pexport->next)
    {

      if(pexport->options & EXPORT_OPTION_USE_DATACACHE)
        {
          LogEvent(COMPONENT_MAIN,
                   "Starting flush on Export Entry #%u",
                   pexport->id);

          fsal_status =
              FSAL_GetClientContext(&(fsal_context[p_flush_data->thread_index]),
                                    &pexport->FS_export_context, 0, -1, NULL, 0);

          if(FSAL_IS_ERROR(fsal_status))
            LogError(COMPONENT_MAIN, ERR_FSAL, fsal_status.major, fsal_status.minor);

#ifdef _USE_XFS
	  /* This is badly, badly broken. rework export struct defs and api because
	   * this breaks dynamic multi-fsal support */

          /* Export Context is required for FSAL_XFS to work properly (it set the XFS fshandle) */
          fsal_status = FSAL_str2path( pexport->dirname, strlen( pexport->dirname )   , &export_path ) ;
          if(FSAL_IS_ERROR(fsal_status))
            LogError(COMPONENT_MAIN, ERR_FSAL, fsal_status.major, fsal_status.minor);

          strncpy( export_context.mount_point,
		   pexport->dirname, FSAL_MAX_PATH_LEN -1 ) ;
          fsal_status = FSAL_BuildExportContext( &export_context, &export_path, NULL ) ;
          if(FSAL_IS_ERROR(fsal_status))
            LogError(COMPONENT_MAIN, ERR_FSAL, fsal_status.major, fsal_status.minor);
#endif
          /* XXX: all entries are put in the same export_id path with id=0 */
          snprintf(cache_sub_dir, MAXPATHLEN, "%s/export_id=%d",
                   nfs_param.cache_layers_param.cache_content_client_param.cache_dir, 0);

          if(cache_content_emergency_flush(cache_sub_dir,
                                           nfs_start_info.flush_behaviour,
                                           nfs_start_info.lw_mark_trigger,
                                           nfs_param.cache_layers_param.dcgcpol.emergency_grace_delay,
                                           p_flush_data->thread_index,
                                           nfs_start_info.nb_flush_threads,
                                           &p_flush_data->nb_flushed,
                                           &p_flush_data->nb_too_young,
                                           &p_flush_data->nb_errors,
                                           &p_flush_data->nb_orphans,
                                           &(fsal_context[p_flush_data->thread_index]),
                                           &content_status) != CACHE_CONTENT_SUCCESS)
            {
              LogCrit(COMPONENT_MAIN,
                      "Flush on Export Entry #%u failed", pexport->id);
            }
          else
            {
              LogEvent(COMPONENT_MAIN,
                       "Flush on Export Entry #%u is ok", pexport->id);

              /* XXX: for now, all cached data are put in the export directory (with export_id=0)
               * Thus, we don't need to have a flush for each export_id.
               * Once a flush is done for one export, we can stop.
               */
              break;
            }

        }
      else
        LogEvent(COMPONENT_MAIN,
                 "Export Entry #%u is not data cached, skipping..",
                 pexport->id);
    }

  /* Tell the admin that flush is done */
  LogEvent(COMPONENT_MAIN,
           "NFS DATACACHE FLUSHER THREAD #%d : flush of the data cache is done for this thread. Closing thread",
           p_flush_data->thread_index);

  return NULL;
}                               /* nfs_file_content_flush_thread */
