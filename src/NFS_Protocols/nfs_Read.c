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
 * \file    nfs_Read.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:53 $
 * \version $Revision: 1.17 $
 * \brief   Routines used for managing the NFS READ request.
 *
 * nfs_Read.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <sys/file.h>		/* for having FNDELAY */
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
#include "nfs_proto_tools.h"
#include "nfs_tools.h"

/**
 *
 * nfs_Read: The NFS PROC2 and PROC3 READ
 *
 * Implements the NFS PROC READ function (for V2 and V3).
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

int nfs_Read(nfs_arg_t * parg,
	     exportlist_t * pexport,
	     fsal_op_context_t * pcontext,
	     cache_inode_client_t * pclient,
	     hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  static char __attribute__ ((__unused__)) funcName[] = "nfs_Read";

  cache_entry_t *pentry;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t *ppre_attr;
  int rc;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  cache_content_status_t content_status;
  fsal_seek_t seek_descriptor;
  fsal_size_t size = 0;
  fsal_size_t read_size;
  fsal_off_t offset = 0;
  caddr_t data = NULL;
  cache_inode_file_type_t filetype;
  fsal_boolean_t eof_met;

  cache_content_policy_data_t datapol;

  datapol.UseMaxCacheSize = FALSE;

  if (preq->rq_vers == NFS_V3)
    {
      /* to avoid setting it on each error case */
      pres->res_read3.READ3res_u.resfail.file_attributes.attributes_follow = FALSE;
    }

  /* Convert file handle into a cache entry */
  if ((pentry = nfs_FhandleToCache(preq->rq_vers,
				   &(parg->arg_read2.file),
				   &(parg->arg_read3.file),
				   NULL,
				   &(pres->res_read2.status),
				   &(pres->res_read3.status),
				   NULL, &pre_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  if ((preq->rq_vers == NFS_V3) && (nfs3_Is_Fh_Xattr(&(parg->arg_read3.file))))
    return nfs3_Read_Xattr(parg, pexport, pcontext, pclient, ht, preq, pres);

  /* get directory attributes before action (for V3 reply) */
  ppre_attr = &pre_attr;

  /* Extract the filetype */
  filetype = cache_inode_fsal_type_convert(pre_attr.type);

  /* Sanity check: read only from a regular file */
  if (filetype != REGULAR_FILE)
    {
      switch (preq->rq_vers)
	{
	case NFS_V2:
	  /*
	   * In the RFC tell it not good but it does
	   * not tell what to do ... 
	   */
	  pres->res_attr2.status = NFSERR_ISDIR;
	  break;

	case NFS_V3:
	  if (filetype == DIR_BEGINNING || filetype == DIR_CONTINUE)
	    pres->res_read3.status = NFS3ERR_ISDIR;
	    else
	    pres->res_read3.status = NFS3ERR_INVAL;
	  break;
	}

      return NFS_REQ_OK;
    }

  /* For MDONLY export, reject write operation */
  /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level */
  /* This is done by replying EDQUOT (this error is known for not disturbing the client's requests cache */
  if (pexport->access_type == ACCESSTYPE_MDONLY
      || pexport->access_type == ACCESSTYPE_MDONLY_RO)
    {
      switch (preq->rq_vers)
	{
	case NFS_V2:
	  pres->res_attr2.status = NFSERR_DQUOT;
	  break;

	case NFS_V3:
	  pres->res_read3.status = NFS3ERR_DQUOT;
	  break;
	}

      nfs_SetFailedStatus(pcontext, pexport,
			  preq->rq_vers,
			  cache_status,
			  &pres->res_read2.status,
			  &pres->res_read3.status,
			  pentry,
			  &(pres->res_read3.READ3res_u.resfail.file_attributes),
			  NULL, NULL, NULL, NULL, NULL, NULL);

      return NFS_REQ_OK;
    }

  /* Extract the argument from the request */
  switch (preq->rq_vers)
    {
    case NFS_V2:
      offset = parg->arg_read2.offset;	/* beginoffset is obsolete */
      size = parg->arg_read2.count;	/* totalcount is obsolete  */
      break;

    case NFS_V3:
      offset = parg->arg_read3.offset;
      size = parg->arg_read3.count;

#ifdef _DEBUG_NFSPROTO
      printf("-----> Read offset=%lld count=%u MaxOffSet=%lld\n", parg->arg_read3.offset,
	     parg->arg_read3.count, pexport->MaxOffsetRead);
#endif

      /* 
       * do not exceed maxium READ/WRITE offset if set 
       */
      if ((pexport->options & EXPORT_OPTION_MAXOFFSETREAD) == EXPORT_OPTION_MAXOFFSETREAD)
	if ((fsal_off_t) (offset + size) > pexport->MaxOffsetRead)
	  {

	    DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
			      "NFS READ: A client tryed to violate max file size %lld for exportid #%hu",
			      pexport->MaxOffsetRead, pexport->id);

	    switch (preq->rq_vers)
	      {
	      case NFS_V2:
		pres->res_attr2.status = NFSERR_DQUOT;
		break;

	      case NFS_V3:
		pres->res_read3.status = NFS3ERR_INVAL;
		break;
	      }

	    nfs_SetFailedStatus(pcontext, pexport,
				preq->rq_vers,
				cache_status,
				&pres->res_read2.status,
				&pres->res_read3.status,
				pentry,
				&(pres->res_read3.READ3res_u.resfail.file_attributes),
				NULL, NULL, NULL, NULL, NULL, NULL);

	    return NFS_REQ_OK;

	  }

      /*
       * We should not exceed the FSINFO rtmax field for
       * the size 
       */
      if (((pexport->options & EXPORT_OPTION_MAXREAD) == EXPORT_OPTION_MAXREAD) &&
	  size > pexport->MaxRead)
	{
	  /*
	   * The client asked for too much, normally
	   * this should not happen because the client
	   * is calling nfs_Fsinfo at mount time and so
	   * is aware of the server maximum write size 
	   */
	  size = pexport->MaxRead;
	}
      break;
    }

  if (size == 0)
    {
      cache_status = CACHE_INODE_SUCCESS;
      read_size = 0;
      data = NULL;
    } else
    {
      data = Mem_Alloc(size);

      if (data == NULL)
	{
	  return NFS_REQ_DROP;
	}

      seek_descriptor.whence = FSAL_SEEK_SET;
      seek_descriptor.offset = offset;

      datapol.UseMaxCacheSize = pexport->options & EXPORT_OPTION_MAXCACHESIZE;
      datapol.MaxCacheSize = pexport->MaxCacheSize;

      /* If export is not cached, cache it now */
      if ((pexport->options & EXPORT_OPTION_USE_DATACACHE) &&
	  (cache_content_cache_behaviour(pentry,
					 &datapol,
					 (cache_content_client_t *)
					 pclient->pcontent_client,
					 &content_status) == CACHE_CONTENT_FULLY_CACHED)
	  && (pentry->object.file.pentry_content == NULL))
	{
	  /* Entry is not in datacache, but should be in, cache it */
	  cache_inode_add_data_cache(pentry, ht, pclient, pcontext, &cache_status);
	  if ((cache_status != CACHE_INODE_SUCCESS) &&
	      (cache_status != CACHE_INODE_CACHE_CONTENT_EXISTS))
	    {
	      /* Entry is not in datacache, but should be in, cache it .
	       * Several threads may call this function at the first time and a race condition can occur here
	       * in order to avoid this, cache_inode_add_data_cache is "mutex protected" 
	       * The first call will create the file content cache entry, the further will return
	       * with error CACHE_INODE_CACHE_CONTENT_EXISTS which is not a pathological thing here */

	      /* If we are here, there was an error */
	      if (nfs_RetryableError(cache_status))
		{
		  return NFS_REQ_DROP;
		}

	      nfs_SetFailedStatus(pcontext, pexport,
				  preq->rq_vers,
				  cache_status,
				  &pres->res_read2.status,
				  &pres->res_read3.status,
				  pentry,
				  &(pres->res_read3.READ3res_u.resfail.file_attributes),
				  NULL, NULL, NULL, NULL, NULL, NULL);

	      return NFS_REQ_OK;
	    }
	}

      if (cache_inode_rdwr(pentry,
			   CACHE_CONTENT_READ,
			   &seek_descriptor,
			   size,
			   &read_size,
			   &attr,
			   data,
			   &eof_met,
			   ht,
			   pclient, pcontext, TRUE, &cache_status) == CACHE_INODE_SUCCESS)

	{
	  switch (preq->rq_vers)
	    {
	    case NFS_V2:
	      pres->res_read2.READ2res_u.readok.data.nfsdata2_val = data;
	      pres->res_read2.READ2res_u.readok.data.nfsdata2_len = read_size;

	      nfs2_FSALattr_To_Fattr(pexport, &attr,
				     &(pres->res_read2.READ2res_u.readok.attributes));

	      pres->res_attr2.status = NFS_OK;
	      break;

	    case NFS_V3:

	      pres->res_read3.READ3res_u.resok.eof = FALSE;

	      /* Did we reach eof ? */
	      /* BUGAZOMEU use eof */
	      if ((offset + read_size) >= attr.filesize)
		pres->res_read3.READ3res_u.resok.eof = TRUE;

	      /* Build Post Op Attributes */
	      nfs_SetPostOpAttr(pcontext, pexport,
				pentry,
				&attr,
				&(pres->res_read3.READ3res_u.resok.file_attributes));

	      pres->res_read3.READ3res_u.resok.file_attributes.attributes_follow = TRUE;

	      pres->res_read3.READ3res_u.resok.count = read_size;
	      pres->res_read3.READ3res_u.resok.data.data_val = data;
	      pres->res_read3.READ3res_u.resok.data.data_len = read_size;

	      pres->res_read3.status = NFS3_OK;
	      break;
	    }			/* switch */

	  return NFS_REQ_OK;
	}
    }

  /* If we are here, there was an error */
  if (nfs_RetryableError(cache_status))
    {
      return NFS_REQ_DROP;
    }

  nfs_SetFailedStatus(pcontext, pexport,
		      preq->rq_vers,
		      cache_status,
		      &pres->res_read2.status,
		      &pres->res_read3.status,
		      pentry,
		      &(pres->res_read3.READ3res_u.resfail.file_attributes),
		      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}				/* nfs_Read */

/**
 * nfs2_Read_Free: Frees the result structure allocated for nfs2_Read.
 * 
 * Frees the result structure allocated for nfs2_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_Read_Free(nfs_res_t * resp)
{
  if ((resp->res_read2.status == NFS_OK) &&
      (resp->res_read2.READ2res_u.readok.data.nfsdata2_len != 0))
    Mem_Free(resp->res_read2.READ2res_u.readok.data.nfsdata2_val);
}				/* nfs2_Read_Free */

/**
 * nfs3_Read_Free: Frees the result structure allocated for nfs3_Read.
 * 
 * Frees the result structure allocated for nfs3_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_Read_Free(nfs_res_t * resp)
{
  if ((resp->res_read3.status == NFS3_OK) &&
      (resp->res_read3.READ3res_u.resok.data.data_len != 0))
    Mem_Free(resp->res_read3.READ3res_u.resok.data.data_val);
}				/* nfs3_Read_Free */
