/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
#include <sys/file.h>  /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
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
extern nfs_parameter_t                   nfs_param ;
extern nfs_worker_data_t               * workers_data ;
extern cache_content_client_t            recover_datacache_client ;
extern nfs_start_info_t                  nfs_start_info;
 
/**
 * nfs_file_content_flush_thread: thead used for RPC dispatching.
 *
 * @param flush_data_arg contains the index of the flush thread
 *                       and it must be filled with flush stats.
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */

fsal_op_context_t          fsal_context[NB_MAX_FLUSHER_THREAD] ;

void * nfs_file_content_flush_thread( void * flush_data_arg )
{
  fsal_status_t              fsal_status ; 
  char                       cache_sub_dir[MAXPATHLEN] ;
  cache_content_status_t     content_status ;
  int                        rc           = 0 ;
  nfs_flush_thread_data_t  * p_flush_data = NULL;
  unsigned long              index        = 0 ;
  exportlist_t             * pexport ;
  char                       function_name[MAXNAMLEN] ;

  p_flush_data = (nfs_flush_thread_data_t*)flush_data_arg;

  sprintf( function_name, "nfs_file_content_flush_thread #%u", p_flush_data->thread_index ) ;

  SetNameFunction( function_name ) ;

  DisplayLogLevel( NIV_DEBUG, "NFS DATACACHE FLUSHER THREAD #%u : Starting", p_flush_data->thread_index ) ;

#ifndef _NO_BUDDY_SYSTEM
  if ( ( rc = BuddyInit( &nfs_param.buddy_param_worker )) != BUDDY_SUCCESS )
    {
      /* Failed init */
      DisplayLog( "NFS DATACACHE FLUSHER THREAD #%u : Memory manager could not be initialized, exiting...", p_flush_data->thread_index ) ;
      exit( 1 ) ;
    }
  DisplayLog( "NFS DATACACHE FLUSHER THREAD #%u : Memory manager successfully initialized", p_flush_data->thread_index ) ;
#endif

  /* Initialisation of credential for current thread */
  DisplayLog( "NFS DATACACHE FLUSHER THREAD #%u : Initialization of thread's credential", p_flush_data->thread_index ) ;
  if ( FSAL_IS_ERROR( FSAL_InitClientContext( &(fsal_context[p_flush_data->thread_index])  )) )
    {
      /* Failed init */
      DisplayLog( "NFS DATACACHE FLUSHER THREAD #%u : Error initializing thread's credential", p_flush_data->thread_index ) ;
      exit( 1 ) ;
    }
    
  /* check for each pexport entry to get those who are data cached */
  for( pexport = nfs_param.pexportlist ; pexport != NULL ; pexport = pexport->next ) 
    {

       if( pexport->options & EXPORT_OPTION_USE_DATACACHE )
          {
             DisplayLog( "Starting flush on Export Entry #%u", pexport->id ) ;
  
             fsal_status = FSAL_GetClientContext( &(fsal_context[p_flush_data->thread_index]),  &pexport->FS_export_context, 0, -1 , NULL, 0 );

             if ( FSAL_IS_ERROR( fsal_status ) )
                 DisplayErrorLog( ERR_FSAL, fsal_status.major, fsal_status.minor);
        
             /* XXX: all entries are put in the same export_id path with id=0 */
             snprintf( cache_sub_dir, MAXPATHLEN, "%s/export_id=%d",  
                       nfs_param.cache_layers_param.cache_content_client_param.cache_dir, 0 ) ;
        
             if( cache_content_emergency_flush( cache_sub_dir, 
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
                                                &content_status ) != CACHE_CONTENT_SUCCESS )
              {                
                DisplayLog( "Flush on Export Entry #%u failed", pexport->id ) ;
              }
             else
              {
                DisplayLog( "Flush on Export Entry #%u is ok", pexport->id ) ;
                        
                /* XXX: for now, all cached data are put in the export directory (with export_id=0)
                 * Thus, we don't need to have a flush for each export_id.
                 * Once a flush is done for one export, we can stop.
                 */
                break;                
              }

          }
        else
          DisplayLog( "Export Entry #%u is not data cached, skipping..", pexport->id ) ;
   }

   /* Tell the admin that flush is done */
  DisplayLog( "NFS DATACACHE FLUSHER THREAD #%d : flush of the data cache is done for this thread. Closing thread", p_flush_data->thread_index ) ;

  return  NULL ; 
} /* nfs_file_content_flush_thread */ 
