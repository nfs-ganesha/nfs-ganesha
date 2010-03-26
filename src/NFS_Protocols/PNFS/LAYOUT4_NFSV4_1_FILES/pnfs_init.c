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

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"

int pnfs_init( pnfs_ds_parameter_t * pds_param, pnfs_client_t * pnfsclient )
{
  int sock ;
  struct sockaddr_in addr_rpc;

  if( !pnfsclient || !pds_param ) 
    return -1 ;

  memset(&addr_rpc, 0, sizeof(addr_rpc));
  addr_rpc.sin_port = pds_param->ipport ;
  addr_rpc.sin_family = AF_INET;
  addr_rpc.sin_addr.s_addr = pds_param->ipaddr ;

  if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
          DisplayLog("PNFS_LAYOUT INIT: cannot create a tcp socket");
	  return -1 ;
    }

  if (connect(sock, (struct sockaddr *)&addr_rpc, sizeof(addr_rpc)) < 0)
        {
          DisplayLog( "pNFS_LAYOUT INIT : Cannot connect to server addr=%u.%u.%u.%u port=%u",
                       (ntohl(pds_param->ipaddr) & 0xFF000000) >> 24,
                       (ntohl(pds_param->ipaddr) & 0x00FF0000) >> 16,
                       (ntohl(pds_param->ipaddr) & 0x0000FF00) >> 8,
                       (ntohl(pds_param->ipaddr) & 0x000000FF),
                       ntohs(pds_param->ipport));
           return -1 ;
        }

  if ((pnfsclient->rpc_client = clnttcp_create(&addr_rpc,
                                               pds_param->prognum,
                                               PNFS_NFS4,
                                               &sock,
                                               PNFS_SENDSIZE,
                                               PNFS_RECVSIZE ) ) == NULL )
        {
          DisplayLog( "PNFS_LAYOUT INIT : Cannot contact server addr=%x.%x.%x.%x port=%u prognum=%u using NFSv4 protocol",
                       (ntohl(pds_param->ipaddr) & 0xFF000000) >> 24,
                       (ntohl(pds_param->ipaddr) & 0x00FF0000) >> 16,
                       (ntohl(pds_param->ipaddr) & 0x0000FF00) >> 8,
                       (ntohl(pds_param->ipaddr) & 0x000000FF),
                       ntohs(pds_param->ipport), pds_param->prognum);

	  return -1 ;
        }

   return 0 ;
} /* pnfs_init */
