/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    nfs_proto_functions.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/05 15:15:01 $
 * \version $Revision: 1.31 $
 * \brief   Prototypes for NFS protocol functions.
 *
 * nfs_proto_functions.h : Prototypes for NFS protocol functions. 
 *
 *
 */

#ifndef _NFS_PROTO_FUNCTIONS_H
#define _NFS_PROTO_FUNCTIONS_H

#include "nfs23.h"
#include "mount.h"
#include "nfs4.h"
#include "nlm4.h"
#include "rquota.h"

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "fsal.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_file_handle.h"
#include "err_HashTable.h"

#define  NFS4_ATTRVALS_BUFFLEN  1024

/* ------------------------------ Typedefs and structs----------------------- */

typedef union nfs_arg__
{
  GETATTR3args arg_getattr3;
  SETATTR3args arg_setattr3;
  LOOKUP3args arg_lookup3;
  ACCESS3args arg_access3;
  READLINK3args arg_readlink3;
  READ3args arg_read3;
  WRITE3args arg_write3;
  CREATE3args arg_create3;
  MKDIR3args arg_mkdir3;
  SYMLINK3args arg_symlink3;
  MKNOD3args arg_mknod3;
  REMOVE3args arg_remove3;
  RMDIR3args arg_rmdir3;
  RENAME3args arg_rename3;
  LINK3args arg_link3;
  READDIR3args arg_readdir3;
  READDIRPLUS3args arg_readdirplus3;
  FSSTAT3args arg_fsstat3;
  FSINFO3args arg_fsinfo3;
  PATHCONF3args arg_pathconf3;
  COMMIT3args arg_commit3;
  COMPOUND4args arg_compound4;

  /* mnt */
  dirpath arg_mnt;

  /* nlm */
  nlm4_testargs arg_nlm4_test;
  nlm4_lockargs arg_nlm4_lock;
  nlm4_cancargs arg_nlm4_cancel;
  nlm4_shareargs arg_nlm4_share;
  nlm4_unlockargs arg_nlm4_unlock;
  nlm4_sm_notifyargs arg_nlm4_sm_notify;
  nlm4_free_allargs arg_nlm4_free_allargs;
  nlm4_res arg_nlm4_res;

  /* Rquota */
  getquota_args arg_rquota_getquota;
  getquota_args arg_rquota_getactivequota;
  setquota_args arg_rquota_setquota;
  setquota_args arg_rquota_setactivequota;

  /* Rquota */
  ext_getquota_args arg_ext_rquota_getquota;
  ext_getquota_args arg_ext_rquota_getactivequota;
  ext_setquota_args arg_ext_rquota_setquota;
  ext_setquota_args arg_ext_rquota_setactivequota;
} nfs_arg_t;

struct COMPOUND4res_extended
{
  COMPOUND4res res_compound4;
  bool         res_cached;
};

typedef union nfs_res__
{
  GETATTR3res res_getattr3;
  SETATTR3res res_setattr3;
  LOOKUP3res res_lookup3;
  ACCESS3res res_access3;
  READLINK3res res_readlink3;
  READ3res res_read3;
  WRITE3res res_write3;
  CREATE3res res_create3;
  MKDIR3res res_mkdir3;
  SYMLINK3res res_symlink3;
  MKNOD3res res_mknod3;
  REMOVE3res res_remove3;
  RMDIR3res res_rmdir3;
  RENAME3res res_rename3;
  LINK3res res_link3;
  READDIR3res res_readdir3;
  READDIRPLUS3res res_readdirplus3;
  FSSTAT3res res_fsstat3;
  FSINFO3res res_fsinfo3;
  PATHCONF3res res_pathconf3;
  COMMIT3res res_commit3;
  COMPOUND4res res_compound4;
  COMPOUND4res_extended res_compound4_extended;

  /* mount */
  fhstatus2 res_mnt1;
  exports res_mntexport;
  mountres3 res_mnt3;
  mountlist res_dump;

  /* nlm4 */
  nlm4_testres  res_nlm4test;
  nlm4_res      res_nlm4;
  nlm4_shareres res_nlm4share;

  /* Ext Rquota */
  getquota_rslt res_rquota_getquota;
  getquota_rslt res_rquota_getactivequota;
  setquota_rslt res_rquota_setquota;
  setquota_rslt res_rquota_setactivequota;
  /* Rquota */
  getquota_rslt res_ext_rquota_getquota;
  getquota_rslt res_ext_rquota_getactivequota;
  setquota_rslt res_ext_rquota_setquota;
  setquota_rslt res_ext_rquota_setactivequota;
} nfs_res_t;

/* flags related to the behaviour of the requests (to be stored in the dispatch behaviour field)  */
#define NOTHING_SPECIAL 0x0000  /* Nothing to be done for this kind of request                    */
#define MAKES_WRITE     0x0001  /* The function modifyes the FSAL (not permitted for RO FS)       */
#define NEEDS_CRED      0x0002  /* A credential is needed for this operation                      */
#define CAN_BE_DUP      0x0004  /* Handling of dup request can be done for this request           */
#define SUPPORTS_GSS    0x0008  /* Request may be authenticated by RPCSEC_GSS                     */

typedef int (*nfs_protocol_function_t) (nfs_arg_t *,
                                        exportlist_t *,
                                        struct req_op_context *req_ctx,
                                        nfs_worker_data_t *,
                                        struct svc_req *, nfs_res_t *);

typedef int (*nfsremote_protocol_function_t) (CLIENT *, nfs_arg_t *, nfs_res_t *);

typedef void (*nfs_protocol_free_t) (nfs_res_t *);

typedef struct nfs_function_desc__
{
  nfs_protocol_function_t service_function;
  nfs_protocol_free_t free_function;
  xdrproc_t xdr_decode_func;
  xdrproc_t xdr_encode_func;
  char *funcname;
  unsigned int dispatch_behaviour;
} nfs_function_desc_t;

int mnt_Null(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int mnt_Mnt(nfs_arg_t *parg,
            exportlist_t *pexport,
            struct req_op_context *req_ctx,
            nfs_worker_data_t *pworker,
            struct svc_req *preq,
            nfs_res_t *pres);

int mnt_Dump(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int mnt_Umnt(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int mnt_UmntAll(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int mnt_Export(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t * pres);

/* @}
 * -- End of MNT protocol functions. --
 */

int nlm_Null(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int nlm4_Test(nfs_arg_t *parg,
              exportlist_t *pexport,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres);

int nlm4_Lock(nfs_arg_t *parg,
              exportlist_t *pexport,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres);

int nlm4_Cancel(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nlm4_Unlock(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nlm4_Sm_Notify(nfs_arg_t *parg,
                   exportlist_t *pexport,
                   struct req_op_context *req_ctx,
                   nfs_worker_data_t *pworker,
                   struct svc_req *preq,
                   nfs_res_t *pres);

int nlm4_Test_Message(nfs_arg_t *parg,
                      exportlist_t *pexport,
                      struct req_op_context *req_ctx,
                      nfs_worker_data_t *pworker,
                      struct svc_req *preq,
                      nfs_res_t *pres);

int nlm4_Cancel_Message(nfs_arg_t *parg,
                        exportlist_t *pexport,
                        struct req_op_context *req_ctx,
                        nfs_worker_data_t *pworker,
                        struct svc_req *preq,
                        nfs_res_t *pres);

int nlm4_Lock_Message(nfs_arg_t *parg,
                      exportlist_t *pexport,
                      struct req_op_context *req_ctx,
                      nfs_worker_data_t *pworker,
                      struct svc_req *preq,
                      nfs_res_t *pres);

int nlm4_Unlock_Message(nfs_arg_t *parg,
                        exportlist_t *pexport,
                        struct req_op_context *req_ctx,
                        nfs_worker_data_t *pworker,
                        struct svc_req *preq,
                        nfs_res_t *pres);


int nlm4_Granted_Res(nfs_arg_t *parg,
                     exportlist_t *pexport,
                     struct req_op_context *req_ctx,
                     nfs_worker_data_t *pworker,
                     struct svc_req *preq,
                     nfs_res_t *pres);

int nlm4_Share(nfs_arg_t            * parg,
               exportlist_t         * pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t    * pworker,
               struct svc_req       * preq,
               nfs_res_t            * pres);

int nlm4_Unshare(nfs_arg_t            * parg,
                 exportlist_t         * pexport,
                 struct req_op_context *req_ctx,
                 nfs_worker_data_t    * pworker,
                 struct svc_req       * preq,
                 nfs_res_t            * pres);

int nlm4_Free_All(nfs_arg_t *parg,
                  exportlist_t *pexport,
                  struct req_op_context *req_ctx,
                  nfs_worker_data_t *pworker,
                  struct svc_req *preq,
                  nfs_res_t *pres);

/* @}
 * -- End of NLM protocol functions. --
 */

int rquota_Null(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int rquota_getquota(nfs_arg_t *parg,
                    exportlist_t *pexport,
                    struct req_op_context *req_ctx,
                    nfs_worker_data_t *pworker,
                    struct svc_req *preq,
                    nfs_res_t *pres);

int rquota_getactivequota(nfs_arg_t *parg,
                          exportlist_t *pexport,
                          struct req_op_context *req_ctx,
                          nfs_worker_data_t *pworker,
                          struct svc_req *preq,
                          nfs_res_t * pres);

int rquota_setquota(nfs_arg_t * parg /* IN  */ ,
                    exportlist_t * pexport /* IN  */ ,
                    struct req_op_context *req_ctx,
                    nfs_worker_data_t *pworker,
                    struct svc_req *preq /* IN  */ ,
                    nfs_res_t * pres /* OUT */ );

int rquota_setactivequota(nfs_arg_t *parg,
                          exportlist_t *pexport,
                          struct req_op_context *req_ctx,
                          nfs_worker_data_t *pworker,
                          struct svc_req *preq,
                          nfs_res_t *pres);

/* @}
 *  * -- End of RQUOTA protocol functions. --
 *   */

int nfs_Null(nfs_arg_t *arg,
             exportlist_t *export,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *worker,
             struct svc_req *req,
             nfs_res_t *res);

int nfs_Getattr(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs_Setattr(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs_Lookup(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

int nfs_Readlink(nfs_arg_t *parg,
                 exportlist_t *pexport,
                 struct req_op_context *req_ctx,
                 nfs_worker_data_t *pworker,
                 struct svc_req *preq,
                 nfs_res_t *pres);

int nfs_Read(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int nfs_Write(nfs_arg_t *parg,
              exportlist_t *pexport,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres);

int nfs_Create(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

int nfs_Remove(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

int nfs_Rename(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

int nfs_Link(nfs_arg_t *parg,
             exportlist_t *pexport,
             struct req_op_context *req_ctx,
             nfs_worker_data_t *pworker,
             struct svc_req *preq,
             nfs_res_t *pres);

int nfs_Symlink(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs_Mkdir(nfs_arg_t *parg,
              exportlist_t *pexport,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres);

int nfs_Rmdir(nfs_arg_t *parg,
              exportlist_t *pexport,
              struct req_op_context *req_ctx,
              nfs_worker_data_t *pworker,
              struct svc_req *preq,
              nfs_res_t *pres);

int nfs_Readdir(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs_Fsstat(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

int nfs3_Access(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs3_Readdirplus(nfs_arg_t *parg,
                     exportlist_t *pexport,
                     struct req_op_context *req_ctx,
                     nfs_worker_data_t *pworker,
                     struct svc_req *preq,
                     nfs_res_t *pres);

int nfs3_Fsinfo(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs3_Pathconf(nfs_arg_t *parg,
                  exportlist_t *pexport,
                  struct req_op_context *req_ctx,
                  nfs_worker_data_t *pworker,
                  struct svc_req *preq,
                  nfs_res_t *pres);

int nfs3_Commit(nfs_arg_t *parg,
                exportlist_t *pexport,
                struct req_op_context *req_ctx,
                nfs_worker_data_t *pworker,
                struct svc_req *preq,
                nfs_res_t *pres);

int nfs3_Mknod(nfs_arg_t *parg,
               exportlist_t *pexport,
               struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t *pres);

/* Functions needed for nfs v4 */

int nfs4_Compound(nfs_arg_t *parg,
                  exportlist_t *pexport,
                  struct req_op_context *req_ctx,
                  nfs_worker_data_t *pworker,
                  struct svc_req *preq /* IN  */ ,
                  nfs_res_t *pres);

typedef int (*nfs4_op_function_t) (struct nfs_argop4 *, compound_data_t *,
                                   struct nfs_resop4 *);

int nfs4_op_access(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_close(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_commit(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_create(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_delegpurge(struct nfs_argop4 *op,   /* [IN] NFS4 OP arguments */
                       compound_data_t * data,  /* [IN] current data for the compound request */
                       struct nfs_resop4 *resp);        /* [OUT] NFS4 OP results */

int nfs4_op_delegreturn(struct nfs_argop4 *op,  /* [IN] NFS4 OP arguments */
                        compound_data_t * data, /* [IN] current data for the compound request */
                        struct nfs_resop4 *resp);       /* [OUT] NFS4 OP results */

int nfs4_op_getattr(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_getfh(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_link(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                 compound_data_t * data,        /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_lock(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                 compound_data_t * data,        /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_lockt(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_locku(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_lookup(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_lookupp(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_lookupp(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_nverify(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_open(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                 compound_data_t * data,        /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_open_confirm(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                         compound_data_t * data,        /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_open_downgrade(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                           compound_data_t * data,      /* [IN] current data for the compound request */
                           struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_openattr(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                     compound_data_t * data,    /* [IN] current data for the compound request */
                     struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

int nfs4_op_putfh(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_putpubfh(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                     compound_data_t * data,    /* [IN] current data for the compound request */
                     struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */
int nfs4_op_putrootfh(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                      compound_data_t * data,   /* [IN] current data for the compound request */
                      struct nfs_resop4 *resp); /* [OUT] NFS4 OP results */

int nfs4_op_read(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                 compound_data_t * data,        /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_readdir(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_remove(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_renew(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_rename(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_restorefh(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                      compound_data_t * data,   /* [IN] current data for the compound request */
                      struct nfs_resop4 *resp); /* [OUT] NFS4 OP results */

int nfs4_op_readdir(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_readlink(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                     compound_data_t * data,    /* [IN] current data for the compound request */
                     struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

int nfs4_op_savefh(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_secinfo(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_setattr(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_setclientid(struct nfs_argop4 *op,  /* [IN] NFS4 OP arguments */
                        compound_data_t * data, /* [IN] current data for the compound request */
                        struct nfs_resop4 *resp);       /* [OUT] NFS4 OP results */

int nfs4_op_setclientid_confirm(struct nfs_argop4 *op,  /* [IN] NFS4 OP arguments */
                                compound_data_t * data, /* [IN] current data for the compound request */
                                struct nfs_resop4 *resp);       /* [OUT] NFS4 OP results */

int nfs4_op_verify(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_write(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                  compound_data_t * data,       /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_release_lockowner(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                              compound_data_t * data,   /* [IN] current data for the compound request */
                              struct nfs_resop4 *resp); /* [OUT] NFS4 OP results */

int nfs4_op_illegal(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                    compound_data_t * data,     /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_exchange_id(struct nfs_argop4 *op, /* [IN] NFS4 OP arguments */
                         compound_data_t * data,        /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);      /* [OUT] NFS4 OP results */

int nfs4_op_commit(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_close(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                   compound_data_t * data,      /* [IN] current data for the compound request */
                   struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_create_session(struct nfs_argop4 *op,      /* [IN] NFS4 OP arguments */
                           compound_data_t * data,     /* [IN] current data for the compound request */
                           struct nfs_resop4 *resp);   /* [OUT] NFS4 OP results */

int nfs4_op_getdevicelist(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                          compound_data_t * data,      /* [IN] current data for the compound request */
                          struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_free_stateid(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                         compound_data_t * data,      /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_getdeviceinfo(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                          compound_data_t * data,      /* [IN] current data for the compound request */
                          struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_destroy_clientid(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                          compound_data_t * data,      /* [IN] current data for the compound request */
                          struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_destroy_session(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                            compound_data_t * data,    /* [IN] current data for the compound request */
                            struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

int nfs4_op_lock(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                 compound_data_t * data,       /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_lockt(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                  compound_data_t * data,      /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_locku(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                  compound_data_t * data,      /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */

int nfs4_op_layoutget(struct nfs_argop4 *op,   /* [IN] NFS4 OP arguments */
                      compound_data_t * data,  /* [IN] current data for the compound request */
                      struct nfs_resop4 *resp);        /* [OUT] NFS4 OP results */

int nfs4_op_layoutcommit(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                         compound_data_t * data,       /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_layoutreturn(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                         compound_data_t * data,       /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_reclaim_complete(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                             compound_data_t * data,   /* [IN] current data for the compound request */
                             struct nfs_resop4 *resp); /* [OUT] NFS4 OP results */

int nfs4_op_sequence(struct nfs_argop4 *op,    /* [IN] NFS4 OP arguments */
                     compound_data_t * data,   /* [IN] current data for the compound request */
                     struct nfs_resop4 *resp); /* [OUT] NFS4 OP results */

int nfs4_op_read(struct nfs_argop4 *op,        /* [IN] NFS4 OP arguments */
                 compound_data_t * data,       /* [IN] current data for the compound request */
                 struct nfs_resop4 *resp);     /* [OUT] NFS4 OP results */

int nfs4_op_set_ssv(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                    compound_data_t * data,    /* [IN] current data for the compound request */
                    struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

int nfs4_op_test_stateid(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
                         compound_data_t * data,    /* [IN] current data for the compound request */
                         struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

int nfs4_op_write(struct nfs_argop4 *op,       /* [IN] NFS4 OP arguments */
                  compound_data_t * data,      /* [IN] current data for the compound request */
                  struct nfs_resop4 *resp);    /* [OUT] NFS4 OP results */


/* Available operations on pseudo fs */
int nfs4_op_getattr_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_access_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_lookup_pseudo(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_lookupp_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_readdir_pseudo(struct nfs_argop4 *op,
                           compound_data_t * data, struct nfs_resop4 *resp);

/* Available operations on xattrs */
int nfs4_op_getattr_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_access_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_lookup_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_lookupp_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_readdir_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp);

nfsstat4 nfs4_fh_to_xattrfh(nfs_fh4 * pfhin, nfs_fh4 * pfhout);
nfsstat4 nfs4_xattrfh_to_fh(nfs_fh4 * pfhin, nfs_fh4 * pfhout);

int nfs4_op_open_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_read_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_write_xattr(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_op_remove_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp);

int nfs4_XattrToFattr(fattr4 * Fattr,
                      compound_data_t * data, nfs_fh4 * objFH, struct bitmap4 * Bitmap);

/* NFSv4 CB calls */
int nfs4_cb_getattr(struct nfs_cb_argop4 *op,
                    compound_data_t * data, struct nfs_cb_resop4 *resp);

int nfs4_cb_recall(struct nfs_cb_argop4 *op,
                   compound_data_t * data, struct nfs_cb_resop4 *resp);

int nfs4_cb_illegal(struct nfs_cb_argop4 *op,
                    compound_data_t * data, struct nfs_cb_resop4 *resp);

/* Stats management for NFSv4 */
int nfs4_op_stat_update(nfs_arg_t * parg /* IN     */ ,
                        nfs_res_t * pres /* IN    */ ,
                        nfs_request_stat_t * pstat_req /* OUT */ );

/* @}
 * -- End of NFS protocols functions. --
 */

/*
 * Definition of an array for the characteristics of each GETATTR sub-operations
 */

#define FATTR4_ATTR_READ       0x00001
#define FATTR4_ATTR_WRITE      0x00010
#define FATTR4_ATTR_READ_WRITE 0x00011

typedef enum {
	FATTR_XDR_NOOP,
	FATTR_XDR_SUCCESS,
	FATTR_XDR_FAILED
} fattr_xdr_result;

struct xdr_attrs_args {
	struct attrlist *attrs;
	nfs_fh4 *hdl4;
	uint32_t rdattr_error;
	int nfs_status;
	compound_data_t *data;
	int statfscalled;
	fsal_dynamicfsinfo_t *dynamicinfo;
};

typedef struct fattr4_dent {
	char *name;                   /* The name of the operation              */
	unsigned int supported;       /* Is this action supported ?             */
	unsigned int size_fattr4;     /* The size of the dedicated attr subtype */
	unsigned int access;          /* The access type for this attributes    */
	attrmask_t attrmask;          /* attr bit for decoding to attrs */
	fattr_xdr_result (*encode)(XDR *xdr, struct xdr_attrs_args *args);
	fattr_xdr_result (*decode)(XDR *xdr, struct xdr_attrs_args *args);
	fattr_xdr_result (*compare)(XDR *xdr1, XDR *xdr2);
	
} fattr4_dent_t;

extern const struct fattr4_dent fattr4tab[];

/* BUGAZOMEU: Some definitions to be removed. FSAL parameters to be used instead */
#define NFS4_LEASE_LIFETIME 120
#define FSINFO_MAX_FILESIZE  0xFFFFFFFFFFFFFFFFll
#define MAX_HARD_LINK_VALUE           (0xffff)
#define NFS4_PSEUDOFS_MAX_READ_SIZE  1048576
#define NFS4_PSEUDOFS_MAX_WRITE_SIZE 1048576
#define NFS4_ROOT_UID 0
#define NFS_MAXPATHLEN MAXPATHLEN
#define DEFAULT_DOMAIN "localdomain"
#define DEFAULT_IDMAPCONF "/etc/idmapd.conf"
#endif                          /* _NFS_PROTO_FUNCTIONS_H */

#define NFS_REQ_OK   0
#define NFS_REQ_DROP 1

/* Free functions */
void mnt1_Mnt_Free(nfs_res_t * pres);
void mnt3_Mnt_Free(nfs_res_t * pres);

void mnt_Dump_Free(nfs_res_t * pres);
void mnt_Export_Free(nfs_res_t * pres);
void mnt_Null_Free(nfs_res_t * pres);
void mnt_Umnt_Free(nfs_res_t * pres);
void mnt_UmntAll_Free(nfs_res_t * pres);

void nlm_Null_Free(nfs_res_t * pres);
void nlm4_Test_Free(nfs_res_t * pres);
void nlm4_Lock_Free(nfs_res_t * pres);
void nlm4_NM_Lock_Free(nfs_res_t * pres);
void nlm4_Share_Free(nfs_res_t * pres);
void nlm4_Unshare_Free(nfs_res_t * pres);
void nlm4_Cancel_Free(nfs_res_t * pres);
void nlm4_Unlock_Free(nfs_res_t * pres);
void nlm4_Sm_Notify_Free(nfs_res_t * pres);
void nlm4_Granted_Res_Free(nfs_res_t * pres);
void nlm4_Free_All_Free(nfs_res_t * pres);

void rquota_Null_Free(nfs_res_t * pres);
void rquota_getquota_Free(nfs_res_t * pres);
void rquota_getactivequota_Free(nfs_res_t * pres);
void rquota_setquota_Free(nfs_res_t * pres);
void rquota_setactivequota_Free(nfs_res_t * pres);

void nfs_Null_Free(nfs_res_t * resp);
void nfs_Getattr_Free(nfs_res_t * resp);
void nfs_Setattr_Free(nfs_res_t * resp);
void nfs2_Lookup_Free(nfs_res_t * resp);
void nfs3_Lookup_Free(nfs_res_t * resp);
void nfs3_Access_Free(nfs_res_t * pres);
void nfs3_Readlink_Free(nfs_res_t * resp);
void nfs2_Read_Free(nfs_res_t * resp);
void nfs_Write_Free(nfs_res_t * resp);
void nfs_Create_Free(nfs_res_t * resp);
void nfs_Mkdir_Free(nfs_res_t * resp);
void nfs_Symlink_Free(nfs_res_t * resp);
void nfs3_Mknod_Free(nfs_res_t * pres);
void nfs_Remove_Free(nfs_res_t * resp);
void nfs_Rmdir_Free(nfs_res_t * resp);
void nfs_Rename_Free(nfs_res_t * resp);
void nfs_Link_Free(nfs_res_t * resp);
void nfs3_Readdir_Free(nfs_res_t * resp);
void nfs3_Readdirplus_Free(nfs_res_t * resp);
void nfs_Fsstat_Free(nfs_res_t * resp);
void nfs3_Fsinfo_Free(nfs_res_t * pres);
void nfs3_Pathconf_Free(nfs_res_t * pres);
void nfs3_Commit_Free(nfs_res_t * pres);
void nfs2_Readdir_Free(nfs_res_t * resp);
void nfs3_Read_Free(nfs_res_t * resp);
void nfs2_Readlink_Free(nfs_res_t * resp);
void nfs4_Compound_FreeOne(nfs_resop4 * pres);
void nfs4_Compound_Free(nfs_res_t * pres);
void nfs4_Compound_CopyResOne(nfs_resop4 * pres_dst, nfs_resop4 * pres_src);
void nfs4_Compound_CopyRes(nfs_res_t * pres_dst, nfs_res_t * pres_src);

void nfs4_op_access_Free(ACCESS4res * resp);
void nfs4_op_close_Free(CLOSE4res * resp);
void nfs4_op_commit_Free(COMMIT4res * resp);
void nfs4_op_create_Free(CREATE4res * resp);
void nfs4_op_delegreturn_Free(DELEGRETURN4res * resp);
void nfs4_op_delegpurge_Free(DELEGPURGE4res * resp);
void nfs4_op_getattr_Free(GETATTR4res * resp);
void nfs4_op_getfh_Free(GETFH4res * resp);
void nfs4_op_illegal_Free(ILLEGAL4res * resp);
void nfs4_op_link_Free(LINK4res * resp);
void nfs4_op_lock_Free(LOCK4res * resp);
void nfs4_op_lockt_Free(LOCKT4res * resp);
void nfs4_op_locku_Free(LOCKU4res * resp);
void nfs4_op_lookup_Free(LOOKUP4res * resp);
void nfs4_op_lookupp_Free(LOOKUPP4res * resp);
void nfs4_op_nverify_Free(NVERIFY4res * resp);
void nfs4_op_open_Free(OPEN4res * resp);
void nfs4_op_open_confirm_Free(OPEN_CONFIRM4res * resp);
void nfs4_op_open_downgrade_Free(OPEN_DOWNGRADE4res * resp);
void nfs4_op_openattr_Free(OPENATTR4res * resp);
void nfs4_op_openattr_Free(OPENATTR4res * resp);
void nfs4_op_putfh_Free(PUTFH4res * resp);
void nfs4_op_putpubfh_Free(PUTPUBFH4res * resp);
void nfs4_op_putrootfh_Free(PUTROOTFH4res * resp);
void nfs4_op_read_Free(READ4res * resp);
void nfs4_op_readdir_Free(READDIR4res * resp);
void nfs4_op_readlink_Free(READLINK4res * resp);
void nfs4_op_release_lockowner_Free(RELEASE_LOCKOWNER4res * resp);
void nfs4_op_rename_Free(RENAME4res * resp);
void nfs4_op_remove_Free(REMOVE4res * resp);
void nfs4_op_renew_Free(RENEW4res * resp);
void nfs4_op_restorefh_Free(RESTOREFH4res * resp);
void nfs4_op_savefh_Free(SAVEFH4res * resp);
void nfs4_op_secinfo_Free(SECINFO4res * resp);
void nfs4_op_setattr_Free(SETATTR4res * resp);
void nfs4_op_setclientid_Free(SETCLIENTID4res * resp);
void nfs4_op_setclientid_confirm_Free(SETCLIENTID_CONFIRM4res * resp);
void nfs4_op_verify_Free(VERIFY4res * resp);
void nfs4_op_write_Free(WRITE4res * resp);

void nfs4_op_close_CopyRes(CLOSE4res * resp_dst, CLOSE4res * resp_src);
void nfs4_op_lock_CopyRes(LOCK4res * resp_dst, LOCK4res * resp_src);
void nfs4_op_locku_CopyRes(LOCKU4res * resp_dst, LOCKU4res * resp_src);
void nfs4_op_open_CopyRes(OPEN4res * resp_dst, OPEN4res * resp_src);
void nfs4_op_open_confirm_CopyRes(OPEN_CONFIRM4res * resp_dst, OPEN_CONFIRM4res * resp_src);
void nfs4_op_open_downgrade_CopyRes(OPEN_DOWNGRADE4res * resp_dst, OPEN_DOWNGRADE4res * resp_src);
void nfs4_op_delegreturn_CopyRes(DELEGRETURN4res * resp_dst, DELEGRETURN4res * resp_src);

void nfs4_op_exchange_id_Free(EXCHANGE_ID4res * resp);
void nfs4_op_close_Free(CLOSE4res * resp);
void nfs4_op_create_session_Free(CREATE_SESSION4res * resp);
void nfs4_op_getdevicelist_Free(GETDEVICELIST4res * resp);
void nfs4_op_getdeviceinfo_Free(GETDEVICEINFO4res * resp);
void nfs4_op_free_stateid_Free(FREE_STATEID4res * resp);
void nfs4_op_destroy_session_Free(DESTROY_SESSION4res * resp);
void nfs4_op_lock_Free(LOCK4res * resp);
void nfs4_op_lockt_Free(LOCKT4res * resp);
void nfs4_op_locku_Free(LOCKU4res * resp);
void nfs4_op_read_Free(READ4res * resp);
void nfs4_op_sequence_Free(SEQUENCE4res * resp);
void nfs4_op_set_ssv_Free(SET_SSV4res * resp);
void nfs4_op_test_stateid_Free(TEST_STATEID4res * resp);
void nfs4_op_write_Free(WRITE4res * resp);
void nfs4_op_reclaim_complete_Free(RECLAIM_COMPLETE4res * resp);

void compound_data_Free(compound_data_t * data);

#ifndef _USE_SWIG
/* Pseudo FS functions */
int nfs4_ExportToPseudoFS(exportlist_t * pexportlist);
pseudofs_t *nfs4_GetPseudoFs(void);

int nfs4_SetCompoundExport(compound_data_t * data);
int nfs4_MakeCred(compound_data_t * data);

int cache_entry_To_Fattr(cache_entry_t *entry, fattr4 *Fattr,
                         compound_data_t *data, nfs_fh4 *objFH,
                         struct bitmap4 *Bitmap);

int nfs4_fsal_attr_To_Fattr(const struct attrlist *pattr, fattr4 * Fattr,
                            compound_data_t * data, struct bitmap4 * Bitmap);
int nfs4_Fattr_To_fsal_attr(struct attrlist *pattr, fattr4 * Fattr,
                            compound_data_t * data, struct bitmap4 * Bitmap);
int nfs4_Fattr_Check_Access(fattr4 * Fattr, int access);
int nfs4_Fattr_Check_Access_Bitmap(struct bitmap4 * pbitmap, int access);
int nfs4_Fattr_Supported(fattr4 * Fattr);
int nfs4_Fattr_Supported_Bitmap(struct bitmap4 * pbitmap);
int nfs4_Fattr_cmp(fattr4 * Fattr1, fattr4 * Fattr2);

int nfs4_referral_str_To_Fattr_fs_location(char *input_str, char *buff, u_int * plen);

/**** Glue related functions ****/
/*  nfs4_stringid_split - split a 'user@domain' string in two separated parts
 *  str2utf8 - convert a regular, zero terminated string to a UTF8 string,
 *  utf82str - convert a utf8 string to a regular zero-terminated string
 *  uid2utf8 - convert a uid to a utf8 string descriptor
 *  gid2utf8 - convert a gid to a utf8 string descriptor
 *  utf82uid - convert a utf8 string descriptor to a uid
 *  uft82gid - convert a utf8 string descriptor to a gid 
 *  gid2str  - convert a gid to a string 
 *  str2gid  - convert a string to a gid
 *  uid2str  - convert a uid to a string
 *  str2uid  - convert a string to a uid
 */

int uid2name(char *name, uid_t * puid);
int name2uid(char *name, uid_t * puid);
#ifdef _HAVE_GSSAPI
#ifdef _MSPAC_SUPPORT
int principal2uid(char *principal, uid_t * puid, struct svc_rpc_gss_data *gd);
#else
int principal2uid(char *principal, uid_t * puid);
#endif
#endif

int gid2name(char *name, gid_t * pgid);
int name2gid(char *name, gid_t * pgid);

void free_utf8(utf8string * utf8str);
int utf82str(char *str, int size, utf8string * utf8str);
int str2utf8(char *str, utf8string * utf8str);

int uid2utf8(uid_t uid, utf8string * utf8str);
int utf82uid(utf8string * utf8str, uid_t *Uid);

int uid2str(uid_t uid, char *str);
int str2uid(char *str, uid_t * Uid);

int gid2str(gid_t gid, char *str);
int str2gid(char *str, gid_t * Gid);

int gid2utf8(gid_t gid, utf8string * utf8str);
int utf82gid(utf8string * utf8str, gid_t *Gid);

void nfs4_stringid_split(char *buff, char *uidname, char *domainname);

seqid4 nfs4_NextSeqId(seqid4 seqid);

bool nfs3_FSALattr_To_Fattr(exportlist_t * pexport,
                            const struct attrlist *pFSAL_attr,
                            fattr3 * pFattr);

bool cache_entry_to_nfs3_Fattr(cache_entry_t *entry,
                               struct req_op_context *ctx,
                               fattr3 *Fattr);

void nfs3_FSALattr_To_PartialFattr(const struct attrlist *FSAL_attr,
                                   attrmask_t *mask,
                                   fattr3 *Fattr);
bool nfs3_Sattr_To_FSALattr(struct attrlist *FSAL_attr,
                            sattr3 *Fattr);

nfsstat3 nfs3_fh_to_xattrfh(nfs_fh3 * pfhin, nfs_fh3 * pfhout);

int nfs3_Access_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres);

int nfs3_Getattr_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       struct req_op_context *req_ctx,
                       struct svc_req *preq, nfs_res_t * pres);

int nfs3_Lookup_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres);

int nfs3_Read_Xattr(nfs_arg_t * parg,
                    exportlist_t * pexport,
                    struct req_op_context *req_ctx,
                    struct svc_req *preq, nfs_res_t * pres);

int nfs3_Create_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres);

int nfs3_Write_Xattr(nfs_arg_t * parg,
                     exportlist_t * pexport,
                     struct req_op_context *req_ctx,
                     struct svc_req *preq, nfs_res_t * pres);

int nfs3_Readdir_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       struct req_op_context *req_ctx,
                       struct svc_req *preq, nfs_res_t * pres);

int nfs3_Readdirplus_Xattr(nfs_arg_t * parg,
                           exportlist_t * pexport,
                           struct req_op_context *req_ctx,
                           struct svc_req *preq, nfs_res_t * pres);

int nfs3_Remove_Xattr(nfs_arg_t * parg /* IN  */ ,
                      exportlist_t * pexport /* IN  */ ,
                      struct req_op_context *req_ctx /* IN  */ ,
                      struct svc_req *preq /* IN  */ ,
                      nfs_res_t * pres /* OUT */ );

int nfs4_PseudoToFattr(pseudofs_entry_t * psfsp,
                       fattr4 * Fattr,
                       compound_data_t * data, nfs_fh4 * objFH, struct bitmap4 * Bitmap);

int nfs4_PseudoToFhandle(nfs_fh4 * fh4p, pseudofs_entry_t * psfsentry);

int nfs4_Fattr_To_FSAL_attr(struct attrlist *pFSAL_attr,    /* Out: File attributes  */
                            fattr4 * pFattr);   /* In: File attributes   */

int nfs4_Fattr_To_fsinfo(fsal_dynamicfsinfo_t *dinfo, fattr4 * pFattr);

int nfs4_FSALattr_To_Fattr(const struct attrlist *pattr,
                           fattr4 *Fattr,
                           compound_data_t *data,
                           nfs_fh4 *objFH,
                           struct bitmap4 *Bitmap);

uint64_t nfs_htonl64(uint64_t arg64);
uint64_t nfs_ntohl64(uint64_t arg64);
int nfs4_bitmap4_Remove_Unsupported(struct bitmap4 *pbitmap) ;

/* Error conversion routines */
nfsstat4 nfs4_Errno_verbose(cache_inode_status_t error, const char *);
nfsstat3 nfs3_Errno_verbose(cache_inode_status_t error, const char *);
#define nfs4_Errno(e) nfs4_Errno_verbose(e, __func__)
#define nfs3_Errno(e) nfs3_Errno_verbose(e, __func__)
int nfs3_AllocateFH(nfs_fh3 * fh);
int nfs4_AllocateFH(nfs_fh4 * fh);

int nfs4_Is_Fh_Referral(nfs_fh4 * pfh);
int nfs4_Set_Fh_Referral(nfs_fh4 * pfh);
#endif
