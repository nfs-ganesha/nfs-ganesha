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
#include "nfs_tcb.h"

/* Structures from another module */

extern char ganesha_exec_path[MAXPATHLEN];
extern char fcc_log_path[MAXPATHLEN];
extern int fcc_debug_level;

/* Use the same structure as the worker (but not all the fields will be used) */
nfs_worker_data_t fcc_gc_data;
static fsal_op_context_t fsal_context;

/* Variable used for forcing flush via a signal */
unsigned int force_flush_by_signal;

int ___cache_content_invalidate_flushed(LRU_entry_t * plru_entry, void *addparam)
{
  cache_content_entry_t *pentry = NULL;
  cache_content_client_t *pclient = NULL;

  pentry = (cache_content_entry_t *) plru_entry->buffdata.pdata;
  pclient = (cache_content_client_t *) addparam;

  if(pentry->local_fs_entry.sync_state != SYNC_OK)
    {
      /* Entry is not to be set invalid */
      return LRU_LIST_DO_NOT_SET_INVALID;
    }

  /* Clean up and set the entry invalid */
  P_w(&pentry->pentry_inode->lock);
  pentry->pentry_inode->object.file.pentry_content = NULL;
  V_w(&pentry->pentry_inode->lock);

  /* Release the entry */
  ReleaseToPool(pentry, &pclient->content_pool);

  /* Retour de la pentry dans le pool */
  return LRU_LIST_SET_INVALID;
}                               /* cache_content_invalidate_flushed */

int file_content_gc_manage_entry(LRU_entry_t * plru_entry, void *addparam)
{
  cache_content_entry_t *pentry = NULL;
  cache_content_status_t cache_content_status;
  exportlist_t *pexport = NULL;

  pentry = (cache_content_entry_t *) plru_entry->buffdata.pdata;
  pexport = (exportlist_t *) addparam;

  if(pentry->local_fs_entry.sync_state == SYNC_OK)
    {
      /* No flush needed */
      return TRUE;
    }
  if((cache_content_status = cache_content_flush(pentry,
                                                 CACHE_CONTENT_FLUSH_AND_DELETE,
                                                 &fcc_gc_data.cache_content_client,
                                                 &fsal_context,
                                                 &cache_content_status)) !=
     CACHE_CONTENT_SUCCESS)
    {
      LogCrit(COMPONENT_MAIN,
              "NFS FILE CONTENT GARBAGE COLLECTION : /!\\ Can't flush %s : error %d",
              pentry->local_fs_entry.cache_path_data, cache_content_status);
    }

  /* Continue with next entry in LRU_Apply_function */
  return TRUE;
}                               /* file_content_gc_manage_entry */

extern  nfs_tcb_t gccb;

void *file_content_gc_thread(void *IndexArg)
{
  char command[2 * MAXPATHLEN];
  exportlist_t *pexport = NULL;
  int is_hw_reached = FALSE;
  int some_flush_to_do = FALSE;
  unsigned long nb_blocks_to_manage;
  char cache_sub_dir[MAXPATHLEN];
  cache_content_status_t cache_content_status;
  FILE *command_stream = NULL;

  char logfile_arg[MAXPATHLEN];
  char *loglevel_arg;

  SetNameFunction("file_content_gc_thread");

  LogEvent(COMPONENT_MAIN,
           "NFS FILE CONTENT GARBAGE COLLECTION : Starting GC thread");

  if(mark_thread_existing(&gccb) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&gccb);
      LogDebug(COMPONENT_DISPATCH,
               "NFS FILE CONTENT GARBAGE COLLECTION Thread exiting before initialization");
      return NULL;
    }

  LogDebug(COMPONENT_MAIN,
           "NFS FILE CONTENT GARBAGE COLLECTION : my pthread id is %p",
           (caddr_t) pthread_self());

  while(1)
    {
      /* Sleep until some work is to be done */
      sleep(nfs_param.cache_layers_param.dcgcpol.run_interval);

      if(gccb.tcb_state != STATE_AWAKE)
        {
          while(1)
            {
              P(gccb.tcb_mutex);
              if(gccb.tcb_state == STATE_AWAKE)
                {
                  V(gccb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&gccb))
                {
                  case THREAD_SM_RECHECK:
                    V(gccb.tcb_mutex);
                    continue;

                  case THREAD_SM_BREAK:
                    V(gccb.tcb_mutex);
                    break;

                  case THREAD_SM_EXIT:
                    V(gccb.tcb_mutex);
                    return NULL;
                }
            }
        }

      LogEvent(COMPONENT_MAIN,
               "NFS FILE CONTENT GARBAGE COLLECTION : processing...");
      for(pexport = nfs_param.pexportlist; pexport != NULL; pexport = pexport->next)
        {
          if(pexport->options & EXPORT_OPTION_USE_DATACACHE)
            {
              snprintf(cache_sub_dir, MAXPATHLEN, "%s/export_id=%d",
                       nfs_param.cache_layers_param.cache_content_client_param.cache_dir,
                       0);

              if((cache_content_status = cache_content_check_threshold(cache_sub_dir,
                                                                       nfs_param.
                                                                       cache_layers_param.
                                                                       dcgcpol.lwmark_df,
                                                                       nfs_param.
                                                                       cache_layers_param.
                                                                       dcgcpol.hwmark_df,
                                                                       &is_hw_reached,
                                                                       &nb_blocks_to_manage))
                 == CACHE_CONTENT_SUCCESS)
                {
                  if(is_hw_reached)
                    {
                      LogEvent(COMPONENT_MAIN,
                               "NFS FILE CONTENT GARBAGE COLLECTION : High Water Mark is  reached, %lu blocks to be removed",
                               nb_blocks_to_manage);
                      some_flush_to_do = TRUE;
                      break;
                    }
                  else {
                    LogEvent(COMPONENT_MAIN,
                             "NFS FILE CONTENT GARBAGE COLLECTION : High Water Mark is not reached");
		  }

                  /* Use signal management */
                  if(force_flush_by_signal == TRUE)
                    {
                      some_flush_to_do = TRUE;
                      break;
                    }
                }
            }
        }                       /* for */

      if (strncmp(fcc_log_path, "/dev/null", 9) == 0)
	switch(LogComponents[COMPONENT_CACHE_INODE_GC].comp_log_type)
	  {
	  case FILELOG:
	    strncpy(logfile_arg, LogComponents[COMPONENT_CACHE_INODE_GC].comp_log_file, MAXPATHLEN);
	    break;
	  case SYSLOG:
	    strncpy(logfile_arg, "SYSLOG", MAXPATHLEN);
	    break;
	  case STDERRLOG:
	    strncpy(logfile_arg, "STDERRLOG", MAXPATHLEN);
	    break;
	  case STDOUTLOG:
	    strncpy(logfile_arg, "STDOUTLOG", MAXPATHLEN);
	    break;
	  default:
	    LogCrit(COMPONENT_MAIN,
	            "Could not figure out the proper -L option for emergency cache flush thread.");
	  }
      else
	strncpy(logfile_arg, fcc_log_path, MAXPATHLEN); /* config variable */

      if(fcc_debug_level != -1) /* config variable */
	loglevel_arg = ReturnLevelInt(fcc_debug_level);
      else
	loglevel_arg = ReturnLevelInt(ReturnLevelComponent(COMPONENT_CACHE_INODE_GC));

      snprintf(command, 2 * MAXPATHLEN, "%s -f %s -N %s -L %s",
	       ganesha_exec_path, config_path, loglevel_arg, logfile_arg);
	       
      if(some_flush_to_do)
        strncat(command, " -P 3", 2 * MAXPATHLEN);      /* Sync and erase */
      else
        strncat(command, " -S 3", 2 * MAXPATHLEN);      /* Sync Only */

      if((command_stream = popen(command, "r")) == NULL)
        LogCrit(COMPONENT_MAIN,
                "NFS FILE CONTENT GARBAGE COLLECTION : /!\\ Cannot lauch command %s",
                command);
      else
        LogEvent(COMPONENT_MAIN,
                 "NFS FILE CONTENT GARBAGE COLLECTION : I launched command %s",
                 command);

      pclose(command_stream);
    }
  tcb_remove(&gccb);
}                               /* file_content_gc_thread */
