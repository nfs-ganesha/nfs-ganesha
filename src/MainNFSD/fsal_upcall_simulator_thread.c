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
 * \file    fsal_upcall_simulator_thread.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   The file that contain the 'fsal_upcall_simulator_thread' routine for the nfsd.
 *
 * fsal_upcall_simulator_thread.c : The file that contain the 'fsal_upcall_simulator_thread' routine for the nfsd (and all
 * the related stuff).
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
#include "nlm4.h"
#include "rquota.h"
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

#ifdef _USE_PNFS
#include "pnfs.h"
#include "pnfs_service.h"
#endif

nfs_worker_data_t mydata ;

static cache_entry_t * choose_pentry( hash_table_t * ht)
{
  struct rbt_node *it;
  struct rbt_head *tete_rbt;
  cache_entry_t * pentry = NULL ;
  unsigned int i = 0;
  hash_data_t *pdata = NULL;


  unsigned int counter = 0 ;

  /* Sanity check */
  if(ht == NULL)
    return NULL ;

  for(i = 0; i < ht->parameter.index_size; i++)
    {
      

      tete_rbt = &((ht->array_rbt)[i]);
      RBT_LOOP(tete_rbt, it)
      {
        counter += 1 ;
        pdata = (hash_data_t *) it->rbt_opaq;
        pentry = (cache_entry_t *) (pdata->buffval.pdata) ;

        /* No file invalidation for the moment (file can handle state) */
        if( counter >= 10 ) 
         return pentry ;

        RBT_INCREMENT(it);
      }
    }
  return NULL ;
} /* choose_pentry */



void *upcall_simulator_thread(void *UnusedArgument)
{
  // cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  char thr_name[32];
  int rc = 0 ;
  cache_entry_t * pentry = NULL ;
  fsal_handle_t * pfsal_handle_invalidate = NULL ;
  fsal_attrib_list_t attr ;
  cache_inode_status_t cache_status ;

#ifdef _USE_MFSL
  fsal_status_t fsal_status ;
#endif

#ifdef _USE_SHARED_FSAL 
  unsigned int i = 0 ;
  unsigned int fsalid = 0 ;
#endif

  snprintf(thr_name, sizeof(thr_name), "Upcall Simulator Thread" );
  SetNameFunction(thr_name);

  LogFullDebug(COMPONENT_CACHE_INODE, "Starting" ) ;

  /* Initialisation of the Buddy Malloc */
#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_CACHE_INODE,
               "Memory manager could not be initialized");
    }
  LogFullDebug(COMPONENT_CACHE_INODE,
               "Memory manager successfully initialized");
#endif

  LogDebug(COMPONENT_CACHE_INODE, "UPCALL SIMULATOR: my pthread id is %p",(caddr_t) pthread_self() ) ;

#ifdef _USE_SHARED_FSAL
  for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
   {
      fsalid =  nfs_param.loaded_fsal[i] ;

      FSAL_SetId( fsalid ) ;

      if(FSAL_IS_ERROR(FSAL_InitClientContext(&(mydata.thread_fsal_context[fsalid]))))
       {
         /* Failed init */
         LogMajor(COMPONENT_CACHE_INODE,
                  "NFS  WORKER #%lu: Error initializing thread's credential for FSAL %s",
                 worker_index, FSAL_fsalid2name( fsalid ) );
         exit(1);
       }
   } /* for */
#else
  if(FSAL_IS_ERROR(FSAL_InitClientContext(&mydata.thread_fsal_context)))
    {
      /* Failed init */
      LogFatal(COMPONENT_CACHE_INODE,
               "Error initializing thread's credential");
    }
#endif /* _USE_SHARED_FSAL */

  /* Init the Cache inode client for this worker */
  if(cache_inode_client_init(&mydata.cache_inode_client,
                             nfs_param.cache_layers_param.cache_inode_client_param,
                             SMALL_CLIENT_INDEX, &mydata))
    {
      /* Failed init */
      LogFatal(COMPONENT_CACHE_INODE,
               "Cache Inode client could not be initialized");
    }
  LogFullDebug(COMPONENT_CACHE_INODE,
               "Cache Inode client successfully initialized");

#ifdef _USE_MFSL

#ifdef _USE_SHARED_FSAL
#error "For the moment, no MFSL are supported with dynamic FSALs"
#else
  if(FSAL_IS_ERROR(MFSL_GetContext(&mydata.cache_inode_client.mfsl_context, (&(mydata.thread_fsal_context) ) ) ) ) 
#endif
    {
      /* Failed init */
      LogFatal(COMPONENT_CACHE_INODE, "Error initing MFSL");
    }
#endif

  mydata.ht = workers_data[0].ht ;

  LogFullDebug(COMPONENT_CACHE_INODE,
               "Cache Content client successfully initialized");

  LogInfo(COMPONENT_CACHE_INODE, "Upcall Simulator successfully initialized");

  /* Thread's infinite loop */
  while(1)
    {
        sleep( 1 ) ;
        if( ( pentry = choose_pentry( mydata.ht) ) != NULL )
         {
           LogCrit( COMPONENT_CACHE_INODE, "About to invalidate entry %p type=%u", pentry, pentry->internal_md.type ) ;

           if((pfsal_handle_invalidate =
               cache_inode_get_fsal_handle(pentry, &cache_status)) == NULL)
            {
              LogCrit( COMPONENT_CACHE_INODE, "Unable to get handle for entry %p to be invalidated", pentry ) ;
            }
           else if( cache_inode_invalidate( pfsal_handle_invalidate,
                                       &attr,
                                       mydata.ht,
                                       &mydata.cache_inode_client,
                                       &cache_status ) != CACHE_INODE_SUCCESS )
             {
                   LogCrit( COMPONENT_CACHE_INODE, "Could not invalidate entry %p, status=%u", pentry, cache_status ) ;
             }
           else
                LogInfo( COMPONENT_CACHE_INODE, "Entry %p has been invalidated", pentry ) ;
         }                           /* while( 1 ) */
    } /* while( 1 ) */

  return NULL;
}                               /* worker_thread */
