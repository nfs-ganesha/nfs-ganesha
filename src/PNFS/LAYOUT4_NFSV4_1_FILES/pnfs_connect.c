/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_init.c
 * \brief   Initialization functions.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <string.h>
#include <signal.h>

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#include "log_macros.h"

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"

/**
 *
 * pnfs_connect: create a TCP connection to a pnfs data server.
 *
 * Create a TCP connection to a pnfs data server.
 *
 * @param pnfsdsclient   [INOUT] pointer to the pnfs client ds structure (client to the ds).
 * @param pnfs_ds_param  [IN]    pointer to pnfs layoutfile ds configuration
 *
 * @return 0 if successful
 * @return -1 if failed
 *
 */
int pnfs_connect(pnfs_ds_client_t * pnfsdsclient, pnfs_ds_parameter_t * pnfs_ds_param)
{
  int sock;
  struct sockaddr_in addr_rpc;

  if(!pnfsdsclient || !pnfs_ds_param)
    return -1;

  memset(&addr_rpc, 0, sizeof(addr_rpc));
  addr_rpc.sin_port = pnfs_ds_param->ipport;
  addr_rpc.sin_family = AF_INET;
  addr_rpc.sin_addr.s_addr = pnfs_ds_param->ipaddr;

  if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
      LogCrit(COMPONENT_PNFS,"PNFS_LAYOUT INIT: cannot create a tcp socket");
      return -1;
    }

  if(connect(sock, (struct sockaddr *)&addr_rpc, sizeof(addr_rpc)) < 0)
    {
      LogCrit(COMPONENT_PNFS,"pNFS_LAYOUT INIT : Cannot connect to server addr=%u.%u.%u.%u port=%u",
                 (ntohl(pnfs_ds_param->ipaddr) & 0xFF000000) >> 24,
                 (ntohl(pnfs_ds_param->ipaddr) & 0x00FF0000) >> 16,
                 (ntohl(pnfs_ds_param->ipaddr) & 0x0000FF00) >> 8,
                 (ntohl(pnfs_ds_param->ipaddr) & 0x000000FF),
                 ntohs(pnfs_ds_param->ipport));
      return -1;
    }

  if((pnfsdsclient->rpc_client = clnttcp_create(&addr_rpc,
                                                pnfs_ds_param->prognum,
                                                PNFS_NFS4,
                                                &sock,
                                                PNFS_SENDSIZE, PNFS_RECVSIZE)) == NULL)
    {
      LogCrit(COMPONENT_PNFS,
          "PNFS_LAYOUT INIT : Cannot contact server addr=%x.%x.%x.%x port=%u prognum=%u using NFSv4 protocol",
           (ntohl(pnfs_ds_param->ipaddr) & 0xFF000000) >> 24,
           (ntohl(pnfs_ds_param->ipaddr) & 0x00FF0000) >> 16,
           (ntohl(pnfs_ds_param->ipaddr) & 0x0000FF00) >> 8,
           (ntohl(pnfs_ds_param->ipaddr) & 0x000000FF),
           ntohs(pnfs_ds_param->ipport), pnfs_ds_param->prognum);

      return -1;
    }

  if((pnfsdsclient->rpc_client->cl_auth = authunix_create_default()) == NULL)
    {
      return -1;
    }

  return 0;
}                               /* pnfs_connect */
