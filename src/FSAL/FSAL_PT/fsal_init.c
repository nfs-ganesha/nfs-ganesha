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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
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
pthread_mutex_t g_transid_mutex; // only one thread can change global transid at a time
pthread_mutex_t g_non_io_mutex;

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

  /* proceeds FSAL internal initialization */
  status = fsal_internal_init_global(&(init_info->fsal_info),
                                     &(init_info->fs_common_info),
                                     &(init_info->fs_specific_info));
  if(FSAL_IS_ERROR(status))
    Return(status.major, status.minor, INDEX_FSAL_Init);

  // FSI INIT
  int rc;

  // Get my pid
  g_client_pid = getpid();

  FSI_TRACE(FSI_NOTICE, "entry, pid = %lx", g_client_pid);

  // init mutexes
  pthread_mutex_init(&g_dir_mutex,NULL);
  pthread_mutex_init(&g_acl_mutex,NULL);
  pthread_mutex_init(&g_handle_mutex,NULL);
  pthread_mutex_init(&g_non_io_mutex,NULL);
  pthread_mutex_init(&g_parseio_mutex,NULL);
  pthread_mutex_init(&g_transid_mutex,NULL);
  g_fsi_name_handle_cache.m_count = 0;
  g_client_address[0] = '\0';


  // Attach to existing FSI Shared Memory
  g_shm_id = shmget(FSI_IPC_SHMEM_KEY, 0, 0);
  if (g_shm_id < 0) {
    FSI_TRACE(FSI_FATAL, "error getting shm id %d (errno = %d)",
              FSI_IPC_SHMEM_KEY, errno);
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  } else {
    g_shm_at = shmat(g_shm_id, NULL, 0);
    if ((void *) -1 == g_shm_at) {
      FSI_TRACE(FSI_FATAL, "error shm attach g_shm_id = %d (errno = %d)",
                g_shm_id, errno);
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
    } else {
      FSI_TRACE(FSI_NOTICE, "shm attach ok");

      // Get the PID of Server process
      struct shmid_ds t_shmid_ds;
      int rtn;
      if ((rtn = shmctl(g_shm_id, IPC_STAT, &t_shmid_ds)) == -1) {
        FSI_TRACE(FSI_FATAL, "shmctl failed, errno = %d", errno);
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
      }
      g_server_pid = t_shmid_ds.shm_cpid;
      FSI_TRACE(FSI_NOTICE, "Server pid = %lx", g_server_pid);
    }
  }

  // Get IO and non-IO request, response message queue IDs
  g_io_req_msgq = msgget(FSI_IPC_IO_REQ_Q_KEY, 0);
  if (g_io_req_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting IO Req Msg Q id %d (errno = %d)",
              FSI_IPC_IO_REQ_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  g_io_rsp_msgq = msgget(FSI_IPC_IO_RSP_Q_KEY, 0);
  if (g_io_rsp_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting IO Rsp Msg Q id %d (errno = %d)",
              FSI_IPC_IO_RSP_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  g_non_io_req_msgq = msgget(FSI_IPC_NON_IO_REQ_Q_KEY, 0);
  if (g_non_io_req_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting non IO Req Msg Q id %d (errno = %d)",
              FSI_IPC_NON_IO_REQ_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  g_non_io_rsp_msgq = msgget(FSI_IPC_NON_IO_RSP_Q_KEY, 0);
  if (g_non_io_rsp_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting non IO Rsp Msg Q id %d (errno = %d)",
              FSI_IPC_NON_IO_RSP_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  // Get shmem request, response message queue IDs
  g_shmem_req_msgq = msgget(FSI_IPC_SHMEM_REQ_Q_KEY, 0);
  if (g_shmem_req_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting shmem Req Msg Q id %d (errno = %d)",
              FSI_IPC_SHMEM_REQ_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  g_shmem_rsp_msgq = msgget(FSI_IPC_SHMEM_RSP_Q_KEY, 0);
  if (g_shmem_rsp_msgq < 0) {
    FSI_TRACE(FSI_FATAL, "error getting shmem Rsp Msg Q id %d (errno = %d)",
              FSI_IPC_SHMEM_RSP_Q_KEY, errno);
    // cleanup the attach made earlier, nothing to clean up for the queues
    if ((rc = shmdt(g_shm_at)) == -1) {
      FSI_TRACE(FSI_FATAL, "shmdt returned rc = %d errno = %d", rc, errno);
    }
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_Init);
  }

  // Summarize init
  FSI_TRACE(FSI_NOTICE, "shmem at  %p", g_shm_at);
  FSI_TRACE(FSI_NOTICE, "g_io_req_msgq %d", g_io_req_msgq);
  FSI_TRACE(FSI_NOTICE, "g_io_rsp_msgq %d", g_io_rsp_msgq);
  FSI_TRACE(FSI_NOTICE, "g_non_io_req_msgq %d", g_non_io_req_msgq);
  FSI_TRACE(FSI_NOTICE, "g_non_io_rsp_msgq %d", g_non_io_rsp_msgq);
  FSI_TRACE(FSI_NOTICE, "g_shmem_req_msgq %d", g_shmem_req_msgq);
  FSI_TRACE(FSI_NOTICE, "g_shmem_rsp_msgq %d", g_shmem_rsp_msgq);

  // clear our handles
  memset(&g_fsi_handles, 0, sizeof(g_fsi_handles));

  // skip stdXXX file handles
  g_fsi_handles.m_count = 4;

  // clear our dirHandles
  memset(&g_fsi_dir_handles, 0, sizeof(g_fsi_dir_handles));

  // clear our ACL Handles
  memset(&g_fsi_acl_handles, 0, sizeof(g_fsi_acl_handles));

  // set initial global path this should match the export fsname
  // Ignore snprintf rc, we have enough space for 2
  snprintf(g_chdir_dirpath, 2, "%s", "/");

  ccl_ipc_stats_init();


  /* Regular exit */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_Init);

}
