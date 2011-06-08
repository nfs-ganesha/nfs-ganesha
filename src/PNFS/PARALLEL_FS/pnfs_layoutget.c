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

#include "log_macros.h"
#include "stuff_alloc.h"
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

nfsstat4 pnfs_parallel_fs_layoutget( LAYOUTGET4args   * playoutgetargs,
				compound_data_t  * data,
				LAYOUTGET4res    * playoutgetres )
{
 unsigned int offset = 0;
  uint32_t int32 = 0;
  int64_t int64 = 0LL;
  unsigned int padlen = 0;
  char deviceid[NFS4_DEVICEID4_SIZE];
  unsigned int i;
  char * buff = NULL ; 
  unsigned int stripe = 1 ;
  nfs_fh4 * pnfsfh4 ;

  if( !data || !playoutgetres )
    return NFS4ERR_SERVERFAULT ;

  pnfsfh4 = &data->currentFH ; 

  if((buff = Mem_Alloc(1024)) == NULL)
    {
      playoutgetres->logr_status = NFS4ERR_SERVERFAULT;
      return playoutgetres->logr_status ;
    }

  /* No return on close for the moment */
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_return_on_close = FALSE;

  /* Manages the stateid */
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_stateid.seqid = 1;
  memcpy(playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_stateid.other,
         playoutgetargs->loga_stateid.other, 12);
  //file_state->stateid_other, 12);

  /* Now the layout specific information */
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_len = 1;  /** @todo manages more than one segment */
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val =
      (layout4 *) Mem_Alloc(sizeof(layout4));

  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_offset =
      playoutgetargs->loga_offset;
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_length = 0xFFFFFFFFFFFFFFFFLL;   /* Whole file */
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].lo_iomode =
      playoutgetargs->loga_iomode;
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].
      lo_content.loc_type = LAYOUT4_NFSV4_1_FILES;


  /** @todo It should be better to use xdr_nfsv4_1_file_layout4 on a xdrmem stream */

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
      int32 = htonl(pnfsfh4->nfs_fh4_len);
      memcpy((char *)(buff + offset), (char *)&int32, sizeof(int32));
      offset += sizeof(int32);

      /* nfl_fh_list.nfl_fh_list_val[i].nfs_fh4_len */
      memcpy((char *)(buff + offset), pnfsfh4->nfs_fh4_val, pnfsfh4->nfs_fh4_len ) ;

      /* Turn the file handle to a 'DS file handle' */
      //if(pds_file->filepart[i].is_ganesha == FALSE)
      //  ((char *)(buff + offset))[2] = 9;

      /* Update the offset for encoding */
      offset += pnfsfh4->nfs_fh4_len ;

      /* XDR padding : keep stuff aligned on 32 bits pattern */
      if( pnfsfh4->nfs_fh4_len  == 0)
        padlen = 0;
      else
        padlen = 4 - ( pnfsfh4->nfs_fh4_len % 4);

      if(padlen > 0)
        memset((char *)(buff + offset), 0, padlen);

      offset += padlen;
    }                           /* for */

  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].
      lo_content.loc_body.loc_body_len = offset ;
  playoutgetres->LAYOUTGET4res_u.logr_resok4.logr_layout.logr_layout_val[0].
      lo_content.loc_body.loc_body_val = buff;

  playoutgetres->logr_status = NFS4_OK ;
  return NFS4_OK ;
}                               /* pnfs_parallel_fs_layoutget */
