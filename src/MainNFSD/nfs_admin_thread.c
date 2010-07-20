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

extern nfs_parameter_t nfs_param;

int nfs_Init_admin_data(nfs_admin_data_t *pdata)
{
  if(pthread_mutex_init(&(pdata->mutex_admin_condvar), NULL) != 0)
    return -1;

  if(pthread_cond_init(&(pdata->admin_condvar), NULL) != 0)
    return -1;

  pdata->reload_exports = FALSE;

  return 0;
}                               /* nfs_Init_admin_data */

void *admin_thread(void *Arg)
{
  nfs_admin_data_t *pmydata = (nfs_admin_data_t *)Arg;

  int rc = 0;

  SetNameFunction("admin_thr");

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_admin)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      DisplayLog("ADMIN THREAD: Memory manager could not be initialized, exiting...");
      exit(1);
    }
  DisplayLogLevel(NIV_EVENT, "ADMIN THREAD: Memory manager successfully initialized");
#endif

  while(1)
    {
      P(pmydata->mutex_admin_condvar);
      while(pmydata->reload_exports == FALSE)
	    pthread_cond_wait(&(pmydata->admin_condvar), &(pmydata->mutex_admin_condvar));
      pmydata->reload_exports = FALSE;
      V(pmydata->mutex_admin_condvar);

      if (!rebuild_export_list(NULL))
	DisplayLog("Error, attempt to reload exports list from config file failed.");
    }

  return NULL;
}                               /* admin_thread */
