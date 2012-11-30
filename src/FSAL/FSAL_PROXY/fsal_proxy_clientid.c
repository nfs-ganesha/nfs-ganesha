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
#include <arpa/inet.h>
#include <pthread.h>
#include "nfs4.h"

#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"

#include "nfs_proto_functions.h"
#include "fsal_nfsv4_macros.h"

extern time_t ServerBootTime;

clientid4 fsal_clientid;
time_t     clientid_renewed ;
pthread_mutex_t fsal_clientid_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t fsal_clientid_mutex_renew = PTHREAD_MUTEX_INITIALIZER;
unsigned int done = 0;

/**
 * FSAL_proxy_setclientid_force:
 * Client ID negociation 
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
static fsal_status_t FSAL_proxy_setclientid_force(proxyfsal_op_context_t * p_context)
{
  int rc;
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;

#define FSAL_CLIENTID_NB_OP_ALLOC 1
  nfs_argop4 argoparray[FSAL_CLIENTID_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_CLIENTID_NB_OP_ALLOC];

  nfs_client_id4 nfsclientid;
  cb_client4 cbproxy;
  char clientid_name[MAXNAMLEN+1];
  char cbaddr[MAXNAMLEN+1];
  char cbnetid[MAXNAMLEN+1];
  struct timeval timeout = TIMEOUTRPC;
  int fd;
  struct sockaddr_in sin;
  socklen_t l = sizeof(sin);

  LogEvent( COMPONENT_FSAL, "Negotiating a new ClientId with the remote server" ) ;

  /* sanity checks.
   */
  if(!p_context)
    ReturnCode(ERR_FSAL_FAULT, 0);
  if(!CLNT_CONTROL(p_context->rpc_client, CLGET_FD, &fd))
    ReturnCode(ERR_FSAL_FAULT, EBADF);

  if(getsockname(fd, &sin, &l))
    ReturnCode(ERR_FSAL_FAULT, errno);

  /* Client id negociation is to be done only one time for the whole FSAL */
  P(fsal_clientid_mutex_renew);

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  snprintf(clientid_name, MAXNAMLEN, "%s(%d) - GANESHA NFSv4 Proxy",
           inet_ntop(AF_INET, &sin.sin_addr, cbaddr, sizeof(cbaddr)),
           getpid());

  nfsclientid.id.id_len = strlen(clientid_name);
  nfsclientid.id.id_val = clientid_name;
  snprintf(nfsclientid.verifier, NFS4_VERIFIER_SIZE, "%x", (int)ServerBootTime);

  cbproxy.cb_program = 0;
  strncpy(cbnetid, "tcp", MAXNAMLEN);
  strncpy(cbaddr, "127.0.0.1", MAXNAMLEN);
  cbproxy.cb_location.r_netid = cbnetid;
  cbproxy.cb_location.r_addr = cbaddr;

  COMPOUNDV4_ARG_ADD_OP_SETCLIENTID(argnfs4, nfsclientid, cbproxy);

  TakeTokenFSCall();

  p_context->credential.user = 0;
  p_context->credential.group = 0;
  p_context->credential.nbgroups = 0;

  /* Call the NFSv4 function */
  rc = COMPOUNDV4_EXECUTE_SIMPLE(p_context, argnfs4, resnfs4);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      V(fsal_clientid_mutex_renew);

      ReturnCode(ERR_FSAL_IO, rc);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    {
      V(fsal_clientid_mutex_renew);
      return fsal_internal_proxy_error_convert(resnfs4.status,
                                               INDEX_FSAL_InitClientContext);
    }

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
  TakeTokenFSCall();
  rc = COMPOUNDV4_EXECUTE_SIMPLE(p_context, argnfs4, resnfs4);
  if(rc != RPC_SUCCESS)
    {
      ReleaseTokenFSCall();

      V(fsal_clientid_mutex_renew);

      ReturnCode(ERR_FSAL_IO, rc);
    }

  ReleaseTokenFSCall();

  if(resnfs4.status != NFS4_OK)
    {
      V(fsal_clientid_mutex_renew);
      return fsal_internal_proxy_error_convert(resnfs4.status,
                                               INDEX_FSAL_InitClientContext);
    }

  /* Keep the confirmed client id */
  fsal_clientid =
      argnfs4.argarray.argarray_val[0].nfs_argop4_u.opsetclientid_confirm.clientid;
  clientid_renewed = time( NULL ) ;

  V(fsal_clientid_mutex_renew);

  p_context->clientid = fsal_clientid;
  p_context->last_lease_renewal = 0;    /* Needs to be renewed */

  ReturnCode(ERR_FSAL_NO_ERROR, 0);
}                               /* FSAL_proxy_setclientid_force */

/**
 * FSAL_proxy_setclientid_renego:
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
fsal_status_t FSAL_proxy_setclientid_renego(proxyfsal_op_context_t * p_context)
{
  time_t now = time( NULL ) ;

  /* The first to come is the only one to do the clientid renegociation */
  if( ( p_context->clientid_renewed <  now ) && (p_context->clientid == fsal_clientid ) )
    return FSAL_proxy_setclientid_force( p_context ) ;
  else
   {
	p_context->clientid = fsal_clientid ;
	p_context->clientid_renewed = clientid_renewed ;
        
	Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);
   }
} /* FSAL_proxy_setclientid_renego */

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
  fsal_status_t fsal_status ;

  /* sanity checks.
   */
  if(!p_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  P(fsal_clientid_mutex);
  if(done == 0)
    {
      fsal_status = FSAL_proxy_setclientid_force( p_context ) ;

      done = 1 ;
      V(fsal_clientid_mutex);

      p_context->clientid = fsal_clientid;
      p_context->last_lease_renewal = 0;    /* Needs to be renewed */

      return fsal_status;
    }
  V(fsal_clientid_mutex);

  p_context->clientid = fsal_clientid;
  p_context->clientid_renewed = clientid_renewed ;
  p_context->last_lease_renewal = 0;    /* Needs to be renewed */
  
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);
}                               /* FSAL_proxy_setclientid */

/**
 * FSAL_proxy_clientid_renewer_thread: this thread is made for refreshing the clientid used by the FSAL, automatically.
 *
 *  This thread is made for refreshing the clientid used by the FSAL, automatically.
 *
 *
 * \return never returns... This is a infinite loop that will die when the daemon stops
 */
void *FSAL_proxy_clientid_renewer_thread(void *Arg)
{
  int rc;

  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = TIMEOUTRPC;
  fsal_status_t fsal_status;
  proxyfsal_op_context_t fsal_context;
  proxyfsal_op_context_t *p_context = &fsal_context;
#define FSAL_RENEW_LEASE_NB_OP_ALLOC 1
  nfs_argop4 argoparray[FSAL_RENEW_LEASE_NB_OP_ALLOC];
  nfs_resop4 resoparray[FSAL_RENEW_LEASE_NB_OP_ALLOC];

  LogEvent(COMPONENT_FSAL, "FSAL_proxy_clientid_refresher_thread: starting...");

  sleep(6);    /** @todo: use getattr to have an actual value of server's lease duration */

  memset(&fsal_context, 0, sizeof(proxyfsal_op_context_t));
  fsal_status = PROXYFSAL_InitClientContext((fsal_op_context_t *)p_context);

  if(FSAL_IS_ERROR(fsal_status))
    {
      LogCrit(COMPONENT_FSAL,
           "FSAL_proxy_clientid_refresher_thread: FSAL error(%u,%u) during init... exiting",
           fsal_status.major, fsal_status.minor);
      exit(1);
    }

  /* Setup results structures */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 0;
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  argnfs4.argarray.argarray_val[0].argop = NFS4_OP_RENEW;
  argnfs4.argarray.argarray_len = 1;

  while(1)
    {
      sleep(60);  /** @todo: use getattr to have an actual value of server's lease duration */

      /* Call the NFSv4 function */
      TakeTokenFSCall();

      argoparray[0].nfs_argop4_u.oprenew.clientid = fsal_clientid;
      COMPOUNDV4_EXECUTE(p_context, argnfs4, resnfs4, rc);
      if(rc != RPC_SUCCESS)
        {
          ReleaseTokenFSCall();

          LogCrit(COMPONENT_FSAL, "FSAL_PROXY: /!\\ RPC error when connecting to the server");

        }

      ReleaseTokenFSCall();

      if(resnfs4.status != NFS4_OK)
        LogCrit(COMPONENT_FSAL,
                "FSAL_PROXY: /!\\ NFSv4 error %u occured when trying to renew client id %16llx",
                resnfs4.status, (long long)argoparray[0].nfs_argop4_u.oprenew.clientid);

    }                           /* while( 1 ) */
}                               /* FSAL_proxy_clientid_renewer_thread */
