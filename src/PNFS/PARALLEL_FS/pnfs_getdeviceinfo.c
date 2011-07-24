/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_encode_getdeviceinfo.c
 * \brief   encode the addr_body_val structure in GETDEVICEINFO
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#include "stuff_alloc.h"
#include "rpc.h"
#include "log_macros.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "pnfs.h" 
#include "pnfs_service.h" 

/**
 *
 * pnfs_parallel_fs_getdeviceinfo: manages the OP4_GETDEVICEINFO operation for pNFS/File on top of PARALLEL_FS
 *
 * Manages the OP4_GETDEVICEINFOT operation for pNFS/File on top of PARALLEL_FS
 *
 * @param pgetdeviceinfoargs [IN]  pointer to getdeviceinfo's arguments
 * @param data           [INOUT]  pointer to related compoud request
 * @param pgetdeviceinfores [OUT] pointer to getdeviceinfot's results
 *
 * @return  NFSv4 status (with NFSv4 error code)
 *
 */

nfsstat4 pnfs_parallel_fs_getdeviceinfo( GETDEVICEINFO4args  * pgetdeviceinfoargs,
			            compound_data_t     * data,
				    GETDEVICEINFO4res   * pgetdeviceinfores )
{
  unsigned int offset = 0;
  uint32_t int32 = 0;
  char tmpchar[MAXNAMLEN];
  unsigned int tmplen = 0;
  unsigned int padlen = 0;
  unsigned int i = 0;

  char *buff = NULL ;

  buff = (char *)pgetdeviceinfores->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_val ;

  /* nflda_stripe_indices.nflda_stripe_indices_len */
  int32 = htonl(nfs_param.pnfs_param.layoutfile.stripe_width);
  memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
  offset += sizeof(int32);

  for(i = 0; i < nfs_param.pnfs_param.layoutfile.stripe_width; i++)
    {
      /* nflda_stripe_indices.nflda_stripe_indices_val */
      int32 = htonl(i);
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);
    }

  /* nflda_multipath_ds_list.nflda_multipath_ds_list_len */
  int32 = htonl(nfs_param.pnfs_param.layoutfile.stripe_width);
  memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
  offset += sizeof(int32);

  for(i = 0; i < nfs_param.pnfs_param.layoutfile.stripe_width; i++)
    {
      /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[i].multipath_list4_len */
      int32 = htonl(1);
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);

      /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[i].multipath_list4_val[0].na_r_netid */
      int32 = htonl(3);         /* because strlen( "tcp" ) = 3 */
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);

      memset(tmpchar, 0, MAXNAMLEN);
      strncpy(tmpchar, "tcp", MAXNAMLEN);
      memcpy((char *)(buff + offset), tmpchar, 4);      /* 4 bytes = 3 bytes for "tcp" and 1 to keep XDR alignment */
      offset += 4;

      /* nflda_multipath_ds_list.nflda_multipath_ds_list_val[i].multipath_list4_val[0].na_r_addr */
      memset(tmpchar, 0, MAXNAMLEN);
      snprintf(tmpchar, MAXNAMLEN, "%s.%u.%u",
               nfs_param.pnfs_param.layoutfile.ds_param[i].ipaddr_ascii,
               nfs_param.pnfs_param.layoutfile.ds_param[i].ipport & 0x0F,
               nfs_param.pnfs_param.layoutfile.ds_param[i].ipport >> 8);
      tmplen = strnlen(tmpchar, MAXNAMLEN);

      /* XDR padding : keep stuff aligned on 32 bits pattern */
      if(tmplen % 4 == 0)
        padlen = 0;
      else
        padlen = 4 - (tmplen % 4);

      /* len of na_r_addr */
      int32 = htonl(tmplen);
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);

      memcpy((char *)(buff + offset), tmpchar, tmplen + padlen);
      offset += tmplen + padlen;

    }                           /* for */
 
  pgetdeviceinfores->GETDEVICEINFO4res_u.gdir_resok4.gdir_device_addr.da_addr_body.da_addr_body_len = offset ;
 
  pgetdeviceinfores->gdir_status = NFS4_OK;

  return pgetdeviceinfores->gdir_status  ;
}                               /* pnfs_parallel_fs_getdeviceinfo */
