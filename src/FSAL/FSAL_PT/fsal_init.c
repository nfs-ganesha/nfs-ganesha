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

#include "pt_ganesha.h"

pthread_mutex_t g_dir_mutex; // dir handle mutex
pthread_mutex_t g_acl_mutex; // acl handle mutex
pthread_mutex_t g_handle_mutex; // file handle processing mutex
pthread_mutex_t g_parseio_mutex; // only one thread can parse an io at a time
// only one thread can change global transid at a time
pthread_mutex_t g_transid_mutex; 
pthread_mutex_t g_non_io_mutex;

pthread_t g_pthread_closehandle_lisetner;

/* Following are for FSI_TRACE control and mapping to Ganesha Trace Facility */
int g_ptfsal_debug_level;    
int g_ptfsal_comp_num;
int g_ptfsal_comp_level;

static int ptfsal_closeHandle_listener_thread_init(void);

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

  /* sanity check.  */
  if(!init_info)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);

  /* These are initial values until we get our own Ganesha component */
  g_ptfsal_debug_level = FSI_DEBUG;   // TODO get our own mechanism to set
                                      //  or have these settable by Ganesha
                                      //  debug control
  g_ptfsal_comp_num = (int) COMPONENT_FSAL;  // till we get our own comp
  g_ptfsal_comp_level = (int) NIV_INFO;      // only has meaning if syslog
                                             // used,using g_ptfsal_debug_level
                                             // to control instead, 

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

  g_fsi_name_handle_cache.m_count = 0;
 

  /* FSI CCL Layer INIT */
  int rc = ccl_init(MULTITHREADED);
  if (rc == -1) {
    FSI_TRACE(FSI_ERR, "ccl_init returned rc = -1, errno = %d", errno);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  FSI_TRACE(FSI_NOTICE, "About to call ptfsal_closeHandle_listener_thread_init");
  if (ptfsal_closeHandle_listener_thread_init() == -1) {
    FSI_TRACE(FSI_ERR, "ptfsal_closeHandle_listener_thread_init returned rc = -1");
    Return(ERR_FSAL_FAULT, 1, INDEX_FSAL_Init);
  }

  /* Regular exit */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);
}

// ----------------------------------------------------------------------------
//   CCL Up Call defintions
// ----------------------------------------------------------------------------

int ccl_up_mutex_lock( pthread_mutex_t * pmutex)
{
  FSI_TRACE(FSI_DEBUG, "requesting lock on 0x%lx", (unsigned int) pmutex);
  int rc = pthread_mutex_lock( pmutex);
  if (rc) {
    FSI_TRACE(FSI_ERR, "error code from pthread_mutex_lock = %d", rc);
  } else {
    FSI_TRACE(FSI_DEBUG, "lock 0x%lx acuired", (unsigned int) pmutex);
  }
}

int ccl_up_mutex_unlock( pthread_mutex_t * pmutex)
{
  FSI_TRACE(FSI_DEBUG, "unlocking 0x%lx", (unsigned int) pmutex);
  int rc = pthread_mutex_unlock( pmutex);
  if (rc) {
    FSI_TRACE(FSI_ERR, "error code from pthread_mutex_unlock = %d "
              "probably did not own lock", rc);
  } else {
    FSI_TRACE(FSI_DEBUG, "successfully unlocked 0x%lx ", 
              (unsigned int) pmutex);
  }
}

unsigned long ccl_up_self()
{
   unsigned long my_tid = pthread_self();
   FSI_TRACE(FSI_DEBUG, "tid = %ld ", my_tid);
   return my_tid;
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

