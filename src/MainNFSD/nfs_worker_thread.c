/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    nfs_worker_thread.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   The file that contain the 'worker_thread' routine for the nfsd.
 *
 * nfs_worker_thread.c : The file that contain the 'worker_thread' routine for the nfsd (and all
 * the related stuff).
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"

#if defined( _USE_TIRPC )
#include <rpc/rpc.h>
#elif defined( _USE_GSSRPC )
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "SemN.h"

#define NULL_SVC ((struct svc_callout *)0)
#define SVCAUTH_PRIVATE(auth) \
        ((struct svc_rpc_gss_data *)(auth)->svc_ah_private)

enum auth_stat _authenticate(register struct svc_req *rqst, struct rpc_msg *msg);
#ifdef _USE_GSSRPC
enum auth_stat Rpcsecgss__authenticate(register struct svc_req *rqst, struct rpc_msg *msg,
                                       bool_t * no_dispatch);
#endif

#ifdef _SOLARIS
#define _authenticate __authenticate
#endif

#ifdef _DEBUG_MEMLEAKS
void nfs_debug_debug_label_info();
#endif

extern nfs_worker_data_t *workers_data;
extern nfs_parameter_t nfs_param;
extern SVCXPRT *Xports[FD_SETSIZE];     /* The one from RPCSEC_GSS library */
extern hash_table_t *ht_dupreq; /* duplicate request hash */

/* These two variables keep state of the thread that gc at this time */
extern unsigned int nb_current_gc_workers;
extern pthread_mutex_t lock_nb_current_gc_workers;

extern pthread_mutex_t mutex_cond_xprt[FD_SETSIZE];
extern pthread_cond_t condvar_xprt[FD_SETSIZE];
extern int etat_xprt[FD_SETSIZE];

/* is daemon terminating ? If so, it drops all requests */
int nfs_do_terminate = FALSE;

/* Static array : all the function pointer per nfs v2 functions */
const nfs_function_desc_t nfs2_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_fhandle2, (xdrproc_t) xdr_ATTR2res,
   "nfs_Getattr", NEEDS_CRED},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR2args, (xdrproc_t) xdr_ATTR2res,
   "nfs_Setattr", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs2_Root, nfs2_Root_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "nfs2_Root",
   NOTHING_SPECIAL},
  {nfs_Lookup, nfs2_Lookup_Free, (xdrproc_t) xdr_diropargs2, (xdrproc_t) xdr_DIROP2res,
   "nfs_Lookup", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Readlink, nfs2_Readlink_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_READLINK2res, "nfs_Readlink", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Read, nfs2_Read_Free, (xdrproc_t) xdr_READ2args, (xdrproc_t) xdr_READ2res,
   "nfs_Read", NEEDS_CRED | SUPPORTS_GSS},
  {nfs2_Writecache, nfs2_Writecache_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
   "nfs_Writecache", NOTHING_SPECIAL},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE2args, (xdrproc_t) xdr_ATTR2res,
   "nfs_Write", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE2args, (xdrproc_t) xdr_DIROP2res,
   "nfs_Create", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_diropargs2, (xdrproc_t) xdr_nfsstat2,
   "nfs_Remove", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME2args, (xdrproc_t) xdr_nfsstat2,
   "nfs_Rename", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK2args, (xdrproc_t) xdr_nfsstat2,
   "nfs_Link", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK2args, (xdrproc_t) xdr_nfsstat2,
   "nfs_Symlink", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_CREATE2args, (xdrproc_t) xdr_DIROP2res,
   "nfs_Mkdir", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_diropargs2, (xdrproc_t) xdr_nfsstat2,
   "nfs_Rmdir", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs2_Readdir_Free, (xdrproc_t) xdr_READDIR2args,
   (xdrproc_t) xdr_READDIR2res, "nfs_Readdir", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_fhandle2, (xdrproc_t) xdr_STATFS2res,
   "nfs_Fsstat", NEEDS_CRED}
};

const nfs_function_desc_t nfs3_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_GETATTR3args,
   (xdrproc_t) xdr_GETATTR3res, "nfs_Getattr", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR3args,
   (xdrproc_t) xdr_SETATTR3res, "nfs_Setattr",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Lookup, nfs3_Lookup_Free, (xdrproc_t) xdr_LOOKUP3args, (xdrproc_t) xdr_LOOKUP3res,
   "nfs_Lookup", NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Access, nfs3_Access_Free, (xdrproc_t) xdr_ACCESS3args, (xdrproc_t) xdr_ACCESS3res,
   "nfs3_Access", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Readlink, nfs3_Readlink_Free, (xdrproc_t) xdr_READLINK3args,
   (xdrproc_t) xdr_READLINK3res, "nfs_Readlink", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Read, nfs3_Read_Free, (xdrproc_t) xdr_READ3args, (xdrproc_t) xdr_READ3res,
   "nfs_Read", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE3args, (xdrproc_t) xdr_WRITE3res,
   "nfs_Write", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE3args, (xdrproc_t) xdr_CREATE3res,
   "nfs_Create", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_MKDIR3args, (xdrproc_t) xdr_MKDIR3res,
   "nfs_Mkdir", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK3args,
   (xdrproc_t) xdr_SYMLINK3res, "nfs_Symlink",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs3_Mknod, nfs3_Mknod_Free, (xdrproc_t) xdr_MKNOD3args, (xdrproc_t) xdr_MKNOD3res,
   "nfs3_Mknod", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_REMOVE3args, (xdrproc_t) xdr_REMOVE3res,
   "nfs_Remove", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_RMDIR3args, (xdrproc_t) xdr_RMDIR3res,
   "nfs_Rmdir", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME3args, (xdrproc_t) xdr_RENAME3res,
   "nfs_Rename", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK3args, (xdrproc_t) xdr_LINK3res,
   "nfs_Link", MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs3_Readdir_Free, (xdrproc_t) xdr_READDIR3args,
   (xdrproc_t) xdr_READDIR3res, "nfs_Readdir", NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Readdirplus, nfs3_Readdirplus_Free, (xdrproc_t) xdr_READDIRPLUS3args,
   (xdrproc_t) xdr_READDIRPLUS3res, "nfs3_Readdirplus", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_FSSTAT3args, (xdrproc_t) xdr_FSSTAT3res,
   "nfs_Fsstat", NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Fsinfo, nfs3_Fsinfo_Free, (xdrproc_t) xdr_FSINFO3args, (xdrproc_t) xdr_FSINFO3res,
   "nfs3_Fsinfo", NEEDS_CRED},
  {nfs3_Pathconf, nfs3_Pathconf_Free, (xdrproc_t) xdr_PATHCONF3args,
   (xdrproc_t) xdr_PATHCONF3res, "nfs3_Pathconf", NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Commit, nfs3_Commit_Free, (xdrproc_t) xdr_COMMIT3args, (xdrproc_t) xdr_COMMIT3res,
   "nfs3_Commit", MAKES_WRITE | NEEDS_CRED | SUPPORTS_GSS}
};

/* Remeber that NFSv4 manages authentication though junction crossing, and so does it for RO FS management (for each operation) */
const nfs_function_desc_t nfs4_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs4_Compound, nfs4_Compound_Free, (xdrproc_t) xdr_COMPOUND4args,
   (xdrproc_t) xdr_COMPOUND4res, "nfs4_Compound", NEEDS_CRED | SUPPORTS_GSS}
};

const nfs_function_desc_t mnt1_func_desc[] = {
  {mnt_Null, mnt_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "mnt_Null",
   NOTHING_SPECIAL},
  {mnt_Mnt, mnt1_Mnt_Free, (xdrproc_t) xdr_dirpath, (xdrproc_t) xdr_fhstatus2, "mnt_Mnt",
   NEEDS_CRED},
  {mnt_Dump, mnt_Dump_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_mountlist, "mnt_Dump",
   NOTHING_SPECIAL},
  {mnt_Umnt, mnt_Umnt_Free, (xdrproc_t) xdr_dirpath, (xdrproc_t) xdr_void, "mnt_Umnt",
   NOTHING_SPECIAL},
  {mnt_UmntAll, mnt_UmntAll_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
   "mnt_UmntAll", NOTHING_SPECIAL},
  {mnt_Export, mnt_Export_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_exports,
   "mnt_Export", NOTHING_SPECIAL}
};

const nfs_function_desc_t mnt3_func_desc[] = {
  {mnt_Null, mnt_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "mnt_Null",
   NOTHING_SPECIAL},
  {mnt_Mnt, mnt3_Mnt_Free, (xdrproc_t) xdr_dirpath, (xdrproc_t) xdr_mountres3, "mnt_Mnt",
   NEEDS_CRED},
  {mnt_Dump, mnt_Dump_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_mountlist, "mnt_Dump",
   NOTHING_SPECIAL},
  {mnt_Umnt, mnt_Umnt_Free, (xdrproc_t) xdr_dirpath, (xdrproc_t) xdr_void, "mnt_Umnt",
   NOTHING_SPECIAL},
  {mnt_UmntAll, mnt_UmntAll_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
   "mnt_UmntAll", NOTHING_SPECIAL},
  {mnt_Export, mnt_Export_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_exports,
   "mnt_Export", NOTHING_SPECIAL}
};

#define nlm4_Unsupported nlm_Null
#define nlm4_Unsupported_Free nlm_Null_Free

#ifdef _USE_NLM
const nfs_function_desc_t nlm4_func_desc[] = {
  [NLMPROC4_NULL] = {
                     nlm_Null, nlm_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                     "nlm_Null", NOTHING_SPECIAL},
  [NLMPROC4_TEST] = {
                     nlm4_Test, nlm4_Test_Free, (xdrproc_t) xdr_nlm4_testargs,
                     (xdrproc_t) xdr_nlm4_testres, "nlm4_Test", NEEDS_CRED},
  [NLMPROC4_LOCK] = {
                     nlm4_Lock, nlm4_Lock_Free, (xdrproc_t) xdr_nlm4_lockargs,
                     (xdrproc_t) xdr_nlm4_res, "nlm4_Lock", NEEDS_CRED},
  [NLMPROC4_CANCEL] = {
                       nlm4_Cancel, nlm4_Cancel_Free, (xdrproc_t) xdr_nlm4_cancargs,
                       (xdrproc_t) xdr_nlm4_res, "nlm4_Cancel", NEEDS_CRED},
  [NLMPROC4_UNLOCK] = {
                       nlm4_Unlock, nlm4_Unlock_Free, (xdrproc_t) xdr_nlm4_unlockargs,
                       (xdrproc_t) xdr_nlm4_res, "nlm4_Unlock", NEEDS_CRED},
  [NLMPROC4_GRANTED] = {
                        nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                        (xdrproc_t) xdr_void, "nlm4_Granted", NOTHING_SPECIAL},
  [NLMPROC4_TEST_MSG] = {
                         nlm4_Test_Message, nlm4_Test_Message_Free,
                         (xdrproc_t) xdr_nlm4_testargs,
                         (xdrproc_t) xdr_void, "nlm4_Test_msg", NEEDS_CRED},
  [NLMPROC4_LOCK_MSG] = {
                         nlm4_Lock_Message, nlm4_Lock_Message_Free,
                         (xdrproc_t) xdr_nlm4_lockargs,
                         (xdrproc_t) xdr_void, "nlm4_Lock_msg", NEEDS_CRED},
  [NLMPROC4_CANCEL_MSG] = {
                           nlm4_Cancel_Message, nlm4_Cancel_Message_Free,
                           (xdrproc_t) xdr_nlm4_cancargs,
                           (xdrproc_t) xdr_void, "nlm4_Cancel_msg", NEEDS_CRED},
  [NLMPROC4_UNLOCK_MSG] = {
                           nlm4_Unlock_Message, nlm4_Unlock_Message_Free,
                           (xdrproc_t) xdr_nlm4_unlockargs,
                           (xdrproc_t) xdr_void, "nlm4_Unlock_msg", NEEDS_CRED},
  [NLMPROC4_GRANTED_MSG] = {
                            nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                            (xdrproc_t) xdr_void, "nlm4_Granted_msg", NOTHING_SPECIAL},
  [NLMPROC4_TEST_RES] = {
                         nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                         (xdrproc_t) xdr_void, "nlm4_Test_res", NOTHING_SPECIAL},
  [NLMPROC4_LOCK_RES] = {
                         nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                         (xdrproc_t) xdr_void, "nlm4_Lock_res", NOTHING_SPECIAL},
  [NLMPROC4_CANCEL_RES] = {
                           nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                           (xdrproc_t) xdr_void, "nlm4_Cancel_res", NOTHING_SPECIAL},
  [NLMPROC4_UNLOCK_RES] = {
                           nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                           (xdrproc_t) xdr_void, "nlm4_Unlock_res", NOTHING_SPECIAL},
  [NLMPROC4_GRANTED_RES] = {
                            nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
                            (xdrproc_t) xdr_void, "nlm4_Granted_res", NOTHING_SPECIAL},
  [NLMPROC4_SM_NOTIFY] = {
                          nlm4_Sm_Notify, nlm4_Sm_Notify_Free,
                          (xdrproc_t) xdr_nlm4_sm_notifyargs, (xdrproc_t) xdr_void,
                          "nlm4_sm_notify", NOTHING_SPECIAL},
  [17] = {
          nlm4_Unsupported, nlm4_Unsupported_Free,
          (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
          "nlm4_Granted_res", NOTHING_SPECIAL},
  [18] = {
          nlm4_Unsupported, nlm4_Unsupported_Free,
          (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
          "nlm4_Granted_res", NOTHING_SPECIAL},
  [19] = {
          nlm4_Unsupported, nlm4_Unsupported_Free,
          (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
          "nlm4_Granted_res", NOTHING_SPECIAL},
  [NLMPROC4_SHARE] {
                    nlm4_Unsupported, nlm4_Unsupported_Free,
                    (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                    "nlm4_Share", NOTHING_SPECIAL},
  [NLMPROC4_UNSHARE] = {
                        nlm4_Unsupported, nlm4_Unsupported_Free,
                        (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                        "nlm4_Unshare", NOTHING_SPECIAL},
  [NLMPROC4_NM_LOCK] = {
                        nlm4_Unsupported, nlm4_Unsupported_Free,
                        (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                        "nlm4_Nm_lock", NOTHING_SPECIAL},
  [NLMPROC4_FREE_ALL] = {
                         nlm4_Unsupported, nlm4_Unsupported_Free,
                         (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                         "nlm4_Free_all", NOTHING_SPECIAL},
};
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
const nfs_function_desc_t rquota1_func_desc[] = {
  [0] = {
         rquota_Null, rquota_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
         "rquota_Null", NOTHING_SPECIAL},
  [RQUOTAPROC_GETQUOTA] = {
                           rquota_getquota, rquota_getquota_Free,
                           (xdrproc_t) xdr_getquota_args,
                           (xdrproc_t) xdr_getquota_rslt, "rquota_Getquota", NEEDS_CRED},
  [RQUOTAPROC_GETACTIVEQUOTA] = {
                                 rquota_getactivequota, rquota_getactivequota_Free,
                                 (xdrproc_t) xdr_getquota_args,
                                 (xdrproc_t) xdr_getquota_rslt, "rquota_Getactivequota",
                                 NEEDS_CRED},
  [RQUOTAPROC_SETQUOTA] = {
                           rquota_setquota, rquota_setquota_Free,
                           (xdrproc_t) xdr_setquota_args,
                           (xdrproc_t) xdr_setquota_rslt, "rquota_Setactivequota",
                           NEEDS_CRED},
  [RQUOTAPROC_SETACTIVEQUOTA] = {
                                 rquota_setactivequota, rquota_setactivequota_Free,
                                 (xdrproc_t) xdr_setquota_args,
                                 (xdrproc_t) xdr_setquota_rslt, "rquota_Getactivequota",
                                 NEEDS_CRED}
};

const nfs_function_desc_t rquota2_func_desc[] = {
  [0] = {
         rquota_Null, rquota_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
         "rquota_Null", NOTHING_SPECIAL},
  [RQUOTAPROC_GETQUOTA] = {
                           rquota_getquota, rquota_getquota_Free,
                           (xdrproc_t) xdr_ext_getquota_args,
                           (xdrproc_t) xdr_getquota_rslt, "rquota_Ext_Getquota",
                           NEEDS_CRED},
  [RQUOTAPROC_GETACTIVEQUOTA] = {
                                 rquota_getactivequota, rquota_getactivequota_Free,
                                 (xdrproc_t) xdr_ext_getquota_args,
                                 (xdrproc_t) xdr_getquota_rslt,
                                 "rquota_Ext_Getactivequota", NEEDS_CRED},
  [RQUOTAPROC_SETQUOTA] = {
                           rquota_setquota, rquota_setquota_Free,
                           (xdrproc_t) xdr_ext_setquota_args,
                           (xdrproc_t) xdr_setquota_rslt, "rquota_Ext_Setactivequota",
                           NEEDS_CRED},
  [RQUOTAPROC_SETACTIVEQUOTA] = {
                                 rquota_setactivequota, rquota_setactivequota_Free,
                                 (xdrproc_t) xdr_ext_setquota_args,
                                 (xdrproc_t) xdr_setquota_rslt,
                                 "rquota_Ext_Getactivequota", NEEDS_CRED}
};

#endif

#ifdef _USE_TIRPC
void Svc_dg_soft_destroy(register SVCXPRT * xprt);
#else
void Svcudp_soft_destroy(register SVCXPRT * xprt);
#endif

/**
 * nfs_Cleanup_request_data: clean the data associated with a request 
 *
 * This function is used to clean the nfs_request_data for a worker. These data are used by the
 * worker for RPC processing.
 * 
 * @param param A structure of type nfs_worker_parameter_t with all the necessary information related to a worker
 * @param pdata Pointer to the data to be initialized.
 * 
 * @return 0 if successfull, -1 otherwise. 
 *
 */
void nfs_Cleanup_request_data(nfs_request_data_t * pdata)
{
  pdata->ipproto = 0;

  pdata->tcp_xprt = NULL;
  pdata->xprt = NULL;
}                               /* nfs_Cleanup_request_data */

struct timeval time_diff(struct timeval time_from, struct timeval time_to)
{

  struct timeval result;

  if(time_to.tv_usec < time_from.tv_usec)
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec - 1;
      result.tv_usec = 1000000 + time_to.tv_usec - time_from.tv_usec;
    }
  else
    {
      result.tv_sec = time_to.tv_sec - time_from.tv_sec;
      result.tv_usec = time_to.tv_usec - time_from.tv_usec;
    }

  return result;
}

/**
 * nfs_rpc_execute: main rpc dispatcher routine
 *
 * This is the regular RPC dispatcher that every RPC server should include. 
 *
 * @param pnfsreq [INOUT] pointer to nfs request 
 *
 * @return nothing (void function)
 *
 */
static void nfs_rpc_execute(nfs_request_data_t * preqnfs,
                            nfs_worker_data_t * pworker_data)
{
  unsigned int rpcxid = 0;
  nfs_function_desc_t funcdesc;
  exportlist_t *pexport = NULL;
  nfs_arg_t arg_nfs;
  nfs_res_t res_nfs;
  short exportid;
  LRU_list_t *lru_dupreq = NULL;
  struct svc_req *ptr_req = &preqnfs->req;
  SVCXPRT *ptr_svc = preqnfs->xprt;
  nfs_res_t previous_res_nfs;
  nfs_stat_type_t stat_type;
  struct sockaddr_in hostaddr;
  struct sockaddr_in *phostaddr;

  struct sockaddr_in *tmp_childaddr;
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in6 *tmp_hostaddr_inet6;
  struct sockaddr_in6 *tmp_childaddr_inet6;
#endif                          /* _USE_TIRPC_IPV6 */

#ifdef _USE_TIRPC
  struct netbuf *pnetbuf;
#endif
  int rc;
  int do_dupreq_cache;
  int status;
  unsigned int i;
  exportlist_client_entry_t related_client;

#ifdef _DEBUG_MEMLEAKS
  static int nb_iter_memleaks = 0;
#endif

  struct timeval timer_start;
  struct timeval timer_end;
  struct timeval timer_diff;
  nfs_request_latency_stat_t latency_stat;

  /* daemon is terminating, do not process any new request */
  if(nfs_do_terminate)
    return;

  /* Get the value from the worker data */
  lru_dupreq = pworker_data->duplicate_request;

  LogFullDebug(COMPONENT_DISPATCH, "NFS DISPATCH: Program %d, Version %d, Function %d",
               ptr_req->rq_prog, ptr_req->rq_vers, ptr_req->rq_proc);

  /* initializing RPC structure */
  memset(&arg_nfs, 0, sizeof(arg_nfs));
  memset(&res_nfs, 0, sizeof(res_nfs));

  /* If we reach this point, there was no dupreq cache hit or no dup req cache was necessary */
  if(ptr_req->rq_prog == nfs_param.core_param.nfs_program)
    {
      switch (ptr_req->rq_vers)
        {
        case NFS_V2:
          if(ptr_req->rq_proc > NFSPROC_STATFS)
            {
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = nfs2_func_desc[ptr_req->rq_proc];
          break;

        case NFS_V3:
          if(ptr_req->rq_proc > NFSPROC3_COMMIT)
            {
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = nfs3_func_desc[ptr_req->rq_proc];
          break;

        case NFS_V4:
          if(ptr_req->rq_proc > NFSPROC4_COMPOUND)
            {
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = nfs4_func_desc[ptr_req->rq_proc];

          /* The export list as a whole is given ti NFSv4 request since NFSv4 is capable of junction traversal */
          pexport = nfs_param.pexportlist;
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: NFS Protocol version %d unknown", ptr_req->rq_vers);
          svcerr_decode(ptr_svc);
          return;
          break;
        }
    }
  else if(ptr_req->rq_prog == nfs_param.core_param.mnt_program)
    {
      if(ptr_req->rq_proc > MOUNTPROC3_EXPORT)  /* functions are almost the same in MOUNTv1 and MOUNTv3 */
        {
          svcerr_decode(ptr_svc);
          return;
        }

      switch (ptr_req->rq_vers)
        {
        case MOUNT_V1:
          funcdesc = mnt1_func_desc[ptr_req->rq_proc];
          break;

        case MOUNT_V3:
          funcdesc = mnt3_func_desc[ptr_req->rq_proc];
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: MOUNT Protocol version %d unknown",
                     ptr_req->rq_vers);
          svcerr_decode(ptr_svc);
          return;
          break;

        }                       /* switch( ptr_req->vers ) */
    }
#ifdef _USE_NLM
  else if(ptr_req->rq_prog == nfs_param.core_param.nlm_program)
    {

      switch (ptr_req->rq_vers)
        {
        case NLM4_VERS:
          if(ptr_req->rq_proc > NLMPROC4_FREE_ALL)
            {
              LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: NLM proc number %d unknown", ptr_req->rq_proc);
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = nlm4_func_desc[ptr_req->rq_proc];
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: NLM Protocol version %d unknown", ptr_req->rq_vers);
          svcerr_decode(ptr_svc);
          return;
          break;
        }                       /* switch( ptr_req->vers ) */
    }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
  else if(ptr_req->rq_prog == nfs_param.core_param.rquota_program)
    {

      switch (ptr_req->rq_vers)
        {
        case RQUOTAVERS:
          if(ptr_req->rq_proc > RQUOTAPROC_SETACTIVEQUOTA)
            {
              LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: RQUOTA proc number %d unknown",
                         ptr_req->rq_proc);
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = rquota1_func_desc[ptr_req->rq_proc];
          break;

        case EXT_RQUOTAVERS:
          if(ptr_req->rq_proc > RQUOTAPROC_SETACTIVEQUOTA)
            {
              LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: EXT_RQUOTA proc number %d unknown",
                         ptr_req->rq_proc);
              svcerr_decode(ptr_svc);
              return;
            }
          funcdesc = rquota2_func_desc[ptr_req->rq_proc];
          break;

        default:
          /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
          LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: RQUOTA Protocol version %d unknown",
                     ptr_req->rq_vers);
          svcerr_decode(ptr_svc);
          return;
          break;
        }                       /* switch( ptr_req->vers ) */
    }
#endif                          /* _USE_QUOTA */
  else
    {
      /* We should never go there (this situation is filtered in nfs_rpc_getreq) */
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: protocol %d is not managed", ptr_req->rq_prog);
      svcerr_decode(ptr_svc);
      return;
    }                           /* switch( ptr_req->rq_prog ) */

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
  LogFullDebug(COMPONENT_DISPATCH, "Before svc_getargs on socket %u, xprt=%p",
               ptr_svc->xp_fd, ptr_svc);
#else
  LogFullDebug(COMPONENT_DISPATCH, "Before svc_getargs on socket %u, xprt=%p",
               ptr_svc->xp_sock, ptr_svc);
#endif

  if(svc_getargs(ptr_svc, funcdesc.xdr_decode_func, (caddr_t) & arg_nfs) == FALSE)
    {
      LogMajor(COMPONENT_DISPATCH,
               "NFS DISPATCHER: FAILURE: Error while calling svc_getargs");
      svcerr_decode(ptr_svc);
      return;
    }

  /* Tag myself as currently processing this request */
  rpcxid = get_rpc_xid(ptr_req);

  /* Is this request already managed by another thread ? 
   * In this case, do not handle the request to avoid thread starvation 
   * because this shows that the FSAL may hang in a specific call */
  for(i = 0; i < nfs_param.core_param.nb_worker; i++)
    {
      /* First check if the xid is the same. */
      if((workers_data[i].current_xid == rpcxid) && (workers_data[i].current_xid != 0)
         && cmp_sockaddr((struct sockaddr *)&hostaddr,
                         (struct sockaddr *)&workers_data[i].hostaddr))
        {
          LogMajor(COMPONENT_DISPATCH,
                   "Dupreq #%u was asked for process since another thread manage it, reject for avoiding threads starvation...",
                   rpcxid);
          /* Free the arguments */
          if(!SVC_FREEARGS(ptr_svc, funcdesc.xdr_decode_func, (caddr_t) & arg_nfs))
            {
              LogCrit(COMPONENT_DISPATCH,
                      "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                      funcdesc.funcname);
            }
          return;
        }
    }

  /* Manage this request, so keeps its xid in worker_data specific structure */
  pworker_data->current_xid = rpcxid;

  /* Getting the xid from the RPC request (this value is used for RPC duplicate request hash */
  if((do_dupreq_cache = funcdesc.dispatch_behaviour & CAN_BE_DUP))
    {
      rpcxid = get_rpc_xid(ptr_req);
      LogFullDebug(COMPONENT_DISPATCH, "NFS DISPATCH: Request has xid=%u", rpcxid);
      previous_res_nfs = nfs_dupreq_get(rpcxid, &status);
      if(status == DUPREQ_SUCCESS)
        {
          /* Request was known, use the previous reply */
          LogFullDebug(COMPONENT_DISPATCH, "NFS DISPATCHER: DupReq Cache Hit: using previous reply, rpcxid=%u",
                       rpcxid);

#if defined( _USE_TIRPC ) || defined( _FREEBSD )
          LogFullDebug(COMPONENT_DISPATCH, "Before svc_sendreply on socket %u (dup req)",
                       ptr_svc->xp_fd);
#else
          LogFullDebug(COMPONENT_DISPATCH, "Before svc_sendreply on socket %u (dup req)",
                       ptr_svc->xp_sock);
#endif
          if(svc_sendreply
             (ptr_svc, funcdesc.xdr_encode_func, (caddr_t) & previous_res_nfs) == FALSE)
            {
              LogEvent(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply");
              svcerr_decode(ptr_svc);
            }
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
          LogFullDebug(COMPONENT_DISPATCH, "After svc_sendreply on socket %u (dup req)",
                       ptr_svc->xp_fd);
#else
          LogFullDebug(COMPONENT_DISPATCH, "After svc_sendreply on socket %u (dup req)",
                       ptr_svc->xp_sock);
#endif
          return;               /* exit the function */
        }
    }

  /* Get the export entry */
  if(ptr_req->rq_prog == nfs_param.core_param.nfs_program)
    {
      /* The NFSv2 and NFSv3 functions'arguments always begin with the file handle (but not the NULL function)
       * this hook is used to get the fhandle with the arguments and so 
       * determine the export entry to be used.
       * In NFSv4, junction traversal is managed by the protocol itself so the whole
       * export list is provided to NFSv4 request. */

      switch (ptr_req->rq_vers)
        {
        case NFS_V2:
          if(ptr_req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs2_FhandleToExportId((fhandle2 *) & arg_nfs);

              if(exportid < 0)
                {
                  /* Bad argument */
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                }

              if((pexport =
                  nfs_Get_export_by_id(nfs_param.pexportlist, exportid)) == NULL)
                {
                  /* Reject the request for authentication reason (incompatible file handle */
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                }
              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for dirname=%s as exportid=%d",
                           pexport->dirname, pexport->id);
            }

          break;

        case NFS_V3:
          if(ptr_req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs3_FhandleToExportId((nfs_fh3 *) & arg_nfs);
              if(exportid < 0)
                {
                  char dumpfh[1024];
                  /* Reject the request for authentication reason (incompatible file handle) */
                  LogMajor(COMPONENT_DISPATCH,
                           "/!\\ | Host 0x%x = %d.%d.%d.%d has badly formed file handle, vers=%d, proc=%d FH=%s",
                           ntohs(hostaddr.sin_addr.s_addr),
                           (ntohl(hostaddr.sin_addr.s_addr) & 0xFF000000) >> 24,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x00FF0000) >> 16,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x0000FF00) >> 8,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x000000FF),
                           ptr_req->rq_vers, ptr_req->rq_proc, dumpfh);
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                  return;
                }

              if((pexport =
                  nfs_Get_export_by_id(nfs_param.pexportlist, exportid)) == NULL)
                {
                  char dumpfh[1024];
                  /* Reject the request for authentication reason (incompatible file handle) */
                  LogMajor(COMPONENT_DISPATCH,
                           "/!\\ | Host 0x%x = %d.%d.%d.%d has badly formed file handle, vers=%d, proc=%d FH=%s",
                           ntohs(hostaddr.sin_addr.s_addr),
                           (ntohl(hostaddr.sin_addr.s_addr) & 0xFF000000) >> 24,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x00FF0000) >> 16,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x0000FF00) >> 8,
                           (ntohl(hostaddr.sin_addr.s_addr) & 0x000000FF),
                           ptr_req->rq_vers, ptr_req->rq_proc, dumpfh);
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                  return;
                }
              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for dirname=%s as exportid=%d",
                           pexport->dirname, pexport->id);
            }
          break;

        case NFS_V4:
          pexport = nfs_param.pexportlist;
          break;
        }                       /* switch( ptr_req->rq_vers ) */
    }
  else if(ptr_req->rq_prog == nfs_param.core_param.mnt_program)
    {
      /* Always use the whole export list for mount protocol */
      pexport = nfs_param.pexportlist;
    }                           /* switch( ptr_req->rq_prog ) */
#ifdef _USE_NLM
  else if(ptr_req->rq_prog == nfs_param.core_param.nlm_program)
    {
      /* Always use the whole export list for NLM protocol (FIXME !! Verify) */
      pexport = nfs_param.pexportlist;
    }
#endif                          /* _USE_NLM */
#ifdef _USE_QUOTA
  else if(ptr_req->rq_prog == nfs_param.core_param.rquota_program)
    {
      /* Always use the whole export list for NLM protocol (FIXME !! Verify) */
      pexport = nfs_param.pexportlist;
    }
#endif                          /* _USE_QUOTA */

  /* Do not call a MAKES_WRITE function on a read-only export entry */
  if((funcdesc.dispatch_behaviour & MAKES_WRITE)
     && (pexport->access_type == ACCESSTYPE_RO ||
         pexport->access_type == ACCESSTYPE_MDONLY_RO))
    {
      if(ptr_req->rq_prog == nfs_param.core_param.nfs_program)
        {
          if(ptr_req->rq_vers == NFS_V2)
            {
              /* All the nfs_res structure in V2 have the status at the same place (because it is an union) */
              res_nfs.res_attr2.status = NFSERR_ROFS;
              rc = NFS_REQ_OK;  /* Processing of the request is done */
            }
          else
            {
              /* V3 request */
              /* All the nfs_res structure in V2 have the status at the same place, and so does V3 ones */
              res_nfs.res_attr2.status = (nfsstat2) NFS3ERR_ROFS;
              rc = NFS_REQ_OK;  /* Processing of the request is done */
            }
        }
      else                      /* unexpected protocol (mount doesn't make write) */
        rc = NFS_REQ_DROP;
    }
  else
    {
      /* This is not a MAKES_WRITE call done on a read-only export entry */

      /* 
       * It is now time for checking if export list allows the machine to perform the request 
       */

      /* Ask the RPC layer for the adresse of the machine */
#ifdef _USE_TIRPC
      /*
       * In tirpc svc_getcaller is deprecated and replaced by
       * svc_getrpccaller().
       * svc_getrpccaller return a struct netbuf (see rpc/types.h) instead
       * of a struct sockaddr_in directly.
       */
      pnetbuf = svc_getrpccaller(ptr_svc);
      memcpy((char *)&pworker_data->hostaddr, (char *)pnetbuf->buf, pnetbuf->len);
#else
      phostaddr = svc_getcaller(ptr_svc);
      memcpy((char *)&pworker_data->hostaddr, (char *)phostaddr,
             sizeof(pworker_data->hostaddr));
#endif

      /* Check if client is using a privileged port, but only for NFS protocol */
      if(ptr_req->rq_prog == nfs_param.core_param.nfs_program && ptr_req->rq_proc != 0)
        {
          if((pexport->options & EXPORT_OPTION_PRIVILEGED_PORT) &&
             (ntohs(hostaddr.sin_port) >= IPPORT_RESERVED))
            {
              LogEvent(COMPONENT_DISPATCH,
                       "/!\\ | Port %d is too high for this export entry, rejecting client",
                       hostaddr.sin_port);
              svcerr_auth(ptr_svc, AUTH_TOOWEAK);
              pworker_data->current_xid = 0;    /* No more xid managed */
              return;
            }
        }

      if(nfs_export_check_access(&pworker_data->hostaddr,
                                 ptr_req,
                                 pexport,
                                 nfs_param.core_param.nfs_program,
                                 nfs_param.core_param.mnt_program,
                                 pworker_data->ht_ip_stats,
                                 pworker_data->ip_stats_pool, &related_client) == FALSE)
        {
          LogEvent(COMPONENT_DISPATCH,
                   "/!\\ | Host 0x%x = %d.%d.%d.%d is not allowed to access this export entry, vers=%d, proc=%d",
                   ntohs(phostaddr->sin_addr.s_addr),
                   (ntohl(phostaddr->sin_addr.s_addr) & 0xFF000000) >> 24,
                   (ntohl(phostaddr->sin_addr.s_addr) & 0x00FF0000) >> 16,
                   (ntohl(phostaddr->sin_addr.s_addr) & 0x0000FF00) >> 8,
                   (ntohl(phostaddr->sin_addr.s_addr) & 0x000000FF),
                   ptr_req->rq_vers, ptr_req->rq_proc);
          /* svcerr_auth( ptr_svc, AUTH_TOOWEAK ) ; */
          pworker_data->current_xid = 0;        /* No more xid managed */
          return;
        }

      /* Do the authentication stuff, if needed */
      if(funcdesc.dispatch_behaviour & NEEDS_CRED)
        {
          if(nfs_build_fsal_context
             (ptr_req, &related_client, pexport,
              &pworker_data->thread_fsal_context) == FALSE)
            {
              svcerr_auth(ptr_svc, AUTH_TOOWEAK);
              pworker_data->current_xid = 0;    /* No more xid managed */
              return;
            }
        }

      /* processing */
      memset(&timer_start, 0, sizeof(struct timeval));
      memset(&timer_end, 0, sizeof(struct timeval));
      memset(&timer_diff, 0, sizeof(struct timeval));

      gettimeofday(&timer_start, NULL);

      LogFullDebug(COMPONENT_NFSPROTO, "NFS DISPATCHER: Calling service function %s start_time %llu.%.6llu",
                   funcdesc.funcname, timer_start.tv_sec, timer_start.tv_usec);
      rc = funcdesc.service_function(&arg_nfs, pexport, &pworker_data->thread_fsal_context, &(pworker_data->cache_inode_client), pworker_data->ht, ptr_req, &res_nfs);  /* BUGAZOMEU Un appel crade pour debugger */

      gettimeofday(&timer_end, NULL);
      timer_diff = time_diff(timer_start, timer_end);

      LogFullDebug(COMPONENT_DISPATCH, "NFS DISPATCHER: Function %s exited with status %d end_time %llu.%.6llu latency %llu.%.6llu",
                   funcdesc.funcname, rc, timer_end.tv_sec, timer_end.tv_usec, timer_diff.tv_sec, timer_diff.tv_usec);
    }

  /* Perform statistics here */
  stat_type = (rc == NFS_REQ_OK) ? GANESHA_STAT_SUCCESS : GANESHA_STAT_DROP;

  latency_stat.latency = timer_diff.tv_sec * 1000000 + timer_diff.tv_usec; /* microseconds */

  nfs_stat_update(stat_type, &(pworker_data->stats.stat_req), ptr_req, &latency_stat);
  pworker_data->stats.nb_total_req += 1;

  /* Perform NFSv4 operations statistics if required */
  if(ptr_req->rq_vers == NFS_V4)
    if(ptr_req->rq_proc == NFSPROC4_COMPOUND)
      nfs4_op_stat_update(&arg_nfs, &res_nfs, &(pworker_data->stats.stat_req));

  pworker_data->current_xid = 0;        /* No more xid managed */

  /* If request is dropped, no return to the client */
  if(rc == NFS_REQ_DROP)
    {
      /* The request was dropped */
      LogEvent(COMPONENT_DISPATCH,
               "Drop request rpc_xid=%u, program %u, version %u, function %u",
               rpcxid, ptr_req->rq_prog, ptr_req->rq_vers, ptr_req->rq_proc);
    }
  else
    {
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      LogFullDebug(COMPONENT_DISPATCH, "Before svc_sendreply on socket %u",
                   ptr_svc->xp_fd);
#else
      LogFullDebug(COMPONENT_DISPATCH, "Before svc_sendreply on socket %u",
                   ptr_svc->xp_sock);
#endif

      /* encoding the result on xdr output */
      if(svc_sendreply(ptr_svc, funcdesc.xdr_encode_func, (caddr_t) & res_nfs) == FALSE)
        {
          LogEvent(COMPONENT_DISPATCH,
                   "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply");
          svcerr_decode(ptr_svc);
          return;
        }
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
      LogFullDebug(COMPONENT_DISPATCH, "After svc_sendreply on socket %u", ptr_svc->xp_fd);
#else
      LogFullDebug(COMPONENT_DISPATCH, "After svc_sendreply on socket %u",
                   ptr_svc->xp_sock);
#endif

      /* store in dupreq cache if needed */
      if(do_dupreq_cache)
        {
          if(nfs_dupreq_add(rpcxid,
                            ptr_req,
                            &res_nfs,
                            lru_dupreq, &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
            {
              LogEvent(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: FAILURE: Bad insertion in dupreq cache");
            }
        }
    }
  /* Free the allocated resources once the work is done */

  /* Free the arguments */
  if(!SVC_FREEARGS(ptr_svc, funcdesc.xdr_decode_func, (caddr_t) & arg_nfs))
    {
      LogCrit(COMPONENT_DISPATCH, "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
              funcdesc.funcname);
    }

  /* Free the reply.
   * This should not be done if the request is dupreq cached because this will
   * mark the dupreq cached info eligible for being reuse by other requests */
  if(!do_dupreq_cache)
    {
      /* Free only the non dropped requests */
      if(rc == NFS_REQ_OK)
        funcdesc.free_function(&res_nfs);
    }
#ifdef _DEBUG_MEMLEAKS
  if(nb_iter_memleaks > 1000)
    {
      nb_iter_memleaks = 0;
      /* BuddyDumpMem( stdout ) ; */
      nfs_debug_debug_label_info();

      LogFullDebug(COMPONENT_MEMLEAKS,
                "Stats de ce thread: total mnt1=%u mnt3=%u nfsv2=%u nfsv3=%u nfsv4=%u",
                pworker_data->stats.stat_req.nb_mnt1_req,
                pworker_data->stats.stat_req.nb_mnt3_req,
                pworker_data->stats.stat_req.nb_nfs2_req,
                pworker_data->stats.stat_req.nb_nfs3_req,
                pworker_data->stats.stat_req.nb_nfs4_req);

    }
  else
    nb_iter_memleaks += 1;
#endif

  return;
}                               /* nfs_rpc_execute */

/**
 * nfs_Init_worker_data: Init the data associated with a worker instance.
 *
 * This function is used to init the nfs_worker_data for a worker thread. These data are used by the
 * worker for RPC processing.
 * 
 * @param param A structure of type nfs_worker_parameter_t with all the necessary information related to a worker
 * @param pdata Pointer to the data to be initialized.
 * 
 * @return 0 if successfull, -1 otherwise. 
 *
 */

int nfs_Init_worker_data(nfs_worker_data_t * pdata)
{
  LRU_status_t status = LRU_LIST_SUCCESS;

  if(pthread_mutex_init(&(pdata->mutex_req_condvar), NULL) != 0)
    return -1;

  if(pthread_mutex_init(&(pdata->request_pool_mutex), NULL) != 0)
    return -1;

  if(pthread_cond_init(&(pdata->req_condvar), NULL) != 0)
    return -1;

  if(pthread_mutex_init(&(pdata->mutex_export_condvar), NULL) != 0)
    return -1;

  if(pthread_cond_init(&(pdata->export_condvar), NULL) != 0)
    return -1;

  if((pdata->pending_request =
      LRU_Init(nfs_param.worker_param.lru_param, &status)) == NULL)
    {
      LogError(COMPONENT_DISPATCH, ERR_LRU, ERR_LRU_LIST_INIT, status);
      return -1;
    }

  if((pdata->duplicate_request =
      LRU_Init(nfs_param.worker_param.lru_dupreq, &status)) == NULL)
    {
      LogError(COMPONENT_DISPATCH, ERR_LRU, ERR_LRU_LIST_INIT, status);
      return -1;
    }

  pdata->passcounter = 0;
  pdata->is_ready = FALSE;
  pdata->gc_in_progress = FALSE;
  pdata->reparse_exports_in_progress = FALSE;
  pdata->waiting_for_exports = FALSE;

  return 0;
}                               /* nfs_Init_worker_data */

/**
 * worker_thread: The main function for a worker thread
 *
 * This is the body of the worker thread. Its starting arguments are located in global array worker_data. The 
 * argument is no pointer but the worker's index. It then uses this index to address its own worker data in 
 * the array.
 * 
 * @param IndexArg the index for the worker thread, in fact an integer cast as a void *
 * 
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *worker_thread(void *IndexArg)
{
  nfs_worker_data_t *pmydata;
  nfs_request_data_t *pnfsreq;
  LRU_entry_t *pentry;
  char *cred_area;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  SVCXPRT *xprt;
  enum auth_stat why;
  long index;
  bool_t found = FALSE;
  int rc = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  unsigned int gc_allowed = FALSE;
  char thr_name[128];
  char auth_str[AUTH_STR_LEN];
  bool_t no_dispatch = FALSE;
  fsal_status_t fsal_status;
#ifdef _USE_GSSRPC
  struct rpc_gss_cred *gc;
#endif

  index = (long)IndexArg;
  pmydata = &(workers_data[index]);

  snprintf(thr_name, 128, "worker#%ld", index);
  SetNameFunction(thr_name);

  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%d : Starting, nb_entry=%d",
           index, pmydata->pending_request->nb_entry);
  /* Initialisation of the Buddy Malloc */
  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%d : Initialization of memory manager", index);

#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH, "NFS WORKER #%d: Memory manager could not be initialized, exiting...",
                 index);
      exit(1);
    }
  LogEvent(COMPONENT_DISPATCH, "NFS WORKER #%d: Memory manager successfully initialized",
           index);
#endif

  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%d: my pthread id is %p", index,
           (caddr_t) pthread_self());

  /* Initialisation of credential for current thread */
  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%d: Initialization of thread's credential",
           index);
  if(FSAL_IS_ERROR(FSAL_InitClientContext(&pmydata->thread_fsal_context)))
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH, "NFS  WORKER #%d: Error initializing thread's credential", index);
      exit(1);
    }

  /* Init the Cache inode client for this worker */
  if(cache_inode_client_init(&pmydata->cache_inode_client,
                             nfs_param.cache_layers_param.cache_inode_client_param,
                             index, pmydata))
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH,
           "NFS WORKER #%d: Cache Inode client could not be initialized, exiting...",
           index);
      exit(1);
    }
  LogDebug(COMPONENT_DISPATCH,
           "NFS WORKER #%d: Cache Inode client successfully initialized", index);

#ifdef _USE_MFSL
  if(FSAL_IS_ERROR(MFSL_GetContext(&pmydata->cache_inode_client.mfsl_context,
                                   &pmydata->thread_fsal_context)))
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH, "NFS  WORKER #%d: Error initing MFSL", index);
      exit(1);
    }
#endif

  /* Init the Cache content client for this worker */
  if(cache_content_client_init(&pmydata->cache_content_client,
                               nfs_param.cache_layers_param.cache_content_client_param))
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH,
           "NFS WORKER #%d: Cache Content client could not be initialized, exiting...",
           index);
      exit(1);
    }
  LogDebug(COMPONENT_DISPATCH,
           "NFS WORKER #%d: Cache Content client successfully initialized", index);

  /* _USE_PNFS */

  /* The worker thread is not garbagging anything at the time it starts */
  pmydata->gc_in_progress = FALSE;

  /* Bind the data cache client to the inode cache client */
  pmydata->cache_inode_client.pcontent_client = (caddr_t) & pmydata->cache_content_client;

#ifdef _USE_PNFS
  /* Init the pNFS engine for each worker */
  if(pnfs_init(&pmydata->cache_inode_client.pnfsclient, &nfs_param.pnfs_param.layoutfile))
    {
      /* Failed init */
      LogCrit(COMPONENT_DISPATCH,
          "NFS WORKER #%d: pNFS engine could not be initialized, exiting...", index);
      exit(1);
    }
  LogDebug(COMPONENT_DISPATCH,
           "NFS WORKER #%d: pNFS engine successfully initialized", index);
#endif
  /* notify dispatcher it is ready */
  pmydata->is_ready = TRUE;

  LogEvent(COMPONENT_DISPATCH, "NFS WORKER #%d successfully initialized", index);

  /* Worker's infinite loop */
  while(1)
    {

      /* update memory and FSAL stats,
       * twice often than stats display.
       */
      if(time(NULL) - pmydata->stats.last_stat_update >
         (int)nfs_param.core_param.stats_update_delay / 2)
        {

          FSAL_get_stats(&pmydata->stats.fsal_stats, FALSE);

#ifndef _NO_BUDDY_SYSTEM
          BuddyGetStats(&pmydata->stats.buddy_stats);
#endif
          /* reset last stat */
          pmydata->stats.last_stat_update = time(NULL);
        }

      /* Wait on condition variable for work to be done */
      LogDebug(COMPONENT_DISPATCH,
               "NFS WORKER #%d: waiting for requests to process, nb_entry=%d, nb_invalid=%d",
               index, pmydata->pending_request->nb_entry,
               pmydata->pending_request->nb_invalid);
      P(pmydata->mutex_req_condvar);
      while(pmydata->pending_request->nb_entry == pmydata->pending_request->nb_invalid 
	    || pmydata->reparse_exports_in_progress == TRUE)
	{
	  /* block because someone is changing the exports list */
	  if (pmydata->reparse_exports_in_progress == TRUE)
	    {
	      pmydata->waiting_for_exports = TRUE;
	      P(pmydata->mutex_export_condvar);
	      pthread_cond_wait(&(pmydata->export_condvar), &(pmydata->mutex_export_condvar));
	      pmydata->waiting_for_exports = FALSE;
	      V(pmydata->mutex_export_condvar);
	    }
	  /* block until there are requests to process in the queue */
	  else
	    pthread_cond_wait(&(pmydata->req_condvar), &(pmydata->mutex_req_condvar));
	}
      LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%d: Processing a new request", index);
      V(pmydata->mutex_req_condvar);

      found = FALSE;
      P(pmydata->request_pool_mutex);
      for(pentry = pmydata->pending_request->LRU; pentry != NULL; pentry = pentry->next)
        {
          if(pentry->valid_state == LRU_ENTRY_VALID)
            {
              found = TRUE;
              break;
            }
        }
      V(pmydata->request_pool_mutex);

      if(!found)
        {
          LogMajor(COMPONENT_DISPATCH, "NFS WORKER #%d : No pending request available",
                   index);
          continue;             /* return to main loop */
        }

      pnfsreq = (nfs_request_data_t *) (pentry->buffdata.pdata);

      LogDebug(COMPONENT_DISPATCH,
               "NFS WORKER #%d : I have some work to do, length=%d, invalid=%d",
               index, pmydata->pending_request->nb_entry,
               pmydata->pending_request->nb_invalid);

#if defined(_USE_TIRPC) || defined( _FREEBSD )
      if(pnfsreq->xprt->xp_fd == 0)
      {
        LogFullDebug(COMPONENT_DISPATCH, "NFS WORKER #%d:No RPC management, xp_fd==0",
                     index);
      }
#else
      if(pnfsreq->xprt->xp_sock == 0)
      {
        LogFullDebug(COMPONENT_DISPATCH, "NFS WORKER #%d:No RPC management, xp_sock==0",
                     index);
      }
#endif
      else
        {
          /* Set pointers */
          cred_area = pnfsreq->cred_area;
          pmsg = &(pnfsreq->msg);
          preq = &(pnfsreq->req);
          xprt = pnfsreq->xprt;

          /*do */
          {
            if(pnfsreq->status)
              {
                preq->rq_xprt = pnfsreq->xprt;
                preq->rq_prog = pmsg->rm_call.cb_prog;
                preq->rq_vers = pmsg->rm_call.cb_vers;
                preq->rq_proc = pmsg->rm_call.cb_proc;
                LogFullDebug(COMPONENT_DISPATCH, "Prog = %d, vers = %d, proc = %d xprt=%p",
                             pmsg->rm_call.cb_prog, pmsg->rm_call.cb_vers,
                             pmsg->rm_call.cb_proc, preq->rq_xprt);
                /* Restore previously save GssData */
#ifdef _USE_GSSRPC
                no_dispatch = FALSE;
                if((why = Rpcsecgss__authenticate(preq, pmsg, &no_dispatch)) != AUTH_OK)
#else
                if((why = _authenticate(preq, pmsg)) != AUTH_OK)
#endif
                  {
                    auth_stat2str(why, auth_str);
                    LogEvent(COMPONENT_DISPATCH,
                             "Could not authenticate request... rejecting with AUTH_STAT=%s",
                             auth_str);
                    svcerr_auth(xprt, why);
                  }
                else
                  {
#ifdef _USE_GSSRPC
                    if(preq->rq_xprt->xp_verf.oa_flavor == RPCSEC_GSS)
                      {
                        gc = (struct rpc_gss_cred *)preq->rq_clntcred;
                        LogFullDebug(COMPONENT_DISPATCH,
                            "========> no_dispatch=%u gc->gc_proc=%u RPCSEC_GSS_INIT=%u RPCSEC_GSS_CONTINUE_INIT=%u RPCSEC_GSS_DATA=%u RPCSEC_GSS_DESTROY=%u",
                             no_dispatch, gc->gc_proc, RPCSEC_GSS_INIT,
                             RPCSEC_GSS_CONTINUE_INIT, RPCSEC_GSS_DATA,
                             RPCSEC_GSS_DESTROY);
                      }
#endif
                    /* A few words of explanation are required here:
                     * In authentication is AUTH_NONE or AUTH_UNIX, then the value of no_dispatch remains FALSE and the request is proceeded normally
                     * If authentication is RPCSEC_GSS, no_dispatch may have value TRUE, this means that gc->gc_proc != RPCSEC_GSS_DATA and that the 
                     * message is in fact an internal negociation message from RPCSEC_GSS using GSSAPI. It then should not be proceed by the worker and
                     * SCV_STAT should be returned to the dispatcher */
                    if(no_dispatch == FALSE)
                      {
                        if(preq->rq_prog == nfs_param.core_param.nfs_program)
                          {
                            /* If we go there, preq->rq_prog ==  nfs_param.core_param.nfs_program */
/* FSAL_PROXY supports only NFSv4 except if handle mapping is enabled */
#if ! defined( _USE_PROXY ) || defined( _HANDLE_MAPPING )
                            if((preq->rq_vers != NFS_V2) &&
                               (preq->rq_vers != NFS_V3) && (preq->rq_vers != NFS_V4))
#else
                            if(preq->rq_vers != NFS_V4)
#endif
                              {
                                LogFullDebug(COMPONENT_DISPATCH,
                                             "/!\\ | Invalid NFS Version #%d",
                                             preq->rq_vers);
#if ! defined( _USE_PROXY ) || defined( _HANDLE_MAPPING )
                                svcerr_progvers(xprt, NFS_V2, NFS_V4);  /* Bad NFS version */
#else
                                svcerr_progvers(xprt, NFS_V4, NFS_V4);  /* Bad NFS version */
#endif
                              }
                            else
                              {
                                /* Actual work starts here */
                                nfs_rpc_execute(pnfsreq, pmydata);
                              }
                          }     /* if( preq->rq_prog ==  nfs_param.core_param.nfs_program ) */
                        else if(preq->rq_prog == nfs_param.core_param.mnt_program)
                          {
                            /* Call is with MOUNTPROG */
                            if((preq->rq_vers != MOUNT_V1) && (preq->rq_vers != MOUNT_V3))
                              {
                                LogFullDebug(COMPONENT_DISPATCH,
                                             "/!\\ | Invalid Mount Version #%d",
                                             preq->rq_vers);
                                svcerr_progvers(xprt, MOUNT_V1, MOUNT_V3);      /* Bad MOUNT version */
                              }
                            else
                              {
                                /* Actual work starts here */
                                nfs_rpc_execute(pnfsreq, pmydata);
                              }
                          }
#ifdef _USE_NLM
                        else if(preq->rq_prog == nfs_param.core_param.nlm_program)
                          {
                            /* Call is with NLMPROG */
                            if(preq->rq_vers != NLM4_VERS)
                              {
                                LogFullDebug(COMPONENT_DISPATCH,
                                             "/!\\ | Invalid NLM Version #%d",
                                             preq->rq_vers);
                                svcerr_progvers(xprt, NLM4_VERS, NLM4_VERS);    /* Bad NLM version */
                              }
                            else
                              {
                                /* Actual work starts here */
                                nfs_rpc_execute(pnfsreq, pmydata);
                              }
                          }

#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
                        else if(preq->rq_prog == nfs_param.core_param.rquota_program)
                          {
                            /* Call is with NLMPROG */
                            if((preq->rq_vers != RQUOTAVERS) &&
                               (preq->rq_vers != EXT_RQUOTAVERS))
                              {
                                LogFullDebug(COMPONENT_DISPATCH,
                                             "/!\\ | Invalid RQUOTA Version #%d",
                                             preq->rq_vers);
                                svcerr_progvers(xprt, RQUOTAVERS, EXT_RQUOTAVERS);      /* Bad NLM version */
                              }
                            else
                              {
                                /* Actual work starts here */
                                nfs_rpc_execute(pnfsreq, pmydata);
                              }
                          }
#endif
                        else    /* No such program */
                          {
                            LogFullDebug(COMPONENT_DISPATCH,
                                         "/!\\ | Invalid Program number #%d",
                                         preq->rq_prog);
                            svcerr_noprog(xprt);        /* This is no NFS, MOUNT program, exit... */
                          }
                      }         /* if( no_dispatch == FALSE ) */
                  }             /* else from if( ( why = _authenticate( preq, pmsg) ) != AUTH_OK) */
              }                 /* if( pnfsreq->status ) */
          }                     /* while (  pnfsreq->status == XPRT_MOREREQS ); Now handle at the dispatcher's level */

        }

      /* Free the req by releasing the entry */
      LogFullDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCH: Invalidating processed entry with xprt_stat=%d",
                   pnfsreq->status);
      P(pmydata->request_pool_mutex);
      if(LRU_invalidate(pmydata->pending_request, pentry) != LRU_LIST_SUCCESS)
        {
          LogCrit(COMPONENT_DISPATCH,
              "NFS DISPATCH: Incoherency: released entry for dispatch could not be tagged invalid");
        }
      V(pmydata->request_pool_mutex);

      if(pmydata->passcounter > nfs_param.worker_param.nb_before_gc)
        {
          /* Garbage collection on dup req cache */
          LogDebug(COMPONENT_DISPATCH,
                   "NFS_WORKER #%d: before dupreq invalidation nb_entry=%d nb_invalid=%d",
                   index, pmydata->duplicate_request->nb_entry,
                   pmydata->duplicate_request->nb_invalid);
          if((rc =
              LRU_invalidate_by_function(pmydata->duplicate_request,
                                         nfs_dupreq_gc_function,
                                         NULL)) != LRU_LIST_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                   "NFS WORKER #%d: FAILURE: Impossible to invalidate entries for duplicate request cache (error %d)",
                   index, rc);
            }
          LogDebug(COMPONENT_DISPATCH,
                   "NFS_WORKER #%d: after dupreq invalidation nb_entry=%d nb_invalid=%d",
                   index, pmydata->duplicate_request->nb_entry,
                   pmydata->duplicate_request->nb_invalid);
          if((rc =
              LRU_gc_invalid(pmydata->duplicate_request,
                             (void *)&pmydata->dupreq_pool)) != LRU_LIST_SUCCESS)
            LogCrit(COMPONENT_DISPATCH,
                    "NFS WORKER #%d: FAILURE: Impossible to gc entries for duplicate request cache (error %d)",
                    index, rc);
          else
            LogFullDebug(COMPONENT_DISPATCH,
                         "NFS WORKER #%d: gc entries for duplicate request cache OK",
                         index);
          LogFullDebug(COMPONENT_DISPATCH,
                       "NFS_WORKER #%d: after dupreq gc nb_entry=%d nb_invalid=%d",
                       index, pmydata->duplicate_request->nb_entry,
                       pmydata->duplicate_request->nb_invalid);

          /* Performing garbabbge collection */
          LogFullDebug(COMPONENT_DISPATCH,
                       "NFS WORKER #%d: garbage collecting on pending request list",
                       index);
          pmydata->passcounter = 0;
          P(pmydata->request_pool_mutex);

          if(LRU_gc_invalid(pmydata->pending_request, (void *)&pmydata->request_pool) !=
             LRU_LIST_SUCCESS)
            LogCrit(COMPONENT_DISPATCH,
                    "NFS WORKER #%d: ERROR: Impossible garbage collection on pending request list",
                    index);
          else
            LogFullDebug(COMPONENT_DISPATCH,
                         "NFS WORKER #%d: garbage collection on pending request list OK",
                         index);

          V(pmydata->request_pool_mutex);

        }
      else
        LogFullDebug(COMPONENT_DISPATCH,
                     "NFS WORKER #%d: garbage collection isn't necessary count=%d, max=%d",
                     index, pmydata->passcounter, nfs_param.worker_param.nb_before_gc);
      pmydata->passcounter += 1;

      /* In case of the use of TCP, commit the dispatcher */
      if(pnfsreq->ipproto == IPPROTO_TCP)
        {
#if defined( _USE_TIRPC ) || defined( _FREEBSD )
          P(mutex_cond_xprt[xprt->xp_fd]);
          etat_xprt[xprt->xp_fd] = 1;
          pthread_cond_signal(&(condvar_xprt[xprt->xp_fd]));
          V(mutex_cond_xprt[xprt->xp_fd]);
#else
          //LogFullDebug(COMPONENT_DISPATCH, "worker : P pour sur %u\n", pnfsreq->xprt->xp_sock ) ; 
          P(mutex_cond_xprt[xprt->xp_sock]);
          etat_xprt[xprt->xp_sock] = 1;
          pthread_cond_signal(&(condvar_xprt[xprt->xp_sock]));
          V(mutex_cond_xprt[xprt->xp_sock]);
#endif
        }
      else if(pnfsreq->ipproto == IPPROTO_UDP)
        nfs_Cleanup_request_data(pnfsreq);

      /* If needed, perform garbage collection on cache_inode layer */
      P(lock_nb_current_gc_workers);
      if(nb_current_gc_workers < nfs_param.core_param.nb_max_concurrent_gc)
        {
          nb_current_gc_workers += 1;
          gc_allowed = TRUE;
        }
      else
        gc_allowed = FALSE;
      V(lock_nb_current_gc_workers);

      if(gc_allowed == TRUE)
        {
          pmydata->gc_in_progress = TRUE;
          LogDebug(COMPONENT_DISPATCH, "There are %d concurrent garbage collection",
                   nb_current_gc_workers);

          if(cache_inode_gc(pmydata->ht,
                            &(pmydata->cache_inode_client),
                            &cache_status) != CACHE_INODE_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "NFS WORKER: FAILURE: Bad cache_inode garbage collection");
            }

          P(lock_nb_current_gc_workers);
          nb_current_gc_workers -= 1;
          V(lock_nb_current_gc_workers);

          pmydata->gc_in_progress = FALSE;
        }
#ifdef _USE_MFSL
      /* As MFSL context are refresh, and because this could be a time consuming operation, the worker is 
       * set as "making garbagge collection" to avoid new requests to come in its pending queue */
      pmydata->gc_in_progress = TRUE;

      P(pmydata->cache_inode_client.mfsl_context.lock);
      fsal_status = MFSL_RefreshContext(&pmydata->cache_inode_client.mfsl_context,
                                        &pmydata->thread_fsal_context);
      V(pmydata->cache_inode_client.mfsl_context.lock);

      if(FSAL_IS_ERROR(fsal_status))
        {
          /* Failed init */
          LogCrit(COMPONENT_DISPATCH, "NFS  WORKER #%d: Error regreshing MFSL context", index);
          exit(1);
        }

      pmydata->gc_in_progress = FALSE;

#endif

    }                           /* while( 1 ) */
  return NULL;
}                               /* worker_thread */
