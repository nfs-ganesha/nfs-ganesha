// ----------------------------------------------------------------------------
// Copyright IBM Corp. 2012, 2012
// All Rights Reserved
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Filename:    fsal_init.c
// Description: FSAL initialization operations implementation
// Author:      FSI IPC dev team
// ----------------------------------------------------------------------------
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  
 * USA
 *
 * -------------
 */

/**
 *
 * \file    fsal_init.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.20 $
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include "pt_ganesha.h"

pthread_mutex_t g_dir_mutex; // dir handle mutex
pthread_mutex_t g_acl_mutex; // acl handle mutex
pthread_mutex_t g_handle_mutex; // file handle processing mutex
pthread_mutex_t g_parseio_mutex; // only one thread can parse an io at a time
// only one thread can change global transid at a time
pthread_mutex_t g_transid_mutex; 
pthread_mutex_t g_non_io_mutex;
pthread_mutex_t g_close_mutex[FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS];
pthread_mutex_t g_io_mutex;
pthread_mutex_t g_statistics_mutex;
pthread_t g_pthread_closehandle_lisetner;
pthread_t g_pthread_polling_closehandler;

#define COMPONENT_FSAL_PT  5   // COMPONENT_FSAL

int PTFSAL_log(int level, const char * message)
{
  DisplayLogComponentLevel(COMPONENT_FSAL_PT, "FSAL_PT", level,
                           (char *)message);
  return 0;
}

int PTFSAL_log_level_check(int level)
{
  return (unlikely(LogComponents[COMPONENT_FSAL_PT].comp_log_level >= level));
}

static int ptfsal_closeHandle_listener_thread_init(void);
static int ptfsal_polling_closeHandler_thread_init(void);
void *ptfsal_parallel_close_thread(void *args);
void *ptfsal_closeHandle_listener_thread(void *args);
/**
 * FSAL_Init : Initializes the FileSystem Abstraction Layer.
 *
 * \param init_info (input, fsal_parameter_t *) :
 *        Pointer to a structure that contains
 *        all initialization parameters for the FSAL.
 *        Specifically, it contains settings about
 *        the filesystem on which the FSAL is based,
 *        security settings, logging policy and outputs,
 *        and other general FSAL options.
 *
 * \return Major error codes :
 *         ERR_FSAL_NO_ERROR     (initialisation OK)
 *         ERR_FSAL_FAULT        (init_info pointer is null)
 *         ERR_FSAL_SERVERFAULT  (misc FSAL error)
 *         ERR_FSAL_ALREADY_INIT (The FS is already initialized)
 *         ERR_FSAL_BAD_INIT     (FS specific init error,
 *                                minor error code gives the reason
 *                                for this error.)
 *         ERR_FSAL_SEC_INIT     (Security context init error).
 */
fsal_status_t
PTFSAL_Init(fsal_parameter_t * init_info    /* IN */)
{
  fsal_status_t status;
  int i;

  /* sanity check.  */
  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* proceeds FSAL internal initialization */
  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info),
                                     &(init_info->fs_specific_info));
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  /* init mutexes */
  pthread_mutex_init(&g_dir_mutex,NULL);
  pthread_mutex_init(&g_acl_mutex,NULL);
  pthread_mutex_init(&g_handle_mutex,NULL);
  pthread_mutex_init(&g_non_io_mutex,NULL);
  pthread_mutex_init(&g_parseio_mutex,NULL);
  pthread_mutex_init(&g_transid_mutex,NULL);
  pthread_mutex_init(&g_fsi_name_handle_mutex, NULL);
  pthread_mutex_init(&g_io_mutex, NULL);
  pthread_mutex_init(&g_statistics_mutex, NULL);
  g_fsi_name_handle_cache.m_count = 0;
  for (i=0; i<FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS; i++) {
    pthread_mutex_init(&g_close_mutex[i], NULL);
  }
 
  // fsi_ipc_trace_level allows using the level settings differently than
  // Ganesha proper.
  // We map FSI Trace Level to Ganesha debug levels through this array.
  int ipc_ccl_to_component_trc_level_map[FSI_NUM_TRACE_LEVELS];
  ipc_ccl_to_component_trc_level_map[FSI_NO_LEVEL] = NIV_NULL;
  ipc_ccl_to_component_trc_level_map[FSI_FATAL]    = NIV_MAJ;
  ipc_ccl_to_component_trc_level_map[FSI_ERR]      = NIV_CRIT;
  ipc_ccl_to_component_trc_level_map[FSI_WARNING]  = NIV_WARN;
  ipc_ccl_to_component_trc_level_map[FSI_NOTICE]   = NIV_WARN;
  ipc_ccl_to_component_trc_level_map[FSI_STAT]     = NIV_EVENT;
  ipc_ccl_to_component_trc_level_map[FSI_INFO]     = NIV_DEBUG;
  ipc_ccl_to_component_trc_level_map[FSI_DEBUG]    = NIV_DEBUG;

  /* FSI CCL Layer INIT */
  int rc = ccl_init(MULTITHREADED, PTFSAL_log, PTFSAL_log_level_check,
                    ipc_ccl_to_component_trc_level_map);

  if (rc == -1) {
    FSI_TRACE(FSI_ERR, "ccl_init returned rc = -1, errno = %d", errno);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  FSI_TRACE(FSI_NOTICE, "About to call "
            "ptfsal_closeHandle_listener_thread_init");
  if (ptfsal_closeHandle_listener_thread_init() == -1) {
    FSI_TRACE(FSI_ERR, "ptfsal_closeHandle_listener_thread_init "
              "returned rc = -1");
    Return(ERR_FSAL_FAULT, 1, INDEX_FSAL_Init);
  }

  FSI_TRACE(FSI_NOTICE, "About to call "
            "ptfsal_polling_closeHandler_thread_init");
  if (ptfsal_polling_closeHandler_thread_init() == -1) {
    FSI_TRACE(FSI_ERR, "ptfsal_polling_closeHandler_thread_init "
              "returned rc = -1");
    Return(ERR_FSAL_FAULT, 1, INDEX_FSAL_Init);
  }

  /* Regular exit */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);
}

static int
ptfsal_closeHandle_listener_thread_init(void)
{
   pthread_attr_t attr_thr;
   int            rc;

   /* Init the thread in charge of renewing the client id */
   /* Init for thread parameter (mostly for scheduling) */
   pthread_attr_init(&attr_thr);

   rc = pthread_create(&g_pthread_closehandle_lisetner,
                       &attr_thr,
                       ptfsal_closeHandle_listener_thread, (void *)NULL);

   if(rc != 0) {
     FSI_TRACE(FSI_ERR, "Failed to create CloseHandleListener thread rc[%d]",
               rc);
     return -1;
   }

   FSI_TRACE(FSI_NOTICE, "CloseHandle listener thread created successfully");
   return 0;
}

static int
ptfsal_polling_closeHandler_thread_init(void)
{
   pthread_attr_t attr_thr;
   int            rc;

   /* Init the thread in charge of renewing the client id */
   /* Init for thread parameter (mostly for scheduling) */
   pthread_attr_init(&attr_thr);

   rc = pthread_create(&g_pthread_polling_closehandler,
                       &attr_thr,
                       ptfsal_polling_closeHandler_thread, (void *)NULL);

   if(rc != 0) {
     FSI_TRACE(FSI_ERR, "Failed to create polling close handler thread rc[%d]",
               rc);
     return -1;
   }

   FSI_TRACE(FSI_NOTICE, "Polling close handler created successfully");
   return 0;
}

fsal_status_t
PTFSAL_terminate()
{
  int index;
  int closureFailure = 0;
  int minor = 0;
  int major = ERR_FSAL_NO_ERROR;
  int rc;
  pthread_attr_t attr_thr;

  typedef struct {
    int isThreadCreated;
    int handleIdx;
    pthread_t threadContext;
  } CLOSE_THREAD_MAP;
 
  CLOSE_THREAD_MAP parallelCloseThreadMap[FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS];
 
  FSI_TRACE(FSI_NOTICE, "Terminating FSAL_PT");
  rc = ccl_up_mutex_lock(&g_handle_mutex);
  if (rc != 0) {
    FSI_TRACE(FSI_ERR, "Failed to lock handle mutex");
    minor = 1;
    major = posix2fsal_error(EIO);
    ReturnCode(major, minor);
  }

  pthread_attr_init(&attr_thr);  
  memset(&parallelCloseThreadMap[0], 0x00, sizeof (parallelCloseThreadMap));

  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS;
       index++) {
    parallelCloseThreadMap[index].handleIdx = index;
  }

  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < g_fsi_handles.m_count;
       index++) {
    if (g_fsi_handles.m_handle[index].m_hndl_in_use != 0) {
      if ((g_fsi_handles.m_handle[index].m_nfs_state == NFS_CLOSE) ||
          (g_fsi_handles.m_handle[index].m_nfs_state == NFS_OPEN)) {
  
        // ignore error code, just trying to clean up while going down
        // and want to continue trying to close out other open files
        rc = pthread_create(&parallelCloseThreadMap[index].threadContext,
                            &attr_thr,
                            ptfsal_parallel_close_thread, 
                            &parallelCloseThreadMap[index].handleIdx);

        if(rc != 0) {
          FSI_TRACE(FSI_ERR, "Failed to create parallel close thread for handle[%d] rc[%d]",
                    index, rc);
        } else {
          FSI_TRACE(FSI_NOTICE, "Created close thread for handle[%d]",index);
          parallelCloseThreadMap[index].isThreadCreated = 1;
        } 
      }
    }
  }

  ccl_up_mutex_unlock(&g_handle_mutex);
  for (index = FSI_CIFS_RESERVED_STREAMS;
       index < FSI_MAX_STREAMS + FSI_CIFS_RESERVED_STREAMS;
       index++) {
    if (parallelCloseThreadMap[index].isThreadCreated == 1) {
      pthread_join(parallelCloseThreadMap[index].threadContext, NULL);
    }
  }

  FSI_TRACE(FSI_NOTICE, "All parallel close threads have exited");
  
  if (closureFailure) {
    FSI_TRACE(FSI_NOTICE, "Terminating with failure to close file(s)");
  } else {
    FSI_TRACE(FSI_NOTICE, "Successful termination of FSAL_PT");
  }

  /* Terminate Close Handle Listener thread if it's not already dead */
  int signal_send_rc = pthread_kill(g_pthread_closehandle_lisetner, SIGTERM);
  if (signal_send_rc == 0) {
    FSI_TRACE(FSI_NOTICE, "Close Handle Listener thread killed successfully");
  } else if (signal_send_rc == ESRCH) {
    FSI_TRACE(FSI_ERR, "Close Handle Listener already terminated");
  } else if (signal_send_rc) {
    FSI_TRACE(FSI_ERR, "Error from pthread_kill = %d", signal_send_rc);
    minor = 3;
    major = posix2fsal_error(signal_send_rc);
  }

  /* Terminate Polling Close Handle thread */
  signal_send_rc = pthread_kill( g_pthread_polling_closehandler, SIGTERM);
  if (signal_send_rc == 0) {
    FSI_TRACE(FSI_NOTICE, "Polling close handle thread killed successfully");
  } else if (signal_send_rc == ESRCH) {
    FSI_TRACE(FSI_ERR, "Polling close handle thread already terminated");
  } else if (signal_send_rc) {
    FSI_TRACE(FSI_ERR, "Error from pthread_kill = %d", signal_send_rc);
    minor = 4;
    major = posix2fsal_error(signal_send_rc);
  }

  ReturnCode(major, minor);
}


void *ptfsal_parallel_close_thread(void *args)
{

   int index = *((int *) args);
   char threadName[40];

   snprintf(threadName, sizeof(threadName), "PT FSAL ParallelClose %d", index);
   SetNameFunction(threadName);
   FSI_TRACE(FSI_NOTICE, "Closing handle[%d]", index);
   ptfsal_implicit_close_for_nfs(index, CCL_CLOSE_STYLE_FIRE_AND_FORGET);

   pthread_exit(0);

}
