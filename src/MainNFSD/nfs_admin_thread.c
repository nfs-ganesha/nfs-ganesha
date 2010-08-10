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
 * \file    nfs_admin_thread.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/31 10:08:04 $
 * \version $Revision: 1.6 $
 * \brief   The file that contain the 'admin_thread' routine for the nfsd.
 *
 * nfs_admin_thread.c : The file that contain the 'admin_thread' routine for the nfsd.
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
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"

extern nfs_parameter_t nfs_param;
nfs_admin_data_t *pmydata;

int nfs_Init_admin_data(nfs_admin_data_t *pdata)
{
  if(pthread_mutex_init(&(pdata->mutex_admin_condvar), NULL) != 0)
    return -1;

  if(pthread_cond_init(&(pdata->admin_condvar), NULL) != 0)
    return -1;

  pdata->reload_exports = FALSE;

  return 0;
}                               /* nfs_Init_admin_data */

void admin_replace_exports()
{
  P(pmydata->mutex_admin_condvar);
  pmydata->reload_exports = TRUE;
  if(pthread_cond_signal(&(pmydata->admin_condvar)) == -1)
      LogCrit(COMPONENT_MAIN, "admin_replace_exports - admin cond signal failed , errno = %d", errno);
  V(pmydata->mutex_admin_condvar);
}

static int wake_workers_for_export_reload()
{
  int i;
  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      pmydata->workers_data[i].reparse_exports_in_progress = FALSE;
      if(pthread_cond_signal(&(pmydata->workers_data[i].export_condvar)) == -1)
	{
	  LogCrit(COMPONENT_MAIN, "replace_exports: Export cond signal failed for thr#%d , errno = %d", i, errno);
	  return -1;
	}
    }
}

static int pause_workers_for_export_reload()
{
  int all_blocked,i;

  /* Pause worker threads */
  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      pmydata->workers_data[i].reparse_exports_in_progress = TRUE;

      /* If threads are blocked on the request queue, wake them up
       * so they are blocked on the exports list replacement. */
      if(pthread_cond_signal(&(pmydata->workers_data[i].req_condvar)) == -1)
	{
	  LogCrit(COMPONENT_MAIN, "replace_exports: Request cond signal failed for thr#%d , errno = %d", i, errno);
	  wake_workers_for_export_reload();
	  return -1;
	}
  }

  /* Wait for all worker threads to block */
  while(1)
    {
      all_blocked = 1;
      for(i = 0; i < nfs_param.core_param.nb_worker; i++)
	if (pmydata->workers_data[i].waiting_for_exports == FALSE)
	  all_blocked = 0;
      if (all_blocked)
	break;
    }

  return 1;
}

/* Skips deleting first entry of export list.
 * This function also frees the fd in the head node of the list. */
int RemoveAllExportsExceptHead(exportlist_t * pexportlist)
{
  exportlist_t *pcurrent;

  pcurrent = pexportlist->next;
  while(pcurrent != NULL)
    {
      /* Leave the head so that the list may be replaced later without
       * changing the reference pointer in worker threads. */
      CleanUpExportContext(&pcurrent->FS_export_context);              
      
      if (pcurrent == pexportlist)
	break;
      

      pexportlist->next = RemoveExportEntry(pcurrent);
      pcurrent = pexportlist->next;
    }

  return 1;   /* Success */
}

/* Skips deleting first entry of export list. */
int rebuild_export_list(char *config_file)
{
  int status = 0;
  exportlist_t * temp_pexportlist;
  config_file_t config_struct;

  /* If no configuration file is given, then the caller must want to reparse the
   * configuration file from startup. */
  if (config_file == NULL)
    {
      LogCrit(COMPONENT_MAIN, "Error: No configuration file was specified for reloading exports.");
      return -1;
    }

  /* Attempt to parse the new configuration file */
  config_struct = config_ParseFile(config_file);
  if(!config_struct)
    {
      LogCrit(COMPONENT_MAIN, "rebuild_export_list: Error while parsing new configuration file %s: %s",
              config_file, config_GetErrorMsg());
      return -1;
    }  

  /* Create the new exports list */
  status = ReadExports(config_struct, &temp_pexportlist);
  if(status < 0)
    {
      LogCrit(COMPONENT_MAIN, "rebuild_export_list: Error while parsing export entries");
      return status;
    }
  else if(status == 0)
    {
      LogCrit(COMPONENT_MAIN, "rebuild_export_list: No export entries found in configuration file !!!");
      return status;
    }

  /* At least one worker thread should exist. Each worker thread has a pointer to
   * the same hash table. */  
  if( nfs_export_create_root_entry(temp_pexportlist, pmydata->ht) != TRUE)
    {
      LogCrit(COMPONENT_MAIN, "replace_exports: Error initializing Cache Inode root entries");
      return -1;
    }

  if (!pause_workers_for_export_reload())
    {
      LogCrit(COMPONENT_MAIN, "replace_exports: Error, could not pause all worker threads.");
      return -1;
    }

  /* Now we know that the configuration was parsed successfully.
   * And that worker threads are no longer accessing the export list.
   * Remove all but the first export entry in the exports list. */
  status = RemoveAllExportsExceptHead(nfs_param.pexportlist);
  if (status <= 0)
    LogCrit(COMPONENT_MAIN, "rebuild_export_list: CRITICAL ERROR while removing some export entries.");

  /* Changed the old export list head to the new export list head. 
   * All references to the exports list should be up-to-date now. */
  memcpy(nfs_param.pexportlist, temp_pexportlist, sizeof(exportlist_t));

  /* We no longer need the head that was created for
   * the new list since the export list is built as a linked list. */
  Mem_Free(temp_pexportlist);
  wake_workers_for_export_reload();
  return status; /* 1 if success */
}

void *admin_thread(void *Arg)
{
  pmydata = (nfs_admin_data_t *)Arg;

  int rc = 0;

  SetNameFunction("admin_thr");

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_admin)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogCrit(COMPONENT_MAIN, "ADMIN THREAD: Memory manager could not be initialized, exiting...");
      exit(1);
    }
  LogEvent(COMPONENT_MAIN, "ADMIN THREAD: Memory manager successfully initialized");
#endif

  while(1)
    {
      P(pmydata->mutex_admin_condvar);
      while(pmydata->reload_exports == FALSE)
	    pthread_cond_wait(&(pmydata->admin_condvar), &(pmydata->mutex_admin_condvar));
      pmydata->reload_exports = FALSE;
      V(pmydata->mutex_admin_condvar);

      if (!rebuild_export_list(pmydata->config_path))
	LogCrit(COMPONENT_MAIN, "Error, attempt to reload exports list from config file failed.");
    }

  return NULL;
}                               /* admin_thread */
