/*
 *
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

pthread_t   mfsl_async_adt_thrid ;
pthread_t * mfsl_async_synclet_thrid ;

mfsl_synclet_data_t * synclet_data ;

mfsl_parameter_t  mfsl_param ;


/** 
 *  FSAL_Init:
 *  Initializes Filesystem abstraction layer.
 */
fsal_status_t  MFSL_Init(
    mfsl_parameter_t        * init_info         /* IN */
)
{
   unsigned long    i = 0 ;
   unsigned int    rc = 0 ; 
   pthread_attr_t  attr_thr ;
   LRU_status_t    lru_status ;


   /* Keep the parameter in mind */
   mfsl_param = *init_info ;
 
   /* Init for thread parameter (mostly for scheduling) */
   pthread_attr_init( &attr_thr ) ;
   pthread_attr_setscope( &attr_thr, PTHREAD_SCOPE_SYSTEM ) ;
   pthread_attr_setdetachstate( &attr_thr, PTHREAD_CREATE_JOINABLE ) ;
 
   /* Allocate the synclet related structure */
   if( ( mfsl_async_synclet_thrid = (pthread_t *)Mem_Alloc( init_info->nb_synclet  * sizeof( pthread_t ) ) ) == NULL )
  	MFSL_return( ERR_FSAL_NOMEM, errno ) ;
     
   if( ( synclet_data = (mfsl_synclet_data_t *)Mem_Alloc( init_info->nb_synclet  * sizeof( mfsl_synclet_data_t ) ) ) == NULL )
  	MFSL_return( ERR_FSAL_NOMEM, errno ) ;

   for( i = 0 ; i <  init_info->nb_synclet ; i++ )
    {
	synclet_data[i].my_index = i ;
        if( pthread_cond_init( &synclet_data[i].op_condvar, NULL ) != 0 )
	   MFSL_return( ERR_FSAL_INVAL, 0 ) ;

        if( pthread_mutex_init( &synclet_data[i].mutex_op_condvar, NULL ) != 0 )
	   MFSL_return( ERR_FSAL_INVAL, 0 ) ;

        if( pthread_mutex_init( &synclet_data[i].mutex_op_lru, NULL ) != 0 )
	   MFSL_return( ERR_FSAL_INVAL, 0 ) ;

        if( ( synclet_data[i].op_lru= LRU_Init( mfsl_param.lru_param, &lru_status ) ) == NULL )
	   MFSL_return( ERR_FSAL_INVAL, 0 ) ;

    } /* for */

   /* Now start the threads */
   if( ( rc = pthread_create( &mfsl_async_adt_thrid, 
                              &attr_thr, 
			      mfsl_async_asynchronous_dispatcher_thread, 
			      (void *)NULL ) ) != 0 )
      MFSL_return( ERR_FSAL_SERVERFAULT, -rc ) ;

   for( i = 0 ; i <  init_info->nb_synclet ; i++ )
    {
       if( ( rc = pthread_create( &mfsl_async_synclet_thrid[i],
				  &attr_thr, 
				  mfsl_async_synclet_thread,
				 (void *)i ) ) != 0 )
          MFSL_return( ERR_FSAL_SERVERFAULT, -rc ) ;
    }

   if( !mfsl_async_hash_init( ) )
     MFSL_return( ERR_FSAL_SERVERFAULT, 0 ) ;

   /* Regular Exit */
   MFSL_return( ERR_FSAL_NO_ERROR, 0 ) ;
}

#endif /* ! _USE_SWIG */


