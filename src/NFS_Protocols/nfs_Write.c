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
 * \file    nfs_Write.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:54 $
 * \version $Revision: 1.14 $
 * \brief   Routines used for managing the Write requests.
 *
 * nfs_Write.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "cache_content_policy.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"


/**
 *
 * nfs_Write: The NFS PROC2 and PROC3 WRITE
 *
 * Implements the NFS PROC WRITE function (for V2 and V3).
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
extern writeverf3  NFS3_write_verifier ; /* NFS V3 write verifier      */
 
int nfs_Write( nfs_arg_t              * parg,    
               exportlist_t           * pexport, 
               fsal_op_context_t      * pcontext,   
               cache_inode_client_t   * pclient,
               hash_table_t           * ht,
               struct svc_req         * preq,    
               nfs_res_t              * pres )
{
  static char  __attribute__(( __unused__ ))   funcName[] = "nfs_Write";
  
  cache_entry_t           * pentry ;
  fsal_attrib_list_t        attr ;
  fsal_attrib_list_t        pre_attr ;
  fsal_attrib_list_t      * ppre_attr ;
  int                       rc ;
  cache_inode_status_t      cache_status = CACHE_INODE_SUCCESS;
  cache_content_status_t    content_status ;
  fsal_seek_t               seek_descriptor ;
  fsal_size_t               size = 0 ;
  fsal_size_t               written_size;
  fsal_off_t                offset = 0;
  caddr_t                   data = NULL;
  enum stable_how           stable;	/* NFS V3 storage stability, see RFC1813 page 50 */
  cache_inode_file_type_t   filetype      ;
  fsal_boolean_t            eof_met ;
 
  cache_content_policy_data_t datapol ;

  datapol.UseMaxCacheSize = FALSE ;
 
  if( preq->rq_vers == NFS_V3 )
   {
	/* to avoid setting it on each error case */
	pres->res_write3.WRITE3res_u.resfail.file_wcc.before.attributes_follow = FALSE;
	pres->res_write3.WRITE3res_u.resfail.file_wcc.after.attributes_follow  = FALSE;
	ppre_attr = NULL;
   }
  
  /* Convert file handle into a cache entry */ 
  if( ( pentry = nfs_FhandleToCache( preq->rq_vers, 
                                     &(parg->arg_write2.file),
                                     &(parg->arg_write3.file),
                                     NULL, 
                                     &(pres->res_attr2.status),
                                     &(pres->res_write3.status),
                                     NULL,
                                     &pre_attr, 
                                     pcontext, 
                                     pclient, 
                                     ht, 
                                     &rc ) ) == NULL )
    {
      /* Stale NFS FH ? */
      return rc ;
    }
 
  if( ( preq->rq_vers == NFS_V3 ) &&
      ( nfs3_Is_Fh_Xattr( &(parg->arg_write3.file) ) ) ) 
   return nfs3_Write_Xattr( parg, 
    		            pexport, 
                            pcontext,
		  	    pclient,
                            ht,
                            preq, 
		            pres ) ;
 
  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_attr;
  
  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert( pre_attr.type ) ;
  
  /* Sanity check: write only a regular file */
  if( filetype != REGULAR_FILE )
    {
      switch( preq->rq_vers )
        {
        case NFS_V2:
          /*
           * In the RFC tell it not good but it does
           * not tell what to do ... 
           * We use NFSERR_ISDIR for lack of better
           */
          pres->res_attr2.status = NFSERR_ISDIR;
          break;
          
        case NFS_V3:
          if( filetype == DIR_BEGINNING || filetype == DIR_CONTINUE )
            pres->res_write3.status = NFS3ERR_ISDIR ;
          else
            pres->res_write3.status = NFS3ERR_INVAL;
          break;
        }
      return NFS_REQ_OK ;
    }
  
  /* For MDONLY export, reject write operation */
  /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level */
  /* This is done by replying EDQUOT (this error is known for not disturbing the client's requests cache */
  if( pexport->access_type == ACCESSTYPE_MDONLY )
    {
      switch( preq->rq_vers )
        {
        case NFS_V2:
          pres->res_attr2.status = NFSERR_DQUOT ;
          break;

        case NFS_V3:
          pres->res_write3.status = NFS3ERR_DQUOT;
          break;
        }

       nfs_SetFailedStatus( pcontext, pexport,
                            preq->rq_vers, 
                            cache_status,
                            &pres->res_attr2.status,
                            &pres->res_write3.status,
                            NULL, NULL,
                            pentry, 
                            ppre_attr,
                            &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                            NULL, NULL, NULL);
  
       
       return NFS_REQ_OK ;
    }
  
  /* Extract the argument from the request */
	switch( preq->rq_vers )
    {
    case NFS_V2:
      if( ppre_attr && ppre_attr->filesize > NFS2_MAX_FILESIZE )
        {
          /*
           *  V2 clients don't understand filesizes >
           *  2GB, so we don't allow them to alter
           *  them in any way. BJP 6/26/2001
           */
          pres->res_attr2.status = NFSERR_FBIG;
          return NFS_REQ_OK ;
        }
      
      offset = parg->arg_write2.offset;	          /* beginoffset is obsolete */
      size = parg->arg_write2.data.nfsdata2_len;	/* totalcount is obsolete  */
      data = parg->arg_write2.data.nfsdata2_val;
      stable = FILE_SYNC;
      break;
      
    case NFS_V3:
			offset = parg->arg_write3.offset;
			size = parg->arg_write3.count;

			if( size > parg->arg_write3.data.data_len)
        {
          /* should never happen */
          pres->res_write3.status = NFS3ERR_INVAL;
          return NFS_REQ_OK ;
        }

#ifdef _DEBUG_NFSPROTO
      printf( "----> Write offset=%lld count=%u\n", parg->arg_write3.offset, parg->arg_write3.count ) ;
#endif
 

    /*
     * do not exceed maxium READ/WRITE offset if set
     */
    if( ( pexport->options & EXPORT_OPTION_MAXOFFSETWRITE ) == EXPORT_OPTION_MAXOFFSETWRITE )  
      if( (fsal_off_t)(size + offset) > pexport->MaxOffsetWrite )
        {
          
          DisplayLogJdLevel( pclient->log_outputs, NIV_EVENT,
              "NFS WRITE: A client tryed to violate max file size %lld for exportid #%hu",
              pexport->MaxOffsetWrite, pexport->id ) ;
          
          switch( preq->rq_vers )
	        {
        	case NFS_V2:
          		pres->res_attr2.status = NFSERR_DQUOT ;
          		break;

        	case NFS_V3:
          		pres->res_write3.status = NFS3ERR_DQUOT;
          		break;
        	}
    
          nfs_SetFailedStatus( pcontext, pexport,
                               preq->rq_vers,
                               cache_status,
                               &pres->res_attr2.status,
                               &pres->res_write3.status,
                               NULL, NULL,
                               pentry,
                               ppre_attr,
                               &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                               NULL, NULL, NULL);


          return NFS_REQ_OK ;

        }
     
	/*
	 * We should take care not to exceed FSINFO wtmax
	 * field for the size 
	 */
      if(  ( pexport->options & EXPORT_OPTION_MAXWRITE == EXPORT_OPTION_MAXWRITE ) &&
           size > pexport->MaxWrite )
        {
          /*
           * The client asked for too much data, we
           * must restrict him 
           */
          size = pexport->MaxWrite ;
        }
			data = parg->arg_write3.data.data_val;
			stable = parg->arg_write3.stable;
			break;
    }
 
  
  if( size == 0 )
    {
      cache_status = CACHE_INODE_SUCCESS ;
      written_size = 0;
    }
  else
    {
      /* An actual write is to be made, prepare it */

      /* If entry is not cached, cache it now */
      datapol.UseMaxCacheSize = pexport->options & EXPORT_OPTION_MAXCACHESIZE ;
      datapol.MaxCacheSize = pexport->MaxCacheSize ;


      if( ( pexport->options & EXPORT_OPTION_USE_DATACACHE ) && 
          ( cache_content_cache_behaviour( pentry, 
                                           &datapol,
                                           (cache_content_client_t *)pclient->pcontent_client, 
                                           &content_status ) == CACHE_CONTENT_FULLY_CACHED ) && 
          ( pentry->object.file.pentry_content == NULL ) )
        {
          /* Entry is not in datacache, but should be in, cache it .
           * Several threads may call this function at the first time and a race condition can occur here
           * in order to avoid this, cache_inode_add_data_cache is "mutex protected" 
           * The first call will create the file content cache entry, the further will return
           * with error CACHE_INODE_CACHE_CONTENT_EXISTS which is not a pathological thing here */
   
          /* Status is set in last argument */
          cache_inode_add_data_cache( pentry, 
                                      ht, 
                                      pclient,
                                      pcontext, 
                                      &cache_status ) ;
          if( ( cache_status != CACHE_INODE_SUCCESS ) && 
              ( cache_status != CACHE_INODE_CACHE_CONTENT_EXISTS ) )
            {
              /* If we are here, there was an error */
              if( nfs_RetryableError( cache_status ) )
                {
                  return NFS_REQ_DROP ;
                }
              
              nfs_SetFailedStatus( pcontext, pexport,
                                   preq->rq_vers, 
                                   cache_status,
                                   &pres->res_attr2.status,
                                   &pres->res_write3.status,
                                   NULL, NULL,
                                   pentry, 
                                   ppre_attr,
                                   &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                                   NULL, NULL, NULL);
              
              return NFS_REQ_OK ; 
            }
        }
      
      /* only FILE_SYNC mode is supported */
      /* Set up uio to define the transfer */
      seek_descriptor.whence = FSAL_SEEK_SET ;
      seek_descriptor.offset = offset ;

      if( cache_inode_rdwr( pentry, 
                            CACHE_CONTENT_WRITE,
                            &seek_descriptor,
                            size,
                            &written_size,
                            &attr,
                            data, 
                            &eof_met,
                            ht, 
                            pclient, 
                            pcontext, 
                            &cache_status )  == CACHE_INODE_SUCCESS )
        {

          switch (preq->rq_vers)
            {
            case NFS_V2:
              nfs2_FSALattr_To_Fattr( pexport,
                                      &attr,
                                      &(pres->res_attr2.ATTR2res_u.attributes) );
              
              pres->res_attr2.status = NFS_OK;
              break;
              
            case NFS_V3:
              
              /* Build Weak Cache Coherency data */
              nfs_SetWccData( pcontext,
                              pexport,
                              pentry, 
                              ppre_attr, 
                              &attr,
                              &(pres->res_write3.WRITE3res_u.resok.file_wcc) );
              
              /* Set the written size */
              pres->res_write3.WRITE3res_u.resok.count = written_size;
              
              /* We always use FILE_SYNC mode */
              pres->res_write3.WRITE3res_u.resok.committed = FILE_SYNC; 
              
              /* Set the write verifier */
              memcpy(pres->res_write3.WRITE3res_u.resok.verf, 
                     NFS3_write_verifier,
                     sizeof(writeverf3));
              
              pres->res_write3.status = NFS3_OK;
              break;
            }
 
          return NFS_REQ_OK ;
        }
    }

#ifdef _DEBUG_NFSPROTO
  printf( "---> failed write: cache_status=%d\n", cache_status ) ;
#endif

  /* If we are here, there was an error */
  if( nfs_RetryableError( cache_status ) )
    {
      return NFS_REQ_DROP ;
    }
  
  nfs_SetFailedStatus( pcontext, pexport,
                       preq->rq_vers, 
                       cache_status,
                       &pres->res_attr2.status,
                       &pres->res_write3.status,
                       NULL, NULL,
                       pentry, 
                       ppre_attr,
                       &(pres->res_write3.WRITE3res_u.resfail.file_wcc),
                       NULL, NULL, NULL);
  
	return NFS_REQ_OK ;
} /* nfs_Write.c */
  


/**
 * nfs_Write_Free: Frees the result structure allocated for nfs_Write.
 * 
 * Frees the result structure allocated for nfs_Write.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs_Write_Free( nfs_res_t * resp )
{
  return;
} /* nfs_Write_Free */







