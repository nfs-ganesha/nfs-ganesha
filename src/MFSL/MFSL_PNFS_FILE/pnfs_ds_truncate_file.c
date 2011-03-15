/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    pnfs_create_ds_file.c
 * \brief   pNFS objects creation functions.
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

#include "stuff_alloc.h"
#include "nfs_proto_functions.h"

#include "PNFS/SPNFS_LIKE/pnfs_layout4_nfsv4_1_files.h"
#include "PNFS/SPNFS_LIKE/pnfs_nfsv41_macros.h"

#define PNFS_LAYOUTFILE_NB_OP_TRUNCATE_DS_FILE 3

#define PNFS_LAYOUTFILE_TRUNCATE_VAL_BUFFER  1024
static int pnfs_truncate_ds_partfile(pnfs_ds_client_t * pnfsdsclient,
                                     size_t newsize,
                                     pnfs_part_file_t * ppartfile)
{
  COMPOUND4args argnfs4;
  COMPOUND4res resnfs4;
  struct timeval timeout = { 25, 0 };
  nfs_argop4 argoparray[PNFS_LAYOUTFILE_NB_OP_TRUNCATE_DS_FILE];
  nfs_resop4 resoparray[PNFS_LAYOUTFILE_NB_OP_TRUNCATE_DS_FILE];
  fsal_attrib_list_t fsal_attr_set;
  fattr4 fattr_set;
  bitmap4 inbitmap;
  bitmap4 convert_bitmap;
  uint32_t inbitmap_val[2];
  uint32_t bitmap_res[2];
  uint32_t bitmap_set[2];
  uint32_t bitmap_conv_val[2];
  unsigned int i;

  if(!pnfsdsclient || !ppartfile )
    return NFS4ERR_SERVERFAULT;

  /* Step 1 OP4_OPEN as OPEN4_CREATE */
  argnfs4.argarray.argarray_val = argoparray;
  resnfs4.resarray.resarray_val = resoparray;
  argnfs4.minorversion = 1;

  /* argnfs4.tag.utf8string_val = "GANESHA NFSv4 Proxy: Mkdir" ; */
  argnfs4.tag.utf8string_val = NULL;
  argnfs4.tag.utf8string_len = 0;
  argnfs4.argarray.argarray_len = 0;

  memset( (char *)&fsal_attr_set, 0, sizeof( fsal_attr_set ) );
  fsal_attr_set.asked_attributes = FSAL_ATTR_SIZE;
  fsal_attr_set.filesize = newsize;

  convert_bitmap.bitmap4_val = bitmap_conv_val;
  convert_bitmap.bitmap4_len = 2;
  convert_bitmap.bitmap4_val[0] = 1 << FATTR4_SIZE ;
  convert_bitmap.bitmap4_val[1] = 0 ;

  
  if(nfs4_FSALattr_To_Fattr(NULL,       /* no exportlist required here */
                            &fsal_attr_set, &fattr_set, NULL,   /* no compound data required here */
                            NULL,       /* No fh here, filehandle is not a settable attribute */
                            &convert_bitmap) == -1)
     return NFS4ERR_INVAL ;

#define PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SEQUENCE  0
#define PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_PUTFH     1
#define PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SETATTR   2

  COMPOUNDV41_ARG_ADD_OP_SEQUENCE(argnfs4, pnfsdsclient->session, pnfsdsclient->sequence);
  COMPOUNDV41_ARG_ADD_OP_PUTFH(argnfs4, ppartfile->handle);
  COMPOUNDV41_ARG_ADD_OP_SETATTR(argnfs4, fattr_set);

  /* For ATTR_SIZE, stateid is needed, we use the stateless "all-0's" stateid here */
  argnfs4.argarray.argarray_val[PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SETATTR].nfs_argop4_u.
      opsetattr.stateid.seqid = 0;
  memset(argnfs4.argarray.argarray_val[PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SETATTR].
         nfs_argop4_u.opsetattr.stateid.other, 0, 12);

  resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SETATTR].nfs_resop4_u.
      opsetattr.attrsset.bitmap4_val = bitmap_set;
  resnfs4.resarray.resarray_val[PNFS_LAYOUTFILE_TRUNCATE_IDX_OP_SETATTR].nfs_resop4_u.
      opsetattr.attrsset.bitmap4_len = 2;

  /* Call the NFSv4 function */
  if(clnt_call(pnfsdsclient->rpc_client, NFSPROC4_COMPOUND,
               (xdrproc_t) xdr_COMPOUND4args, (caddr_t) & argnfs4,
               (xdrproc_t) xdr_COMPOUND4res, (caddr_t) & resnfs4, timeout) != RPC_SUCCESS)
    {
      return NFS4ERR_IO;        /* @todo: For wanting of something more appropriate */
    }

  if(resnfs4.status != NFS4_OK)
    return resnfs4.status;

  pnfsdsclient->sequence += 1;

  return NFS4_OK;
}                               /* pnfs_ds_unlink_file */

int pnfs_ds_truncate_file( pnfs_client_t * pnfsclient,
                           size_t newsize,
                           pnfs_ds_file_t * pfile)
{
  char filename[MAXNAMLEN];
  unsigned int i;
  int rc = 0;

  if(!pnfsclient || !pfile)
    return NFS4ERR_SERVERFAULT;

  for(i = 0; i < pnfsclient->nb_ds; i++)
    {
      if((rc =
          pnfs_truncate_ds_partfile( &(pnfsclient->ds_client[i]),
                                     newsize, 
                                     &(pfile->filepart[i]))) != NFS4_OK)
        return rc;
    }

  return NFS4_OK;
}                               /* pnfs_ds_truncate_file */
