/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    fsal_creds.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/24 13:45:36 $
 * \version $Revision: 1.15 $
 * \brief   FSAL credentials handling functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/clnt.h>
#include <gssrpc/auth.h>
#else
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/clnt.h>
#include <rpc/auth.h>
#endif
#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_common.h"
#include "fsal_nfsv4_macros.h"
#include <pwd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h> /* For rresvport */

extern proxyfs_specific_initinfo_t global_fsal_proxy_specific_info;

/** usefull subopt definitions */

/* define your specific NFS export options here : */
enum
{
  YOUR_OPTION_1 = 0,
  YOUR_OPTION_2 = 1,
  YOUR_OPTION_3 = 2,
  YOUR_OPTION_4 = 3,
};

const char *fs_specific_opts[] = {
  "option1",
  "option2",
  "option3",
  "option4",
  NULL
};

/**
 * generous gift from GNU
 * so you can use it even on Cray, etc...
 */
static int Getsubopt(char **optionp, const char *const *tokens, char **valuep)
{
  char *endp, *vstart;
  int cnt;

  if(**optionp == '\0')
    return -1;

  /* Find end of next token.  */
  endp = strchr(*optionp, ',');
  if(endp == NULL)
    endp = strchr(*optionp, '\0');

  /* Find start of value.  */
  vstart = memchr(*optionp, '=', endp - *optionp);
  if(vstart == NULL)
    vstart = endp;

  /* Try to match the characters between *OPTIONP and VSTART against
     one of the TOKENS.  */
  for(cnt = 0; tokens[cnt] != NULL; ++cnt)
    if(memcmp(*optionp, tokens[cnt], vstart - *optionp) == 0
       && tokens[cnt][vstart - *optionp] == '\0')
      {
        /* We found the current option in TOKENS.  */
        *valuep = vstart != endp ? vstart + 1 : NULL;

        if(*endp != '\0')
          *endp++ = '\0';
        *optionp = endp;

        return cnt;
      }

  /* The current suboption does not match any option.  */
  *valuep = *optionp;

  if(*endp != '\0')
    *endp++ = '\0';
  *optionp = endp;

  return -1;
}

/**
 * @defgroup FSALCredFunctions Credential handling functions.
 *
 * Those functions handle security contexts (credentials).
 * 
 * @{
 */

/**
 * Parse FS specific option string
 * to build the export entry option.
 */
fsal_status_t PROXYFSAL_BuildExportContext(fsal_export_context_t * p_export_context,       /* OUT */
                                           fsal_path_t * p_export_path, /* IN */
                                           char *fs_specific_options    /* IN */
    )
{
  char subopts[256];
  char *p_subop;
  char *value;

  /* sanity check */
  if(!p_export_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_BuildExportContext);

  /* Save pointer to fsal_staticfsinfo_t in export context */
  p_export_context->fe_static_fs_info = &global_fs_info;

  if((fs_specific_options != NULL) && (fs_specific_options[0] != '\0'))
    {

      /* copy the option string (because it is modified by getsubopt call) */
      strncpy(subopts, fs_specific_options, 256);
      subopts[255] = '\0';
      p_subop = subopts;        /* set initial pointer */

      /* parse the FS specific option string */

      switch (Getsubopt(&p_subop, fs_specific_opts, &value))
        {
        case YOUR_OPTION_1:
          /* analyze your option 1 and fill the export_context structure */
          break;

        case YOUR_OPTION_2:
          /* analyze your option 2 and fill the export_context structure */
          break;

        case YOUR_OPTION_3:
          /* analyze your option 3 and fill the export_context structure */
          break;

        case YOUR_OPTION_4:
          /* analyze your option 4 and fill the export_context structure */
          break;

        default:
          {
            LogCrit(COMPONENT_FSAL,
                    "FSAL LOAD PARAMETER: ERROR: Invalid suboption found in EXPORT::FS_Specific : %s : xxxxxx expected.",
                    value);
            Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_BuildExportContext);
          }
        }

    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_BuildExportContext);
}

fsal_status_t PROXYFSAL_InitClientContext(fsal_op_context_t *context)
{

  int sock;
  struct sockaddr_in addr_rpc;
  struct timeval timeout = TIMEOUTRPC;
  int rc;
  int priv_port = 0 ; 
  fsal_status_t fsal_status;

#ifdef _USE_GSSRPC
  struct rpc_gss_sec rpcsec_gss_data;
  gss_OID mechOid;
  char mechname[1024];
  gss_buffer_desc mechgssbuff;
  OM_uint32 maj_stat, min_stat;
#endif
  proxyfsal_op_context_t * p_thr_context = (proxyfsal_op_context_t *)context;

  /* sanity check */
  if(!p_thr_context)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

  /* initialy set the export entry to none */
  p_thr_context->export_context = NULL;

  /* It is now time to initiate the rpc client within the thread's specific material */
  /* Keep here the reference to the server */
  p_thr_context->srv_prognum = global_fsal_proxy_specific_info.srv_prognum;
  p_thr_context->srv_addr = global_fsal_proxy_specific_info.srv_addr;
  p_thr_context->srv_port = global_fsal_proxy_specific_info.srv_port;
  p_thr_context->srv_sendsize = global_fsal_proxy_specific_info.srv_sendsize;
  p_thr_context->srv_recvsize = global_fsal_proxy_specific_info.srv_recvsize;
  p_thr_context->use_privileged_client_port = global_fsal_proxy_specific_info.use_privileged_client_port;
  p_thr_context->retry_sleeptime = global_fsal_proxy_specific_info.retry_sleeptime;
  p_thr_context->file_counter = 0LL;
  strncpy(p_thr_context->srv_proto, global_fsal_proxy_specific_info.srv_proto, MAXNAMLEN);
  pthread_mutex_init(&p_thr_context->lock, NULL);

  memset(&addr_rpc, 0, sizeof(addr_rpc));
  addr_rpc.sin_port = p_thr_context->srv_port;
  addr_rpc.sin_family = AF_INET;
  addr_rpc.sin_addr.s_addr = p_thr_context->srv_addr;

  if(!strcmp(p_thr_context->srv_proto, "udp"))
    {
      if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        Return(ERR_FSAL_FAULT, errno, INDEX_FSAL_InitClientContext);

      if((p_thr_context->rpc_client = clntudp_bufcreate(&addr_rpc,
                                                        p_thr_context->srv_prognum,
                                                        FSAL_PROXY_NFS_V4,
                                                        (struct timeval)
                                                        {
                                                        25, 0},
                                                        &sock,
                                                        p_thr_context->srv_sendsize,
                                                        p_thr_context->srv_recvsize)) ==
         NULL)
        {

          LogCrit(COMPONENT_FSAL,
                  "FSAL INIT : Cannot contact server addr=%u.%u.%u.%u port=%u prognum=%u using NFSv4 protocol",
                  (ntohl(p_thr_context->srv_addr) & 0xFF000000) >> 24,
                  (ntohl(p_thr_context->srv_addr) & 0x00FF0000) >> 16,
                  (ntohl(p_thr_context->srv_addr) & 0x0000FF00) >> 8,
                  (ntohl(p_thr_context->srv_addr) & 0x000000FF),
                  ntohs(p_thr_context->srv_port), p_thr_context->srv_prognum);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_InitClientContext);
        }
    }
  else if(!strcmp(p_thr_context->srv_proto, "tcp"))
    {
      if( p_thr_context->use_privileged_client_port  == TRUE )
        {
	  if( (sock = rresvport( &priv_port ) )< 0 )
           {
             LogCrit(COMPONENT_FSAL, "FSAL_INIT: cannot create a tcp socket on a privileged port");
             Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);
           }
        }
     else
       {
        if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
          {
            LogCrit(COMPONENT_FSAL, "FSAL_INIT: cannot create a tcp socket");
            Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);
          }
       }


    if(connect(sock, (struct sockaddr *)&addr_rpc, sizeof(addr_rpc)) < 0)
        {
          LogCrit(COMPONENT_FSAL,
                  "FSAL INIT : Cannot connect to server addr=%u.%u.%u.%u port=%u",
                  (ntohl(p_thr_context->srv_addr) & 0xFF000000) >> 24,
                  (ntohl(p_thr_context->srv_addr) & 0x00FF0000) >> 16,
                  (ntohl(p_thr_context->srv_addr) & 0x0000FF00) >> 8,
                  (ntohl(p_thr_context->srv_addr) & 0x000000FF),
                  ntohs(p_thr_context->srv_port));

          Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);
        }

      if((p_thr_context->rpc_client = clnttcp_create(&addr_rpc,
                                                     p_thr_context->srv_prognum,
                                                     FSAL_PROXY_NFS_V4,
                                                     &sock,
                                                     p_thr_context->srv_sendsize,
                                                     p_thr_context->srv_recvsize)) ==
         NULL)
        {
          LogCrit(COMPONENT_FSAL,
                  "FSAL INIT : Cannot contact server addr=%x.%x.%x.%x port=%u prognum=%u using NFSv4 protocol",
                  (ntohl(p_thr_context->srv_addr) & 0xFF000000) >> 24,
                  (ntohl(p_thr_context->srv_addr) & 0x00FF0000) >> 16,
                  (ntohl(p_thr_context->srv_addr) & 0x0000FF00) >> 8,
                  (ntohl(p_thr_context->srv_addr) & 0x000000FF),
                  ntohs(p_thr_context->srv_port), p_thr_context->srv_prognum);

          Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_InitClientContext);
        }
    }
  else
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_InitClientContext);
    }

#ifdef _USE_GSSRPC
  if(global_fsal_proxy_specific_info.active_krb5 == TRUE)
    {
      fsal_status = fsal_internal_set_auth_gss(p_thr_context);
      if(FSAL_IS_ERROR(fsal_status))
        Return(fsal_status.major, fsal_status.minor, INDEX_FSAL_InitClientContext);
    }
  else
#endif                          /* _USE_GSSRPC */
  if((p_thr_context->rpc_client->cl_auth = authunix_create_default()) == NULL)
    {
      Return(ERR_FSAL_INVAL, 0, INDEX_FSAL_InitClientContext);
    }

  /* test if the newly created context can 'ping' the server via PROC_NULL */
  if((rc = clnt_call(p_thr_context->rpc_client, NFSPROC4_NULL,
                     (xdrproc_t) xdr_void, (caddr_t) NULL,
                     (xdrproc_t) xdr_void, (caddr_t) NULL, timeout)) != RPC_SUCCESS)
    {
      Return(ERR_FSAL_INVAL, rc, INDEX_FSAL_InitClientContext);
    }

  fsal_status = FSAL_proxy_setclientid(p_thr_context);
  if(FSAL_IS_ERROR(fsal_status))
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);

#ifdef _BY_FILEID
  if(FSAL_proxy_set_hldir(p_thr_context, global_fsal_proxy_specific_info.openfh_wd) == -1)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_InitClientContext);
#endif

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_InitClientContext);

}

/* @} */
