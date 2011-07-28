/*
 *
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
 * \file    fsal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:41:01 $
 * \version $Revision: 1.72 $
 * \brief   File System Abstraction Layer interface.
 *
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* fsal_types contains constants and type definitions for FSAL */
#include <errno.h>
#include <pthread.h>
#include "fsal_types.h"
#include "fsal.h"
#include "mfsl_types.h"
#include "mfsl.h"
#include "common_utils.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "stuff_alloc.h"

#ifndef _USE_SWIG

pthread_t mfsl_async_adt_thrid;
pthread_t *mfsl_async_synclet_thrid;

mfsl_synclet_data_t *synclet_data;

mfsl_parameter_t mfsl_param;

/**
 * 
 * MFSL_Init: Inits the MFSL layer.
 *
 * Inits the MFSL layer.
 *
 * @param init_info      [IN] pointer to the MFSL parameters
 *
 * @return a FSAL status
 *
 */

fsal_status_t MFSL_Init(mfsl_parameter_t * init_info    /* IN */
    )
{
  unsigned long i = 0;
  unsigned int rc = 0;
  pthread_attr_t attr_thr;
  LRU_status_t lru_status;

  /* Keep the parameter in mind */
  mfsl_param = *init_info;

  /* Init for thread parameter (mostly for scheduling) */
  pthread_attr_init(&attr_thr);
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);

  /* Allocate the synclet related structure */
  if((mfsl_async_synclet_thrid =
      (pthread_t *) Mem_Alloc(init_info->nb_synclet * sizeof(pthread_t))) == NULL)
    MFSL_return(ERR_FSAL_NOMEM, errno);

  if((synclet_data =
      (mfsl_synclet_data_t *) Mem_Alloc(init_info->nb_synclet *
                                        sizeof(mfsl_synclet_data_t))) == NULL)
    MFSL_return(ERR_FSAL_NOMEM, errno);

  for(i = 0; i < init_info->nb_synclet; i++)
    {
      synclet_data[i].my_index = i;
      if(pthread_cond_init(&synclet_data[i].op_condvar, NULL) != 0)
        MFSL_return(ERR_FSAL_INVAL, 0);

      if(pthread_mutex_init(&synclet_data[i].mutex_op_condvar, NULL) != 0)
        MFSL_return(ERR_FSAL_INVAL, 0);

      if(pthread_mutex_init(&synclet_data[i].mutex_op_lru, NULL) != 0)
        MFSL_return(ERR_FSAL_INVAL, 0);

      if((synclet_data[i].op_lru = LRU_Init(mfsl_param.lru_param, &lru_status)) == NULL)
        MFSL_return(ERR_FSAL_INVAL, 0);

      synclet_data[i].passcounter = 0;

    }                           /* for */

  /* Now start the threads */
  if((rc = pthread_create(&mfsl_async_adt_thrid,
                          &attr_thr,
                          mfsl_async_asynchronous_dispatcher_thread, (void *)NULL)) != 0)
    MFSL_return(ERR_FSAL_SERVERFAULT, -rc);

  for(i = 0; i < init_info->nb_synclet; i++)
    {
      if((rc = pthread_create(&mfsl_async_synclet_thrid[i],
                              &attr_thr, mfsl_async_synclet_thread, (void *)i)) != 0)
        MFSL_return(ERR_FSAL_SERVERFAULT, -rc);
    }

  if(!mfsl_async_hash_init())
    MFSL_return(ERR_FSAL_SERVERFAULT, 0);

  /* Regular Exit */
  MFSL_return(ERR_FSAL_NO_ERROR, 0);
}

#endif                          /* ! _USE_SWIG */
