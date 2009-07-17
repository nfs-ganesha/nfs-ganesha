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
 * \file    cache_inode_commit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:27 $
 * \version $Revision: 1.20 $
 * \brief   Commits an IO on a REGULAR_FILE.
 *
 * cache_inode_commit.c : Commits an IO on a REGULAR_FILE.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */


#include "fsal.h"

#include "LRU_List.h"
#include "log_functions.h"
#include "HashData.h"
#include "HashTable.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "stuff_alloc.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

/**
 *
 * cache_inode_commit: commits a write operation on unstable storage
 *
 * Reads/Writes through the cache layer.
 *
 * @param pentry [IN] entry in cache inode layer whose content is to be accessed.
 * @param read_or_write [IN] a flag of type cache_content_io_direction_t to tell if a read or write is to be done. 
 * @param seek_descriptor [IN] absolute position (in the FSAL file) where the IO will be done.
 * @param buffer_size [IN] size of the buffer pointed by parameter 'buffer'. 
 * @param pio_size [OUT] the size of the io that was successfully made.
 * @param pfsal_attr [OUT] the FSAL attributes after the operation.
 * @param buffer write:[IN] read:[OUT] the buffer for the data.
 * @param ht [INOUT] the hashtable used for managing the cache. 
 * @param pclient [IN]  ressource allocated by the client for the nfs management.
 * @param pcontext [IN] fsal context for the operation.
 * @pstatus [OUT] returned status.
 *
 * @return CACHE_CONTENT_SUCCESS is successful .
 *
 * @todo: BUGAZEOMEU; gestion de la taille du fichier a prendre en compte.
 *
 */

cache_inode_status_t cache_inode_commit( cache_entry_t              * pentry, 
                                         uint64_t                     offset,
                                         fsal_size_t                  count,  
                                         fsal_attrib_list_t         * pfsal_attr,
                                         hash_table_t               * ht,  
                                         cache_inode_client_t       * pclient, 
                                         fsal_op_context_t          * pcontext, 
                                         cache_inode_status_t       * pstatus )
{
  fsal_seek_t    seek_descriptor ;
  fsal_size_t    size_io_done ;  
  fsal_boolean_t eof ;


  if( pentry->object.file.unstable_data.buffer == NULL )
   {
     *pstatus = CACHE_INODE_SUCCESS ;
     return *pstatus ;
   }

  if( count == 0 || count ==  0xFFFFFFFFL )
   {
     /* Count = 0 means "flush all data to permanent storage */
     seek_descriptor.offset = pentry->object.file.unstable_data.offset ;
     seek_descriptor.whence = FSAL_SEEK_SET ;


    if( cache_inode_rdwr( pentry,
		          CACHE_INODE_WRITE,
			  &seek_descriptor,
			  pentry->object.file.unstable_data.length,
			  &size_io_done,
			  pfsal_attr,
			  pentry->object.file.unstable_data.buffer,
			  &eof,
			  ht,
			  pclient,
			  pcontext,
			  TRUE,
			  pstatus ) != CACHE_INODE_SUCCESS )
	return *pstatus ;
     
    P_w( &pentry->lock ) ; 

    Mem_Free( pentry->object.file.unstable_data.buffer ) ;
    pentry->object.file.unstable_data.buffer = NULL ;	      

    V_w( &pentry->lock ) ; 

   }
  else
   {
     if( offset < pentry->object.file.unstable_data.offset )
      {
         *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
         return *pstatus ;
      }

     seek_descriptor.offset = offset ;
     seek_descriptor.whence = FSAL_SEEK_SET ;


     return cache_inode_rdwr( pentry,
			      CACHE_INODE_WRITE,
			      &seek_descriptor,
			      count,
			      &size_io_done,
			      pfsal_attr,
			      (char *)(pentry->object.file.unstable_data.buffer + offset - pentry->object.file.unstable_data.offset ),
			      &eof,
			      ht,
			      pclient,
			      pcontext,
			      TRUE,
			      pstatus ) ;	  
   }

  /* Regulat exit */
  *pstatus = CACHE_INODE_SUCCESS ;
  return *pstatus ;
} /* cache_inode_commit */


 
