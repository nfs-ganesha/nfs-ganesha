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
 * \file    cache_inode_statfs.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:28 $
 * \version $Revision: 1.13 $
 * \brief   Get and eventually cache an entry.
 *
 * cache_inode_statfs.c : Get static and dynamic info on the cache inode layer. 
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif /* _SOLARIS */


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
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

/*
 * ASSUMPTION: DIR_CONT entries are always garbabbaged before their related DIR_BEGINNG 
 */
cache_inode_status_t cache_inode_statfs( cache_entry_t        * pentry,
                                         fsal_staticfsinfo_t  * pstaticinfo, 
                                         fsal_dynamicfsinfo_t * pdynamicinfo,
                                         fsal_op_context_t          * pcontext,
                                         cache_inode_status_t * pstatus ) 
{
  fsal_handle_t           * pfsal_handle ;
  fsal_status_t             fsal_status ;
  
 
  
  /* Sanity check */
  if( !pentry || !pstaticinfo || !pdynamicinfo || !pstatus ) /* pcontext is not used: it is not tested */
    {
      *pstatus = CACHE_INODE_INVALID_ARGUMENT ;
      return *pstatus ;
    }
  
  /* Default return value */
  *pstatus = CACHE_INODE_SUCCESS ;

  /* Get the handle for this entry */
  if( ( pfsal_handle = cache_inode_get_fsal_handle( pentry, pstatus ) ) == NULL )
    return *pstatus ;

  /* Get FSAL to get static and dynamic info */
  if( FSAL_IS_ERROR( ( fsal_status = FSAL_static_fsinfo( pfsal_handle, pcontext, pstaticinfo ) ) ) )
    {
      *pstatus = cache_inode_error_convert( fsal_status ) ;
      return *pstatus ;
    }
  
  if( FSAL_IS_ERROR( ( fsal_status = FSAL_dynamic_fsinfo( pfsal_handle, pcontext, pdynamicinfo ) ) ) )
    {
      *pstatus = cache_inode_error_convert( fsal_status ) ;
      return *pstatus ;
    }
  
#ifdef  _DEBUG_CACHE_INODE
   printf( "-- cache_inode_statfs --> pdynamicinfo->total_bytes = %llu pdynamicinfo->free_bytes = %llu pdynamicinfo->avail_bytes = %llu\n", 
                  pdynamicinfo->total_bytes,  pdynamicinfo->free_bytes,  pdynamicinfo->avail_bytes ) ;
   
   printf( "-- cache_inode_statfs --> dynamicinfo.total_files = %llu dynamicinfo.free_files = %llu dynamicinfo.avail_files = %llu\n", 
           pdynamicinfo->total_files,  pdynamicinfo->free_files,  pdynamicinfo->avail_files ) ;
#endif
  return CACHE_INODE_SUCCESS ;
} /* cache_inode_get */


