/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * \file    nfs_tools.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/20 07:39:22 $
 * \version $Revision: 1.14 $
 * \brief   Some tools very usefull in the nfs protocol implementation.
 *
 * nfs_tools.c : Some tools very usefull in the nfs protocol implementation
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
#include <sys/types.h>
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "stuff_alloc.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_dupreq.h"

extern nfs_parameter_t nfs_param;
extern nfs_function_desc_t nfs2_func_desc[];
extern nfs_function_desc_t nfs3_func_desc[];
extern nfs_function_desc_t nfs4_func_desc[];
extern nfs_function_desc_t mnt1_func_desc[];
extern nfs_function_desc_t mnt3_func_desc[];
#ifdef _USE_NLM
extern nfs_function_desc_t nlm4_func_desc[];
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
extern nfs_function_desc_t rquota1_func_desc[];
extern nfs_function_desc_t rquota2_func_desc[];
#endif                          /* _USE_QUOTA */
/* Structure used for duplicated request cache */
hash_table_t *ht_dupreq;

/**
 * 
 * get_rpc_xid: extract the transfer Id from a RPC request.
 *
 * RPC Xid is used for RPC Reply cache. With UDP connection, the xid is in an opaque structure
 * stored in xprt->xp_p2, but with TCP connection, the xid is in another opaque structure stored
 * in xprt->xp_p1. xp_p2 and xp_p1 are mutually exclusive. The opaque structure are well defined in
 * ONC RPC protocol definitions, and used internally by the ONC layers. Since I need to know the xid
 * the structures are defined here.
 *
 *  @param reqp A pointer to the request to be examined.
 *
 *  @return the found xid.
 *
 */

unsigned int get_rpc_xid(struct svc_req *reqp)
{
  /* RPC Xid is used for RPC Reply cache. With UDP connection, the xid is in an opaque structure
   * stored in xprt->xp_p2, but with TCP connection, the xid is in another opaque structure stored
   * in xprt->xp_p1. xp_p2 and xp_p1 are mutually exclusive. The opaque structure are well defined in
   * ONC RPC protocol definitions, and used internally by the ONC layers. Since I need to know the xid
   * the structures are defined here */

  struct udp_private2__
  {                             /* kept in xprt->xp_p2 */
    int up_unused;
    u_long up_xid;
  };

  struct tcp_conn2__
  {                             /* kept in xprt->xp_p1 */
    enum xprt_stat strm_stat;
    u_long x_id;
    XDR xdrs;
    char verf_body[MAX_AUTH_BYTES];
  };

  unsigned int Xid;

  /* Map the xp1 and xp2 field to the udp and tcp private structures */
  struct tcp_conn2__ *ptcpxp = (struct tcp_conn2__ *)(reqp->rq_xprt->xp_p1);
  struct udp_private2__ *pudpxp = (struct udp_private2__ *)(reqp->rq_xprt->xp_p2);

  /* The request is either UDP or TCP. If UDP Xid is null, then look for TCP xid */
  if(reqp->rq_xprt->xp_p2 != NULL)
    Xid = pudpxp->up_xid;       /* UDP XID */
  else
    Xid = ptcpxp->x_id;         /* TCP XID */

  return Xid;
}                               /* get_rpc_xid */

/**
 *
 * print_entry_dupreq: prints an entry in the LRU list.
 * 
 * prints an entry in the LRU list.
 *
 * @param data [IN] data stored in a LRU entry to be printed.
 * @param str [OUT] string used to store the result. 
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int print_entry_dupreq(LRU_data_t data, char *str)
{
  strcpy(str, "");
  return 0;
}

static int _remove_dupreq(hash_buffer_t *buffkey, dupreq_entry_t *pdupreq,
			  struct prealloc_pool *dupreq_pool, int nfs_req_status)
{
  int rc;
  nfs_function_desc_t funcdesc;

  rc = HashTable_Del(ht_dupreq, buffkey, NULL, NULL);

  /* if hashtable no such key => dupreq garbaged by another thread */
  if(rc != HASHTABLE_SUCCESS && rc != HASHTABLE_ERROR_NO_SUCH_KEY)
    return 1;                   /* Error while cleaning */
  else if(rc == HASHTABLE_ERROR_NO_SUCH_KEY)
    return 0;                   /* don't free the dupreq twice */

  /* Locate the function descriptor associated with this cached request */
  if(pdupreq->rq_prog == nfs_param.core_param.nfs_program)
    {
      switch (pdupreq->rq_vers)
        {
        case NFS_V2:
          funcdesc = nfs2_func_desc[pdupreq->rq_proc];
          break;

        case NFS_V3:
          funcdesc = nfs3_func_desc[pdupreq->rq_proc];
          break;

        case NFS_V4:
          funcdesc = nfs4_func_desc[pdupreq->rq_proc];
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogMajor(COMPONENT_DUPREQ, "NFS DUPREQ: NFS Protocol version %d unknown in dupreq_gc",
                   (int)pdupreq->rq_vers);
          funcdesc = nfs2_func_desc[0]; /* free function for PROC_NULL does nothing */
          break;
        }
    }
  else if(pdupreq->rq_prog == nfs_param.core_param.mnt_program)
    {
      switch (pdupreq->rq_vers)
        {
        case MOUNT_V1:
          funcdesc = mnt1_func_desc[pdupreq->rq_proc];
          break;

        case MOUNT_V3:
          funcdesc = mnt3_func_desc[pdupreq->rq_proc];
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogMajor(COMPONENT_DUPREQ, "NFS DUPREQ: MOUNT Protocol version %d unknown in dupreq_gc",
                   (int)pdupreq->rq_vers);
          break;

        }                       /* switch( pdupreq->vers ) */
    }
#ifdef _USE_NLM
  else if(pdupreq->rq_prog == nfs_param.core_param.nlm_program)
    {

      switch (pdupreq->rq_vers)
        {
        case NLM4_VERS:
          funcdesc = nlm4_func_desc[pdupreq->rq_proc];
          break;
        }                       /* switch( pdupreq->vers ) */
    }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
  else if(pdupreq->rq_prog == nfs_param.core_param.rquota_program)
    {

      switch (pdupreq->rq_vers)
        {
        case RQUOTAVERS:
          funcdesc = rquota1_func_desc[pdupreq->rq_proc];
          break;

        case EXT_RQUOTAVERS:
          funcdesc = rquota2_func_desc[pdupreq->rq_proc];
          break;
        }                       /* switch( pdupreq->vers ) */
    }
#endif
  else
    {
      /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
      LogMajor(COMPONENT_DUPREQ, "NFS DUPREQ: protocol %d is not managed", (int)pdupreq->rq_prog);
    }

  /* Call the free function */
  if (nfs_req_status == NFS_REQ_OK)
    funcdesc.free_function(&(pdupreq->res_nfs));

  /* Send the entry back to the pool */
  ReleaseToPool(pdupreq, dupreq_pool);

  return DUPREQ_SUCCESS;
}

int nfs_dupreq_delete(long xid, struct svc_req *ptr_req, SVCXPRT *xprt,
                      struct prealloc_pool *dupreq_pool)
{
  int status;

  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  struct sockaddr_in *phostaddr;
  dupreq_key_t pdupkey;
  dupreq_entry_t *dupreq;

  /* Get the socket address for the key */
  phostaddr = svc_getcaller(xprt);
  memcpy((char *)&pdupkey.addr, (char *)phostaddr,
	 sizeof(pdupkey.addr));
  pdupkey.xid = xid;
  /* Checksum the request */
  pdupkey.checksum = 0;
  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) &pdupkey;
  buffkey.len = sizeof(dupreq_key_t);

  if(HashTable_Get(ht_dupreq, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      /* reset timestamp */
      ((dupreq_entry_t *) buffval.pdata)->timestamp = time(NULL);

      status = DUPREQ_SUCCESS;
      dupreq = (dupreq_entry_t *) buffval.pdata;
      LogDebug(COMPONENT_DUPREQ, "NFS DUPREQ: Hit in the dupreq cache for xid=%u addr=%d", xid,
	       phostaddr->sin_addr.s_addr);
    }
  else {
    return DUPREQ_NOT_FOUND;  
  }

  LogFullDebug(COMPONENT_DUPREQ, "NFS_DUPREQ: REMOVING ip= %d.%d.%d.%d port=%d xid=%u rq_prog=%d",
               (ntohl(phostaddr->sin_addr.s_addr) & 0xFF000000) >> 24,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x00FF0000) >> 16,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x0000FF00) >> 8,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x000000FF),
               phostaddr->sin_port,
               xid,
               dupreq->rq_prog);
  
  status = _remove_dupreq(&buffkey, dupreq, dupreq_pool, ! NFS_REQ_OK);
  return status;
}

/**
 *
 * clean_entry_dupreq: cleans an entry in the dupreq cache.
 *
 * cleans an entry in the dupreq cache.
 *
 * @param pentry [INOUT] entry to be cleaned. 
 * @param addparam [IN] additional parameter used for cleaning.
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int clean_entry_dupreq(LRU_entry_t * pentry, void *addparam)
{
  hash_buffer_t buffkey;
  nfs_function_desc_t funcdesc;
  struct prealloc_pool *dupreq_pool = (struct prealloc_pool *) addparam;
  dupreq_entry_t *pdupreq = (dupreq_entry_t *) (pentry->buffdata.pdata);
  int rc;

  struct sockaddr_in *phostaddr;
  dupreq_key_t pdupkey;

  /* Get the socket address for the key */
  memcpy((char *)&pdupkey.addr, (char *)&pdupreq->addr, sizeof(pdupkey.addr));
  pdupkey.xid = pdupreq->xid;
  /* Checksum the request */
  pdupkey.checksum = pdupreq->checksum;

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) &pdupkey;
  buffkey.len = sizeof(dupreq_key_t);

  LogDebug(COMPONENT_DUPREQ, "NFS DUPREQ: Garbage collection on xid=%u", pdupreq->xid);

  return _remove_dupreq(&buffkey, pdupreq, dupreq_pool, NFS_REQ_OK);
}                               /* clean_entry_dupreq */

/**
 *
 *  dupreq_rbt_hash_func: computes the hash value for the entry in dupreq cache.
 * 
 * Computes the hash value for the entry in dupreq cache. In fact, it just use the Xid modulo the hash array size.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed hash value.
 *
 * @see HashTable_Init
 *
 */
unsigned long dupreq_value_hash_func(hash_parameter_t * p_hparam,
                                     hash_buffer_t * buffclef)
{
  dupreq_key_t *key = (dupreq_key_t *)(buffclef->pdata);
  unsigned long addr_hash = 0;
  int port;

  if(key->addr.sa_family == AF_INET)
    {
      addr_hash += ((struct sockaddr_in *)&key->addr)->sin_addr.s_addr;
      port = ((struct sockaddr_in *)&key->addr)->sin_port;
      addr_hash ^= (port<<16);
    }
#ifdef _USE_TIRPC_IPV6
  else if(key->addr.sa_family == AF_INET)
    {
      //      addr_hash += ((struct sockaddr_in6 *)key->addr)->sin6_addr.s6_addr;
      //      addr_hash += ((struct sockaddr_in6 *)key->addr)->sin6_port;
    }
#endif                          /* _USE_TIRPC_IPV6 */
  else
      LogCrit(COMPONENT_DUPREQ, "NFS DUPREQ: Could not determine whether dupreq entry used a IPV4 or IPV6 address.");

  return (((unsigned long)key->xid + addr_hash)^(key->checksum)) % p_hparam->index_size;
}                               /*  dupreq_value_hash_func */

/**
 *
 *  dupreq_rbt_hash_func: computes the rbt value for the entry in dupreq cache.
 * 
 * Computes the rbt value for the entry in dupreq cache. In fact, it just use the Xid 
 * itself (which is an unsigned integer) as the rbt value.
 * This function is called internal in the HasTable_* function
 *
 * @param hparam [IN] hash table parameter.
 * @param buffcleff[in] pointer to the hash key buffer
 *
 * @return the computed rbt value.
 *
 * @see HashTable_Init
 *
 */
unsigned long dupreq_rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  dupreq_key_t *key = (dupreq_key_t *)(buffclef->pdata);
  unsigned long addr_hash = 0;
  int port;

  if(key->addr.sa_family == AF_INET)
    {
      addr_hash += ((struct sockaddr_in *)&key->addr)->sin_addr.s_addr;
      port = ((struct sockaddr_in *)&key->addr)->sin_port;
      addr_hash ^= (port<<16);
    }
#ifdef _USE_TIRPC_IPV6
  else if(key->addr.sa_family == AF_INET)
    {
      //      addr_hash += ((struct sockaddr_in6 *)key->addr)->sin6_addr.s6_addr;
      //      addr_hash += ((struct sockaddr_in6 *)key->addr)->sin6_port;
    }
#endif                          /* _USE_TIRPC_IPV6 */
  else
      LogCrit(COMPONENT_DUPREQ, "NFS DUPREQ: Could not determine whether dupreq entry used a IPV4 or IPV6 address.");

  return ((unsigned long)key->xid + addr_hash)^(key->checksum);
}                               /* dupreq_rbt_hash_func */

/**
 *
 * compare_req: compares the xid, ip, and port stored in the key buffers.
 * may also compare the checksum if the relevant option was enabled during
 * compile.
 *
 * compare the xid, ip, and port stored in the key buffers. This function is
 * to be used as 'compare_key' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN] first key
 * @param buff2 [IN] second key
 *
 * @return 0 if keys are identifical, 1 if they are different. 
 *
 */
int compare_req(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  dupreq_key_t *key1 = (dupreq_key_t *)(buff1->pdata);
  dupreq_key_t *key2 = (dupreq_key_t *)(buff2->pdata);

  if (key1->xid != key2->xid)
    return 1;
  if (cmp_sockaddr(&key1->addr, &key2->addr) == 0)
    return 1;
  if (key1->checksum != key2->checksum)
    return 1;
  return 0;
}                               /* compare_xid */

/**
 *
 * display_xid: displays the xid stored in the buffer.
 *
 * displays the xid stored in the buffer. This function is to be used as 'key_to_str' field in 
 * the hashtable storing the nfs duplicated requests. 
 *
 * @param buff1 [IN]  buffer to display
 * @param buff2 [OUT] output string
 *
 * @return number of character written.
 *
 */
int display_xid(hash_buffer_t * pbuff, char *str)
{
  long xid = (long)(pbuff->pdata);

  return sprintf(str, "%lX", xid);
}

/**
 *
 * nfs_Init_dupreq: Init the hashtable and LRU for duplicate request cache
 *
 * Perform all the required initialization for hashtable and LRU for duplicate request cache
 * 
 * @param param [IN] parameter used to init the duplicate request cache
 *
 * @return 0 if successful, -1 otherwise
 *
 */
int nfs_Init_dupreq(nfs_rpc_dupreq_parameter_t param)
{
  if((ht_dupreq = HashTable_Init(param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_DUPREQ, "NFS DUPREQ: Cannot init the duplicate request hash table");
      return -1;
    }

  return DUPREQ_SUCCESS;
}                               /* nfs_Init_dupreq */

/**
 *
 * nfs_dupreq_add_not_finished: adds an entry in the duplicate requests cache.
 *
 * Adds an entry in the duplicate requests cache.
 *
 * @param xid [IN] the transfer id to be used as key
 * @param pnfsreq [IN] the request pointer to cache
 *
 * @return DUPREQ_SUCCESS if successfull\n.
 * @return DUPREQ_INSERT_MALLOC_ERROR if an error occured during the insertion process.
 *
 */

int nfs_dupreq_add_not_finished(long xid,
				struct svc_req *ptr_req,
				SVCXPRT *xprt,
				struct prealloc_pool *dupreq_pool,
				nfs_res_t *res_nfs)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  hash_buffer_t buffdata;
  dupreq_entry_t *pdupreq = NULL;
  int status;

  struct sockaddr_in *phostaddr;
  dupreq_key_t *pdupkey = NULL;

  /* Entry to be cached */
  GetFromPool(pdupreq, dupreq_pool, dupreq_entry_t);
  if(pdupreq == NULL)
    return DUPREQ_INSERT_MALLOC_ERROR;

  if((pdupkey = (dupreq_key_t *) Mem_Alloc(sizeof(dupreq_key_t))) == NULL)
    return DUPREQ_INSERT_MALLOC_ERROR;

  /* Get the socket address for the key */
  phostaddr = svc_getcaller(xprt);
  memcpy((char *)&pdupkey->addr, (char *)phostaddr,
	 sizeof(pdupkey->addr));

  pdupkey->xid = xid;

  /* Checksum the request */
  pdupkey->checksum = 0;

  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) pdupkey;
  buffkey.len = sizeof(dupreq_key_t);

  /* I build the data with the request pointer that should be in state 'IN USE' */
  pdupreq->xid = pdupkey->xid;
  memcpy((char *)&pdupreq->addr, (char *)&pdupkey->addr,
	sizeof(pdupkey->addr));
  pdupreq->checksum = pdupkey->checksum;
  pdupreq->rq_prog = ptr_req->rq_prog;
  pdupreq->rq_vers = ptr_req->rq_vers;
  pdupreq->rq_proc = ptr_req->rq_proc;
  pdupreq->timestamp = time(NULL);
  pdupreq->processing = 1;
  buffdata.pdata = (caddr_t) pdupreq;
  buffdata.len = sizeof(dupreq_entry_t);

  LogFullDebug(COMPONENT_DUPREQ, "NFS_DUPREQ: TEST_AND_SET ip= %d.%d.%d.%d port=%d xid=%u rq_prog=%d",
               (ntohl(phostaddr->sin_addr.s_addr) & 0xFF000000) >> 24,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x00FF0000) >> 16,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x0000FF00) >> 8,
               (ntohl(phostaddr->sin_addr.s_addr) & 0x000000FF),
               phostaddr->sin_port,
               xid,
               pdupreq->rq_prog);

  status = HashTable_Test_And_Set(ht_dupreq, &buffkey, &buffdata,
				  HASHTABLE_SET_HOW_SET_NO_OVERWRITE);

  if (status == HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      if(HashTable_Get(ht_dupreq, &buffkey, &buffval) == HASHTABLE_SUCCESS)
	{
	  P(((dupreq_entry_t *)buffval.pdata)->dupreq_mutex);
	  if ( ((dupreq_entry_t *)buffval.pdata)->processing == 1)
	    { 
	      V(((dupreq_entry_t *)buffval.pdata)->dupreq_mutex);
	      status = DUPREQ_BEING_PROCESSED;
	    }
	  else
	    {
	      *res_nfs = ((dupreq_entry_t *) buffval.pdata)->res_nfs;
	      V(((dupreq_entry_t *)buffval.pdata)->dupreq_mutex);
              status = DUPREQ_ALREADY_EXISTS;
	    }
	}
      else
	status = DUPREQ_NOT_FOUND;      
    }
  else if (status == HASHTABLE_INSERT_MALLOC_ERROR)
      status = DUPREQ_INSERT_MALLOC_ERROR;
  else
    status = DUPREQ_SUCCESS;
  if (status != DUPREQ_SUCCESS)
    ReleaseToPool(pdupreq, dupreq_pool);
  return status;
}                               /* nfs_dupreq_add_not_finished */

/**
 *
 * nfs_dupreq_finish: Changes the being_processed flag in a dupreq to 0 and
 * adds the reply info to the buffval.
 *
 * Changes the being_processed flag in a dupreq to 0 and adds the reply info
 * to the buffval. Used after the duplicate request has already been added to
 * the dupreq cache but has not been fully processed yet.
 *
 * @param xid [IN] the transfer id to be used as key
 * @param pnfsreq [IN] the request pointer to cache
 *
 * @return DUPREQ_SUCCESS if successfull\n.
 * @return DUPREQ_INSERT_MALLOC_ERROR if an error occured during the insertion process.
 *
 */

int nfs_dupreq_finish(long xid,
		      struct svc_req *ptr_req,
		      SVCXPRT *xprt,
		      nfs_res_t * p_res_nfs,
		      LRU_list_t * lru_dupreq)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  nfs_res_t res_nfs;
  LRU_entry_t *pentry = NULL;
  LRU_status_t lru_status;

  struct sockaddr_in *phostaddr;
  dupreq_key_t pdupkey;

  /* Get the socket address for the key */
  phostaddr = svc_getcaller(xprt);
  memcpy((char *)&pdupkey.addr, (char *)phostaddr,
	 sizeof(pdupkey.addr));
  pdupkey.xid = xid;
  /* Checksum the request */
  pdupkey.checksum = 0;
  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) &pdupkey;
  buffkey.len = sizeof(dupreq_key_t);
  if(HashTable_Get(ht_dupreq, &buffkey, &buffval) != HASHTABLE_SUCCESS)
    return DUPREQ_NOT_FOUND;

  LogDebug(COMPONENT_DUPREQ, "NFS DUPREQ: Hit in the dupreq cache for xid=%u", xid);

  P(((dupreq_entry_t *)buffval.pdata)->dupreq_mutex);
  ((dupreq_entry_t *)buffval.pdata)->res_nfs = *p_res_nfs;
  ((dupreq_entry_t *)buffval.pdata)->timestamp = time(NULL);
  ((dupreq_entry_t *)buffval.pdata)->processing = 0;
  V(((dupreq_entry_t *)buffval.pdata)->dupreq_mutex);

  /* Add it to lru list */
  if((pentry = LRU_new_entry(lru_dupreq, &lru_status)) == NULL)
    return DUPREQ_INSERT_MALLOC_ERROR;
  pentry->buffdata.pdata = buffval.pdata;
  pentry->buffdata.len = buffval.len;

  return DUPREQ_SUCCESS;
}                               /* nfs_dupreq_finish */

/**
 *
 * nfs_dupreq_get: Tries to get a duplicated requests for dupreq cache
 *
 * Tries to get a duplicated requests for dupreq cache. 
 * 
 * @param xid [IN] the transfer id we are looking for
 * @param pstatus [OUT] the pointer to the status for the operation
 *
 * @return the result previously set if *pstatus == DUPREQ_SUCCESS
 *
 */
nfs_res_t nfs_dupreq_get(long xid, struct svc_req *ptr_req, SVCXPRT *xprt, int *pstatus)
{
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  nfs_res_t res_nfs;

  struct sockaddr_in *phostaddr;
  dupreq_key_t pdupkey;

  /* Get the socket address for the key */
  phostaddr = svc_getcaller(xprt);
  memcpy((char *)&pdupkey.addr, (char *)phostaddr,
	 sizeof(pdupkey.addr));
  pdupkey.xid = xid;
  /* Checksum the request */
  pdupkey.checksum = 0;
  /* I have to keep an integer as key, I wil use the pointer buffkey->pdata for this, 
   * this also means that buffkey->len will be 0 */
  buffkey.pdata = (caddr_t) &pdupkey;
  buffkey.len = sizeof(dupreq_key_t);
  if(HashTable_Get(ht_dupreq, &buffkey, &buffval) == HASHTABLE_SUCCESS)
    {
      /* reset timestamp */
      ((dupreq_entry_t *) buffval.pdata)->timestamp = time(NULL);

      *pstatus = DUPREQ_SUCCESS;
      res_nfs = ((dupreq_entry_t *) buffval.pdata)->res_nfs;
      LogDebug(COMPONENT_DUPREQ, "NFS DUPREQ: Hit in the dupreq cache for xid=%ld", xid);
    }
  else {
    LogFullDebug(COMPONENT_DUPREQ, "Failed to get dupreq entry: ip= %d.%d.%d.%d port=%d xid=%u",
                 (ntohl(phostaddr->sin_addr.s_addr) & 0xFF000000) >> 24,
                 (ntohl(phostaddr->sin_addr.s_addr) & 0x00FF0000) >> 16,
                 (ntohl(phostaddr->sin_addr.s_addr) & 0x0000FF00) >> 8,
                 (ntohl(phostaddr->sin_addr.s_addr) & 0x000000FF),
                 phostaddr->sin_port,
                 xid);
      *pstatus = DUPREQ_NOT_FOUND;
  }
  return res_nfs;
}                               /* nfs_dupreq_get */

/**
 *
 * nfs_dupreq_gc_function: Tests is an entry in dupreq cache is to be set invalid (has expired).
 *
 * Tests is an entry in dupreq cache is to be set invalid (has expired).
 *
 * @param pentry [IN] pointer to the entry to test
 *
 * @return 1 if entry must be set invalid, 0 if not.
 *
 * @see LRU_invalidate_by_function
 * @see LRU_gc_invalid
 *
 */
int nfs_dupreq_gc_function(LRU_entry_t * pentry, void *addparam)
{
  dupreq_entry_t *pdupreq = NULL;

  pdupreq = (dupreq_entry_t *) (pentry->buffdata.pdata);

  /* Test if entry is expired */
  if(time(NULL) - pdupreq->timestamp > nfs_param.core_param.expiration_dupreq)
    return LRU_LIST_SET_INVALID;

  return LRU_LIST_DO_NOT_SET_INVALID;
}                               /* nfs_dupreq_fc_function */

/**
 *
 * nfs_dupreq_get_stats: gets the hash table statistics for the duplicate requests.
 *
 * Gets the hash table statistics for the duplicate requests.
 *
 * @param phstat [OUT] pointer to the resulting stats.
 *
 * @return nothing (void function)
 *
 * @see HashTable_GetStats
 *
 */
void nfs_dupreq_get_stats(hash_stat_t * phstat)
{
  HashTable_GetStats(ht_dupreq, phstat);
}                               /* nfs_dupreq_get_stats */
