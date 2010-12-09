/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_unlink.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:37 $
 * \version $Revision: 1.9 $
 * \brief   object removing function.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/xdr.h>
#else
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#endif
#include <pthread.h>
#include "nfs4.h"

#include "BuddyMalloc.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

extern time_t ServerBootTime;

#ifndef _NO_BUDDY_SYSTEM
extern buddy_parameter_t default_buddy_parameter;
#endif

clientid4 fsal_clientid;
pthread_mutex_t fsal_clientid_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * FSAL_proxy_setclientid:
 * Client ID negociation, step 1
 *
 * \param p_context (input):
 *        Authentication context for the operation (user,...).
 *
 * \return Major error codes :
 *        - ERR_FSAL_NO_ERROR     (no error)
 *        - ERR_FSAL_FAULT        (a NULL pointer was passed as mandatory argument)
 *        - Other error codes can be returned :
 *          ERR_FSAL_ACCESS, ERR_FSAL_IO, ...
 */
fsal_status_t FSAL_proxy_setclientid(proxyfsal_op_context_t * p_context)
{
  int rc;
  fsal_status_t fsal_status;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;

#define FSAL_CLIENTID_NB_OP_ALLOC 1
  nfs_argop4 argoparray[FSAL_CLIENTID_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_CLIENTID_NB_OP_ALLOC];

  nfs_client_id4 nfsclientid;
  cb_client4 cbproxy;
  char clientid_name[MAXNAMLEN];
  char cbaddr[MAXNAMLEN];
  char cbnetid[MAXNAMLEN];
  clientid4 resultclientid;
  struct timeval __attribute__ ((__unused__)) timeout = TIMEOUTRPC;

  /* sanity checks.
   */
  if(!p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* Client id negociation is to be done only one time for the whole FSAL */

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  snprintf(clientid_name, MAXNAMLEN, "GANESHA NFSv4 Proxy Pid=%u", getpid());
  nfsclientid.id.id_len = strlen(clientid_name);
  nfsclientid.id.id_val = clientid_name;
  snprintf(nfsclientid.verifier, NFS4_VERIFIER_SIZE, "%x", (int)ServerBootTime);

  cbproxy.cb_program = 0;
  strncpy(cbnetid, "tcp", MAXNAMLEN);
  strncpy(cbaddr, "127.0.0.1", MAXNAMLEN);
#ifdef _USE_NFS4_1
  cbproxy.cb_location.na_r_netid = cbnetid;
  cbproxy.cb_location.na_r_addr = cbaddr;
#else
  cbproxy.cb_location.r_netid = cbnetid;
  cbproxy.cb_location.r_addr = cbaddr;
#endif

  COMPOUNDV4_ARG_ADD_OP_SETCLIENTID(argnfs4, nfsclientid, cbproxy);

  TakeTokenFSCall();

  p_context->user_credential.user = 0;
  p_context->user_credential.group = 0;
  p_context->user_credential.nbgroups = 0;

  /* Call the NFSv4 function */
  rc = COMPOUNDV4_EXECUTE_SIMPLE(p_context, argnfs4, resnfs4 );
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      V(fsal_clientid_mutex);

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_unlink);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    {
      V(fsal_clientid_mutex);
      return fsal_internal_proxy_error_convert(resnfs4.status,
                                                   INDEX_FSAL_InitClientContext);
    }

  resultclientid =
          resnfs4.resarray.resarray_val[0].nfs_resop4_u.opsetclientid.SETCLIENTID4res_u.
          resok4.clientid;

  /* Step 2: Confirm the client id */
  argnfs4.minorversion = 0;
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  argnfs4.argarray.argarray_val[0].argop = NFS4_OP_SETCLIENTID_CONFIRM;
  argnfs4.argarray.argarray_val[0].nfs_argop4_u.opsetclientid_confirm.clientid =
      resnfs4.resarray.resarray_val[0].nfs_resop4_u.opsetclientid.SETCLIENTID4res_u.
      resok4.clientid;
  memcpy((char *)argnfs4.argarray.argarray_val[0].nfs_argop4_u.opsetclientid_confirm.
             setclientid_confirm,
             (char *)resnfs4.resarray.resarray_val[0].nfs_resop4_u.opsetclientid.
             SETCLIENTID4res_u.resok4.setclientid_confirm, NFS4_VERIFIER_SIZE);
  argnfs4.argarray.argarray_len = 1;

  /* Call the NFSv4 function */
  rc = COMPOUNDV4_EXECUTE_SIMPLE(p_context, argnfs4, resnfs4);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      V(fsal_clientid_mutex);

      Return(ERR_FSAL_IO, rc, INDEX_FSAL_unlink);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    return fsal_internal_proxy_error_convert(resnfs4.status,
                                                 INDEX_FSAL_InitClientContext);

  /* Keep the confirmed client id */
  fsal_clientid =
          argnfs4.argarray.argarray_val[0].nfs_argop4_u.opsetclientid_confirm.clientid;

  V(fsal_clientid_mutex);

  p_context->clientid = fsal_clientid;
  p_context->last_lease_renewal = 0;    /* Needs to be renewed */

  fsal_status.major = ERR_FSAL_NO_ERROR;
  fsal_status.minor = 0;

  return fsal_status;
}                               /* FSAL_proxy_setclientid */

