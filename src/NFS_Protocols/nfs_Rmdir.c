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
 * \file    nfs_Rmdir.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.13 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Rmdir.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
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

/**
 *
 * nfs_Create: The NFS PROC2 and PROC3 RMDIR
 *
 * Implements the NFS PROC RMDIR function (for V2 and V3).
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs_Rmdir( nfs_arg_t               * parg    /* IN  */,
               exportlist_t            * pexport /* IN  */,
               fsal_op_context_t             * pcontext   /* IN  */,
               cache_inode_client_t    * pclient /* IN  */,
               hash_table_t            * ht      /* INOUT */,
               struct svc_req          * preq    /* IN  */,
               nfs_res_t               * pres    /* OUT */ ) 
{
  cache_entry_t          * parent_pentry = NULL ;
  cache_entry_t          * pentry_child  = NULL ;
  fsal_attrib_list_t       pre_parent_attr      ;
  fsal_attrib_list_t       parent_attr          ;
  fsal_attrib_list_t     * ppre_attr            ;
  fsal_attrib_list_t       pentry_child_attr    ;
  cache_inode_file_type_t  filetype             ;
  cache_inode_file_type_t  childtype            ;
  cache_inode_status_t     cache_status         ; 
  int                      rc                   ;
  fsal_name_t              name ;
  char                   * dir_name = NULL;
  
	if (preq->rq_vers == NFS_V3)
	{
		/* to avoid setting it on each error case */
		pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.before.attributes_follow = FALSE;
		pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc.after.attributes_follow = FALSE;
		ppre_attr = NULL;
	}

	/* Convert file handle into a pentry */
  if( ( parent_pentry = nfs_FhandleToCache( preq->rq_vers, 
                                            &(parg->arg_rmdir2.dir),
                                            &(parg->arg_rmdir3.object.dir),
                                            NULL, 
                                            &(pres->res_stat2),
                                            &(pres->res_rmdir3.status),
                                            NULL, 
                                            &pre_parent_attr, 
                                            pcontext, 
                                            pclient, 
                                            ht, 
                                            &rc ) ) == NULL )
     {
      /* Stale NFS FH ? */
       return rc ;
     }
  
	/* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_parent_attr ;
  
  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert( pre_parent_attr.type ) ;

	/*
	 * Sanity checks: new directory name must be non-null; parent must be
	 * a directory. 
	 */
  if( filetype != DIR_BEGINNING && filetype != DIR_CONTINUE )
	{
		switch( preq->rq_vers )
		{
		case NFS_V2:
      pres->res_stat2 = NFSERR_NOTDIR;
      break;
		case NFS_V3:
      pres->res_rmdir3.status = NFS3ERR_NOTDIR;
      break;
    }
    
		return NFS_REQ_OK;
	}
  
	switch( preq->rq_vers )
	{
	case NFS_V2:
    dir_name = parg->arg_rmdir2.name;
    break;
    
	case NFS_V3:
    dir_name = parg->arg_rmdir3.object.name;
    break;
    
	}
  
	if (dir_name == NULL || strlen(dir_name) == 0)
    {
      cache_status = CACHE_INODE_INVALID_ARGUMENT;	/* for lack of better... */
    } 
  else
    {
      if( ( cache_status = cache_inode_error_convert( FSAL_str2name( dir_name, 
                                                                     FSAL_MAX_NAME_LEN,
                                                                     &name ) ) ) ==  CACHE_INODE_SUCCESS )
        {
          /*
           * Lookup to the entry to be removed to check if it is a directory 
           */
          if( ( pentry_child = cache_inode_lookup( parent_pentry, 
                                                   &name, 
                                                   &pentry_child_attr, 
                                                   ht, 
                                                   pclient, 
                                                   pcontext, 
                                                   &cache_status ) ) != NULL )
            {
               /* Extract the filetype */
              childtype = cache_inode_fsal_type_convert( pentry_child_attr.type ) ;

              /*
               * Sanity check: make sure we are about to remove a directory 
               */
              if( childtype != DIR_BEGINNING && childtype != DIR_CONTINUE )
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFSERR_NOTDIR;
                      break;
                      
                    case NFS_V3:
                      pres->res_rmdir3.status = NFS3ERR_NOTDIR;
                      break;
                    }
                  return NFS_REQ_OK ;
                }

              /*
               * Remove the directory.  Use NULL vnode for the directory
               * that's being removed because we know the directory's name. 
               */
              
              if( cache_inode_remove( parent_pentry, 
                                      &name, 
                                      &parent_attr, 
                                      ht, 
                                      pclient, 
                                      pcontext, 
                                      &cache_status ) == CACHE_INODE_SUCCESS )
                {
                  switch (preq->rq_vers)
                    {
                    case NFS_V2:
                      pres->res_stat2 = NFS_OK;
                      break;
                      
                    case NFS_V3:
                      /* Build Weak Cache Coherency data */
                      nfs_SetWccData( pcontext, pexport,
                                      parent_pentry,
                                      ppre_attr, 
                                      &parent_attr,
                                      &(pres->res_rmdir3.RMDIR3res_u.resok.dir_wcc));
                      
                      pres->res_rmdir3.status = NFS3_OK;
                      break;
                    }
                  return NFS_REQ_OK;
                }
            }
        }
    }
  
  /* If we are here, there was an error */
	if( nfs_RetryableError( cache_status ) )
    {
      return NFS_REQ_DROP;
    }
  
	nfs_SetFailedStatus( pcontext, pexport,
                       preq->rq_vers, 
                       cache_status,
                       &pres->res_stat2,
                       &pres->res_rmdir3.status,
                       NULL, NULL,
                       parent_pentry,
                       ppre_attr,
                       &(pres->res_rmdir3.RMDIR3res_u.resfail.dir_wcc),
                       NULL, NULL, NULL);


	return NFS_REQ_OK;
} /* nfs_Rmdir */


/**
 * nfs_Rmdir_Free: Frees the result structure allocated for nfs_Rmdir.
 * 
 * Frees the result structure allocated for nfs_Rmdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Rmdir_Free( nfs_res_t * resp )
{
  /* Nothing to do here */
  return ;
} /* nfs_Rmdir_Free */
