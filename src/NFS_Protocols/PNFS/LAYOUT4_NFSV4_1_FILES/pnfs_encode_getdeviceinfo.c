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

#include <string.h>
#include <signal.h>

#include "stuff_alloc.h"

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#else
#include <rpc/rpc.h>
#endif

#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"
#include "nfs_core.h"

/**
 *
 * pnfs_encode_getdeviceinfo: encodes the addr_body_val structure in GETDEVICEINFO.
 *
 * Encode the addr_body_val structure in GETDEVICEINFO.
 *
 * @param buff [OUT] buffer in which XDR encoding will be made
 * @param plen [OUT] length of buffer
 *
 * @return  nothing (void function)
 *
 */

extern nfs_parameter_t nfs_param;

void pnfs_encode_getdeviceinfo(char *buff, unsigned int *plen)
{
  unsigned int offset = 0;
  uint32_t int32 = 0;
  char tmpchar[MAXNAMLEN];
  unsigned int tmplen = 0;
  unsigned int padlen = 0;
  unsigned int i = 0;

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

      *plen = offset;
    }                           /* for */
}                               /* pnfs_encode_getdeviceinfo */
