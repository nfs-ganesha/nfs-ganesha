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
 * \file    nfs_Fsstat.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.14 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs_Fsstat.c : Routines used for managing the NFS4 COMPOUND functions.
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
 * nfs_Fsstat: The NFS PROC2 and PROC3 FSSTAT
 *
 * Implements the NFS PROC2 and PROC3 FSSTAT. 
 * 
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list 
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call 
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK or NFS_REQ_DROP
 *
 */


int nfs_Fsstat( nfs_arg_t         * parg,    
                exportlist_t      * pexport, 
                fsal_op_context_t       * pcontext,   
                cache_inode_client_t    * pclient,
                hash_table_t      * ht,
                struct svc_req    * preq,    
                nfs_res_t         * pres )   
{
	static char  __attribute__(( __unused__ ))   funcName[] = "nfs_Fsstat";
 
  fsal_staticfsinfo_t   staticinfo ;
  fsal_dynamicfsinfo_t  dynamicinfo ;
  cache_inode_status_t  cache_status ;
  cache_entry_t       * pentry = NULL ;
  fsal_attrib_list_t    attr;
  int                   rc = 0 ;
  
	if (preq->rq_vers == NFS_V3)
	{
		/* to avoid setting it on each error case */
		pres->res_fsstat3.FSSTAT3res_u.resfail.obj_attributes.attributes_follow = FALSE;
	}

	/* convert file handle to vnode */	
  if( ( pentry = nfs_FhandleToCache( preq->rq_vers, 
                                     &(parg->arg_lookup2.dir), 
                                     &(parg->arg_lookup3.what.dir),
                                     NULL,
                                     &(pres->res_statfs2.status),
                                     &(pres->res_fsstat3.status),
                                     NULL,
                                     NULL, 
                                     pcontext, 
                                     pclient, 
                                     ht, 
                                     &rc ) ) == NULL )
    {
      /* Stale NFS FH ? */
      /* return NFS_REQ_DROP ; */
      return rc ;
    }

	/* Get statistics and convert from cache */
  
  if( ( cache_status = cache_inode_statfs( pentry, 
                                           &staticinfo, 
                                           &dynamicinfo, 
                                           pcontext,
                                           &cache_status ) ) == CACHE_INODE_SUCCESS )
    {
      /* This call is costless, the pentry was cached during call to nfs_FhandleToCache */
      if( ( cache_status = cache_inode_getattr( pentry,
                                                &attr, 
                                                ht,
                                                pclient, pcontext,
                                                &cache_status ) ) == CACHE_INODE_SUCCESS )
        {     
#ifdef  _DEBUG_NFSPROTO
          printf( "-- nfs_Fsstat --> dynamicinfo.total_bytes = %llu dynamicinfo.free_bytes = %llu dynamicinfo.avail_bytes = %llu\n", 
                  dynamicinfo.total_bytes,  dynamicinfo.free_bytes,  dynamicinfo.avail_bytes ) ;
          printf( "-- nfs_Fsstat --> dynamicinfo.total_files = %llu dynamicinfo.free_files = %llu dynamicinfo.avail_files = %llu\n", 
                  dynamicinfo.total_files,  dynamicinfo.free_files,  dynamicinfo.avail_files ) ;
#endif               
          switch (preq->rq_vers)
            {
            case NFS_V2:
              pres->res_statfs2.STATFS2res_u.info.tsize  = NFS2_MAXDATA;
              pres->res_statfs2.STATFS2res_u.info.bsize  = DEV_BSIZE ;
              pres->res_statfs2.STATFS2res_u.info.blocks = dynamicinfo.total_bytes / DEV_BSIZE ;
              pres->res_statfs2.STATFS2res_u.info.bfree  = dynamicinfo.free_bytes  / DEV_BSIZE ;
              pres->res_statfs2.STATFS2res_u.info.bavail = dynamicinfo.avail_bytes / DEV_BSIZE ;
              pres->res_statfs2.status = NFS_OK;
              break;
              
            case NFS_V3:
              nfs_SetPostOpAttr( pcontext, pexport,
                                 pentry, 
                                 &attr,
                                 &(pres->res_fsstat3.FSSTAT3res_u.resok.obj_attributes) );
              
              pres->res_fsstat3.FSSTAT3res_u.resok.tbytes = dynamicinfo.total_bytes;
              pres->res_fsstat3.FSSTAT3res_u.resok.fbytes = dynamicinfo.free_bytes;
              pres->res_fsstat3.FSSTAT3res_u.resok.abytes = dynamicinfo.avail_bytes;
              pres->res_fsstat3.FSSTAT3res_u.resok.tfiles = dynamicinfo.total_files;
              pres->res_fsstat3.FSSTAT3res_u.resok.ffiles = dynamicinfo.free_files ;
              pres->res_fsstat3.FSSTAT3res_u.resok.afiles = dynamicinfo.avail_files;
              pres->res_fsstat3.FSSTAT3res_u.resok.invarsec = 0;	/* volatile FS */
              pres->res_fsstat3.status = NFS3_OK;

#ifdef  _DEBUG_NFSPROTO
              printf( "-- nfs_Fsstat --> tbytes=%llu fbytes=%llu abytes=%llu\n", 
                      pres->res_fsstat3.FSSTAT3res_u.resok.tbytes,
                      pres->res_fsstat3.FSSTAT3res_u.resok.fbytes,
                      pres->res_fsstat3.FSSTAT3res_u.resok.abytes ) ;
              
              printf( "-- nfs_Fsstat --> tfiles=%llu fffiles=%llu afiles=%llu\n", 
                      pres->res_fsstat3.FSSTAT3res_u.resok.tfiles, 
                      pres->res_fsstat3.FSSTAT3res_u.resok.ffiles, 
                      pres->res_fsstat3.FSSTAT3res_u.resok.afiles ) ;
#endif           

              break;

            }
          return NFS_REQ_OK ;
        }
    }
  
	/* At this point we met an error */
	if( nfs_RetryableError( cache_status) )
		return NFS_REQ_DROP ;

	nfs_SetFailedStatus( pcontext, pexport,
                       preq->rq_vers, 
                       cache_status,
                       &pres->res_statfs2.status,
                       &pres->res_fsstat3.status,
                       NULL, NULL,
                       NULL, NULL, NULL,
                       NULL, NULL, NULL);
  
	return NFS_REQ_OK ;
} /* nfs_Fsstat */

/**
 * nfs_Fsstat_Free: Frees the result structure allocated for nfs_Fsstat.
 * 
 * Frees the result structure allocated for nfs_Fsstat.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Fsstat_Free( nfs_res_t * resp )
{
  /* Nothing to do here */
  return ;
} /* nfs_Fsstat_Free */
