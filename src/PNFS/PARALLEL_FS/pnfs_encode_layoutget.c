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

//#include "PNFS/LAYOUT4_NFSV4_1_FILES/pnfs_layout4_nfsv4_1_files.h"
#include "pnfs.h"

/**
 *
 * pnfs_encode_layoutget: encodes the loc_body_val structure in LAYOUTGET.
 *
 * Encodes the loc_body_val structure in layoutget.
 *
 * @param pds_file [IN]  structure representing file's part on the DS.
 * @param buff     [OUT] buffer in which XDR encoding will be made
 * @param plen     [OUT] pointerlength of buffer
 *
 * @return  NFSv4 status (with NFSv4 error code)
 *
 */

int pnfs_lustre_encode_layoutget( char * buffin, unsigned int * plenin, char *buff, unsigned int *plen)
{
  unsigned int offset = 0;
  uint32_t int32 = 0;
  int64_t int64 = 0LL;
  unsigned int padlen = 0;
  char deviceid[NFS4_DEVICEID4_SIZE];
  unsigned int i;

  unsigned int stripe = 1 ;

  /* nfl_deviceid */
  memset(deviceid, 0, NFS4_DEVICEID4_SIZE);
  deviceid[0] = 1 ; /** @todo : this part of the code is to be reviewed */
  memcpy((char *)(buff + offset), deviceid, NFS4_DEVICEID4_SIZE);
  offset += NFS4_DEVICEID4_SIZE;

  /* nfl_util */
  int32 = htonl(0x2000);
  memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
  offset += sizeof(int32);

  /* nfl_first_stripe_index */
  int32 = 0;
  memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
  offset += sizeof(int32);

  /* nfl_pattern_offset */
  int64 = 0LL;
  memcpy((char *)(buff + offset), (char *)&int64, sizeof(int64));
  offset += sizeof(int64);

  /* nfl_fh_list.nfl_fh_list_len */
  int32 = htonl( stripe );
  memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
  offset += sizeof(int32);

  for(i = 0; i < stripe; i++)
    {
      /* nfl_fh_list.nfl_fh_list_val[i].nfs_fh4_len */
      int32 = htonl(*plenin);
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);

      /* nfl_fh_list.nfl_fh_list_val[i].nfs_fh4_len */
      memcpy((char *)(buff + offset), buffin, *plenin ) ;

      /* Turn the file handle to a 'DS file handle' */
      //if(pds_file->filepart[i].is_ganesha == FALSE)
      //  ((char *)(buff + offset))[2] = 9;

      /* Update the offset for encoding */
      offset += *plenin ;

      /* XDR padding : keep stuff aligned on 32 bits pattern */
      if( *plenin == 0)
        padlen = 0;
      else
        padlen = 4 - ( *plenin % 4);

      if(padlen > 0)
        memset((char *)(buff + offset), 0, padlen);

      offset += padlen;

      *plen = offset;
    }                           /* for */

  return NFS4_OK ;
}                               /* pnfs_ds_encode_layoutget */
