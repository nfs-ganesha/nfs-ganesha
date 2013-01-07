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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
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

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <sys/signal.h>
#include <poll.h>
#include "HashData.h"
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_req_queue.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "nfs_tcb.h"
#include "fridgethr.h"
#include "SemN.h"

extern nfs_worker_data_t *workers_data;
#ifdef SONAS
extern uint64_t rpc_out;
#endif

pool_t *request_pool;
pool_t *request_data_pool;
pool_t *dupreq_pool;
pool_t *ip_stats_pool;
pool_t *nfs_res_pool;

const nfs_function_desc_t invalid_funcdesc =
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
   "invalid_function", NOTHING_SPECIAL};

  const nfs_function_desc_t *INVALID_FUNCDESC = &invalid_funcdesc;

/* Static array : all the function pointer per nfs v2 functions */
const nfs_function_desc_t nfs2_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_ATTR2res, "nfs_Getattr",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR2args,
   (xdrproc_t) xdr_ATTR2res, "nfs_Setattr",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs2_Root, nfs2_Root_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs2_Root",
   NOTHING_SPECIAL},
  {nfs_Lookup, nfs2_Lookup_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_DIROP2res, "nfs_Lookup",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Readlink, nfs2_Readlink_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_READLINK2res, "nfs_Readlink",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Read, nfs2_Read_Free, (xdrproc_t) xdr_READ2args,
   (xdrproc_t) xdr_READ2res, "nfs_Read",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS | MAKES_IO},
  {nfs2_Writecache, nfs2_Writecache_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs_Writecache",
   NOTHING_SPECIAL},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE2args,
   (xdrproc_t) xdr_ATTR2res, "nfs_Write",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS | MAKES_IO},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE2args,
   (xdrproc_t) xdr_DIROP2res, "nfs_Create",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_nfsstat2, "nfs_Remove",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Rename",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Link",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Symlink",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_CREATE2args,
   (xdrproc_t) xdr_DIROP2res, "nfs_Mkdir",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_nfsstat2, "nfs_Rmdir",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs2_Readdir_Free, (xdrproc_t) xdr_READDIR2args,
   (xdrproc_t) xdr_READDIR2res, "nfs_Readdir",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_STATFS2res, "nfs_Fsstat",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS}
};

const nfs_function_desc_t nfs3_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void,
   "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_GETATTR3args,
   (xdrproc_t) xdr_GETATTR3res, "nfs_Getattr",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR3args,
   (xdrproc_t) xdr_SETATTR3res, "nfs_Setattr",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Lookup, nfs3_Lookup_Free, (xdrproc_t) xdr_LOOKUP3args,
   (xdrproc_t) xdr_LOOKUP3res, "nfs_Lookup",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs3_Access, nfs3_Access_Free, (xdrproc_t) xdr_ACCESS3args,
   (xdrproc_t) xdr_ACCESS3res, "nfs3_Access",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Readlink, nfs3_Readlink_Free, (xdrproc_t) xdr_READLINK3args,
   (xdrproc_t) xdr_READLINK3res, "nfs_Readlink",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Read, nfs3_Read_Free, (xdrproc_t) xdr_READ3args,
   (xdrproc_t) xdr_READ3res, "nfs_Read",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS | MAKES_IO},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE3args,
   (xdrproc_t) xdr_WRITE3res, "nfs_Write",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS | MAKES_IO},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE3args,
   (xdrproc_t) xdr_CREATE3res, "nfs_Create",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_MKDIR3args,
   (xdrproc_t) xdr_MKDIR3res, "nfs_Mkdir",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK3args,
   (xdrproc_t) xdr_SYMLINK3res, "nfs_Symlink",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs3_Mknod, nfs3_Mknod_Free, (xdrproc_t) xdr_MKNOD3args,
   (xdrproc_t) xdr_MKNOD3res, "nfs3_Mknod",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_REMOVE3args,
   (xdrproc_t) xdr_REMOVE3res, "nfs_Remove",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_RMDIR3args,
   (xdrproc_t) xdr_RMDIR3res, "nfs_Rmdir",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME3args,
   (xdrproc_t) xdr_RENAME3res, "nfs_Rename",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK3args,
   (xdrproc_t) xdr_LINK3res, "nfs_Link",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs3_Readdir_Free, (xdrproc_t) xdr_READDIR3args,
   (xdrproc_t) xdr_READDIR3res, "nfs_Readdir",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs3_Readdirplus, nfs3_Readdirplus_Free, (xdrproc_t) xdr_READDIRPLUS3args,
   (xdrproc_t) xdr_READDIRPLUS3res, "nfs3_Readdirplus",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_FSSTAT3args,
   (xdrproc_t) xdr_FSSTAT3res, "nfs_Fsstat",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs3_Fsinfo, nfs3_Fsinfo_Free, (xdrproc_t) xdr_FSINFO3args,
   (xdrproc_t) xdr_FSINFO3res, "nfs3_Fsinfo",
   NEEDS_CRED | NEEDS_EXPORT},
  {nfs3_Pathconf, nfs3_Pathconf_Free, (xdrproc_t) xdr_PATHCONF3args,
   (xdrproc_t) xdr_PATHCONF3res, "nfs3_Pathconf",
   NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS},
  {nfs3_Commit, nfs3_Commit_Free, (xdrproc_t) xdr_COMMIT3args,
   (xdrproc_t) xdr_COMMIT3res, "nfs3_Commit",
   MAKES_WRITE | NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS}
};

/* Remeber that NFSv4 manages authentication though junction crossing, and
 * so does it for RO FS management (for each operation) */
const nfs_function_desc_t nfs4_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs4_Compound, nfs4_Compound_Free, (xdrproc_t) xdr_COMPOUND4args,
   (xdrproc_t) xdr_COMPOUND4res, "nfs4_Compound",
   NOTHING_SPECIAL}
};

const nfs_function_desc_t mnt1_func_desc[] = {
  {mnt_Null, mnt_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "mnt_Null",
   NOTHING_SPECIAL},
  /* Mnt defers any credential handling and export processing for actual
   * operation processing, the export is not known until the dirpath is parsed.
   */
  {mnt_Mnt, mnt1_Mnt_Free, (xdrproc_t) xdr_dirpath,
   (xdrproc_t) xdr_fhstatus2, "mnt_Mnt",
   NOTHING_SPECIAL},
  {mnt_Dump, mnt_Dump_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_mountlist, "mnt_Dump",
   NOTHING_SPECIAL},
  {mnt_Umnt, mnt_Umnt_Free, (xdrproc_t) xdr_dirpath,
   (xdrproc_t) xdr_void, "mnt_Umnt",
   NOTHING_SPECIAL},
  {mnt_UmntAll, mnt_UmntAll_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "mnt_UmntAll",
   NOTHING_SPECIAL},
  {mnt_Export, mnt_Export_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_exports, "mnt_Export",
   NOTHING_SPECIAL}
};

const nfs_function_desc_t mnt3_func_desc[] = {
  {mnt_Null, mnt_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "mnt_Null",
   NOTHING_SPECIAL},
  {mnt_Mnt, mnt3_Mnt_Free, (xdrproc_t) xdr_dirpath,
   (xdrproc_t) xdr_mountres3, "mnt_Mnt",
   NOTHING_SPECIAL},
  {mnt_Dump, mnt_Dump_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_mountlist, "mnt_Dump",
   NOTHING_SPECIAL},
  {mnt_Umnt, mnt_Umnt_Free, (xdrproc_t) xdr_dirpath,
   (xdrproc_t) xdr_void, "mnt_Umnt",
   NOTHING_SPECIAL},
  {mnt_UmntAll, mnt_UmntAll_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "mnt_UmntAll",
   NOTHING_SPECIAL},
  {mnt_Export, mnt_Export_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_exports, "mnt_Export",
   NOTHING_SPECIAL}
};

#define nlm4_Unsupported nlm_Null
#define nlm4_Unsupported_Free nlm_Null_Free

#ifdef _USE_NLM
const nfs_function_desc_t nlm4_func_desc[] = {
  [NLMPROC4_NULL] = {
      nlm_Null, nlm_Null_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm_Null",
      NOTHING_SPECIAL},
  [NLMPROC4_TEST] = {
      nlm4_Test, nlm4_Test_Free, (xdrproc_t) xdr_nlm4_testargs,
      (xdrproc_t) xdr_nlm4_testres, "nlm4_Test",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_LOCK] = {
      nlm4_Lock, nlm4_Lock_Free, (xdrproc_t) xdr_nlm4_lockargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Lock",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_CANCEL] = {
      nlm4_Cancel, nlm4_Cancel_Free, (xdrproc_t) xdr_nlm4_cancargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Cancel",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_UNLOCK] = {
      nlm4_Unlock, nlm4_Unlock_Free, (xdrproc_t) xdr_nlm4_unlockargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Unlock",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_GRANTED] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Granted",
      NOTHING_SPECIAL},
  [NLMPROC4_TEST_MSG] = {
      nlm4_Test_Message, nlm4_Test_Free,
      (xdrproc_t) xdr_nlm4_testargs,
      (xdrproc_t) xdr_void, "nlm4_Test_msg",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_LOCK_MSG] = {
      nlm4_Lock_Message, nlm4_Lock_Free,
      (xdrproc_t) xdr_nlm4_lockargs,
      (xdrproc_t) xdr_void, "nlm4_Lock_msg",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_CANCEL_MSG] = {
      nlm4_Cancel_Message, nlm4_Cancel_Free,
      (xdrproc_t) xdr_nlm4_cancargs,
      (xdrproc_t) xdr_void, "nlm4_Cancel_msg",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_UNLOCK_MSG] = {
      nlm4_Unlock_Message, nlm4_Unlock_Free,
      (xdrproc_t) xdr_nlm4_unlockargs,
      (xdrproc_t) xdr_void, "nlm4_Unlock_msg",
      NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_GRANTED_MSG] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Granted_msg",
      NOTHING_SPECIAL},
  [NLMPROC4_TEST_RES] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Test_res",
      NOTHING_SPECIAL},
  [NLMPROC4_LOCK_RES] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Lock_res",
      NOTHING_SPECIAL},
  [NLMPROC4_CANCEL_RES] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Cancel_res",
      NOTHING_SPECIAL},
  [NLMPROC4_UNLOCK_RES] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Unlock_res",
      NOTHING_SPECIAL},
  [NLMPROC4_GRANTED_RES] = {
      nlm4_Granted_Res, nlm4_Granted_Res_Free, (xdrproc_t) xdr_nlm4_res,
      (xdrproc_t) xdr_void, "nlm4_Granted_res",
      NOTHING_SPECIAL},
  [NLMPROC4_SM_NOTIFY] = {
      nlm4_Sm_Notify, nlm4_Sm_Notify_Free,
      (xdrproc_t) xdr_nlm4_sm_notifyargs, (xdrproc_t) xdr_void,
      "nlm4_sm_notify",
      NOTHING_SPECIAL},
  [17] = {
      nlm4_Unsupported, nlm4_Unsupported_Free,
      (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
      "nlm4_Granted_res",
      NOTHING_SPECIAL},
  [18] = {
      nlm4_Unsupported, nlm4_Unsupported_Free,
      (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
      "nlm4_Granted_res",
      NOTHING_SPECIAL},
  [19] = {
      nlm4_Unsupported, nlm4_Unsupported_Free,
      (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
      "nlm4_Granted_res",
      NOTHING_SPECIAL},
  [NLMPROC4_SHARE] {nlm4_Share, nlm4_Share_Free,
                    (xdrproc_t) xdr_nlm4_shareargs, (xdrproc_t) xdr_nlm4_shareres,
                    "nlm4_Share",
                    NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_UNSHARE] = {nlm4_Unshare, nlm4_Unshare_Free,
                        (xdrproc_t) xdr_nlm4_shareargs, (xdrproc_t) xdr_nlm4_shareres,
                        "nlm4_Unshare",
                        NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_NM_LOCK] = {
                        /* NLM_NM_LOCK uses the same handling as NLM_LOCK except for
                         * monitoring, nlm4_Lock will make that determination.
                         */
                        nlm4_Lock, nlm4_Lock_Free,
                        (xdrproc_t) xdr_nlm4_lockargs, (xdrproc_t) xdr_nlm4_res,
                        "nlm4_Nm_lock",
                        NEEDS_CRED | NEEDS_EXPORT},
  [NLMPROC4_FREE_ALL] = {nlm4_Free_All, nlm4_Free_All_Free,
                         (xdrproc_t) xdr_nlm4_free_allargs, (xdrproc_t) xdr_void,
                         "nlm4_Free_all",
                         NOTHING_SPECIAL},
};
#endif                          /* _USE_NLM */

#ifdef _USE_RQUOTA
const nfs_function_desc_t rquota1_func_desc[] = {
  [0] = {
         rquota_Null, rquota_Null_Free, (xdrproc_t) xdr_void,
         (xdrproc_t) xdr_void, "rquota_Null",
         NOTHING_SPECIAL},
  [RQUOTAPROC_GETQUOTA] = {
      rquota_getquota, rquota_getquota_Free,
      (xdrproc_t) xdr_getquota_args,
      (xdrproc_t) xdr_getquota_rslt, "rquota_Getquota",
      NEEDS_CRED},
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
        rquota_Null, rquota_Null_Free, (xdrproc_t) xdr_void,
        (xdrproc_t) xdr_void, "rquota_Null",
        NOTHING_SPECIAL},
    [RQUOTAPROC_GETQUOTA] = {
        rquota_getquota, rquota_getquota_Free,
        (xdrproc_t) xdr_ext_getquota_args,
        (xdrproc_t) xdr_getquota_rslt, "rquota_Ext_Getquota",
        NEEDS_CRED},
  [RQUOTAPROC_GETACTIVEQUOTA] = {
      rquota_getactivequota, rquota_getactivequota_Free,
      (xdrproc_t) xdr_ext_getquota_args,
      (xdrproc_t) xdr_getquota_rslt,
      "rquota_Ext_Getactivequota",
      NEEDS_CRED},
    [RQUOTAPROC_SETQUOTA] = {
        rquota_setquota, rquota_setquota_Free,
        (xdrproc_t) xdr_ext_setquota_args,
        (xdrproc_t) xdr_setquota_rslt, "rquota_Ext_Setactivequota",
        NEEDS_CRED},
    [RQUOTAPROC_SETACTIVEQUOTA] = {
        rquota_setactivequota, rquota_setactivequota_Free,
        (xdrproc_t) xdr_ext_setquota_args,
        (xdrproc_t) xdr_setquota_rslt,
        "rquota_Ext_Getactivequota",
        NEEDS_CRED}
};

#endif                          /* _USE_RQUOTA */

extern const char *pause_state_str[];
int is_rpc_call_valid(fridge_thr_contex_t *, SVCXPRT *,
		      struct svc_req *);

/*
 * Extract nfs function descriptor from nfs request.
 *
 * XXX This function calls is_rpc_call_valid, which one might not expect to
 * be sending RPC replies.  Fix this, and remove thr_ctx argument.
 */
const nfs_function_desc_t *
nfs_rpc_get_funcdesc(fridge_thr_contex_t *thr_ctx, nfs_request_data_t *preqnfs)
{
  struct svc_req *req = &preqnfs->req;
  bool slocked = FALSE;

  /* Validate rpc call, but don't report any errors here */
  if(is_rpc_call_valid(thr_ctx, preqnfs->xprt, req) == false)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "INVALID_FUNCDESC for Program %d, Version %d, "
                   "Function %d after is_rpc_call_valid",
                   (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc);
      return INVALID_FUNCDESC;
    }

  if(req->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      if(req->rq_vers == NFS_V2)
        return INVALID_FUNCDESC;
      else if(req->rq_vers == NFS_V3)
        return &nfs3_func_desc[req->rq_proc];
      else
        return &nfs4_func_desc[req->rq_proc];
    }

  if(req->rq_prog == nfs_param.core_param.program[P_MNT])
    {
      preqnfs->lookahead.flags |= NFS_LOOKAHEAD_MOUNT;
      if(req->rq_vers == MOUNT_V1)
        return &mnt1_func_desc[req->rq_proc];
      else
        return &mnt3_func_desc[req->rq_proc];
    }

#ifdef _USE_NLM
  if(req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      return &nlm4_func_desc[req->rq_proc];
    }
#endif                          /* _USE_NLM */

#ifdef _USE_RQUOTA
  if(req->rq_prog == nfs_param.core_param.program[P_RQUOTA])
    {
      if(req->rq_vers == RQUOTAVERS)
        return &rquota1_func_desc[req->rq_proc];
      else
        return &rquota2_func_desc[req->rq_proc];
    }
#endif                          /* _USE_RQUOTA */

  /* Oops, should never get here! */
  DISP_SLOCK(preqnfs->xprt);
  svcerr_noprog(preqnfs->xprt, req);
  DISP_SUNLOCK(preqnfs->xprt);

  LogFullDebug(COMPONENT_DISPATCH,
               "INVALID_FUNCDESC for Program %d, Version %d, Function %d",
               (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc);
  return INVALID_FUNCDESC;
}

/**
 * nfs_rpc_execute: main rpc dispatcher routine
 *
 * This is the regular RPC dispatcher that every RPC server should include.
 *
 * @param[in,out] preq NFS request
 *
 */
static void
nfs_rpc_execute(request_data_t    * preq,
		nfs_worker_data_t * pworker_data)
{
  exportlist_t               * pexport = NULL;
  nfs_request_data_t         * preqnfs = preq->r_u.nfs;
  nfs_arg_t                  * parg_nfs = &preqnfs->arg_nfs;
  nfs_res_t                  * res_nfs = NULL;
  short                        exportid;
  struct svc_req             * req = &preqnfs->req;
  SVCXPRT                    * xprt = preqnfs->xprt;
  nfs_stat_type_t              stat_type;
  int                          port;
  int                          rc;
  export_perms_t             * pexport_perms = &pworker_data->export_perms;
  int                          protocol_options = 0;
  fsal_op_context_t          * pfsal_op_ctx = NULL;
  struct nfs_req_timer         req_timer;
  unsigned int                 fsal_count   = 0;
  dupreq_status_t              dpq_status;
  enum auth_stat               auth_rc;
  const char                 * progname = "unknown";
  xprt_type_t                  xprt_type = svc_get_xprt_type(xprt);
  bool                         slocked = FALSE;

  /* Initialize permissions to allow nothing */
  pexport_perms->options       = 0;
  pexport_perms->anonymous_uid = (uid_t) ANON_UID;
  pexport_perms->anonymous_gid = (gid_t) ANON_GID;

  /* Initialized user_credentials */
  init_credentials(&pworker_data->user_credentials);

  /* Req timer */
  init_nfs_req_timer(&req_timer);

  /* Get the function descriptor.  Bail if it cant be executed. */
  pworker_data->funcdesc = preqnfs->funcdesc;
  if(pworker_data->funcdesc == INVALID_FUNCDESC)
    return;

  /* Must hold slock when calling TI-RPC send channel function,
   * including svc_sendreply and the svcerr_* calls */

  if(copy_xprt_addr(&pworker_data->hostaddr, xprt) == 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "copy_xprt_addr failed for Program %d, Version %d, "
                   "Function %d",
                   (int)req->rq_prog, (int)req->rq_vers,
                   (int)req->rq_proc);
      DISP_SLOCK(xprt);
      svcerr_systemerr(xprt, req);
      DISP_SUNLOCK(xprt);
      return;
    }

  port = get_port(&pworker_data->hostaddr);

  sprint_sockaddr(&pworker_data->hostaddr,
                  pworker_data->hostaddr_str,
                  sizeof(pworker_data->hostaddr_str));

  LogDebug(COMPONENT_DISPATCH,
           "Request from %s for Program %d, Version %d, Function %d has xid=%u",
           pworker_data->hostaddr_str,
           (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc,
           req->rq_xid);

  /* If req is uncacheable, or if req is v41+, nfs_dupreq_start will do
   * nothing but allocate a result object and mark the request (ie, the
   * path is short, lockless, and does no hash/search). */
  dpq_status = nfs_dupreq_start(preqnfs, req);
  res_nfs = preqnfs->res_nfs;

  switch(dpq_status)
    {
    case DUPREQ_SUCCESS:
	  /* a new request, continue processing it */
          LogFullDebug(COMPONENT_DISPATCH, "Current request is not duplicate or "
		       "not cacheable");
          break;
          /* Found the request in the dupreq cache. It's an old request so resend
           * old reply. */
        case DUPREQ_EXISTS:
          /* Request was known, use the previous reply */
          LogFullDebug(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: DupReq Cache Hit: using previous "
                       "reply, rpcxid=%u",
                       req->rq_xid);

          DISP_SLOCK(xprt);
          if(svc_sendreply
             (xprt, req, pworker_data->funcdesc->xdr_encode_func,
              (caddr_t) res_nfs) == FALSE)
            {
              LogWarn(COMPONENT_DISPATCH,
                      "NFS DISPATCHER: FAILURE: Error while calling "
                      "svc_sendreply on a duplicate request. rpcxid=%u "
                      "socket=%d function:%s client:%s program:%d "
                      "nfs version:%d proc:%d xid:%u",
                      req->rq_xid, xprt->xp_fd,
                      pworker_data->funcdesc->funcname,
                      pworker_data->hostaddr_str, (int)req->rq_prog,
                      (int)req->rq_vers, (int)req->rq_proc, req->rq_xid);
              svcerr_systemerr(xprt, req);
            }
          DISP_SUNLOCK(xprt);

          LogFullDebug(COMPONENT_DISPATCH,
                       "After svc_sendreply on socket %d (dup req)",
                       xprt->xp_fd);
#ifdef SONAS
          rpc_out++;
#endif
	  goto dupreq_finish;
          break;

          /* Another thread owns the request */
        case DUPREQ_BEING_PROCESSED:
	  LogFullDebug(COMPONENT_DISPATCH,
		       "DUP: Request xid=%u is already being processed; the "
		       "active thread will reply",
		       req->rq_xid);
          /* Ignore the request, send no error */
	  goto freeargs;
          break;

          /* something is very wrong with the duplicate request cache */
        case DUPREQ_ERROR:
          LogCrit(COMPONENT_DISPATCH,
		  "DUP: Did not find the request in the duplicate request cache "
		  "and couldn't add the request.");
          DISP_SLOCK(xprt);
          svcerr_systemerr(xprt, req);
          DISP_SUNLOCK(xprt);
	  goto freeargs;
          break;

          /* oom */
        case DUPREQ_INSERT_MALLOC_ERROR:
          LogCrit(COMPONENT_DISPATCH,
                  "DUP: Cannot process request, not enough memory available!");
          DISP_SLOCK(xprt);
          svcerr_systemerr(xprt, req);
          DISP_SUNLOCK(xprt);
	  goto freeargs;
          break;

        default:
          LogCrit(COMPONENT_DISPATCH,
                  "Unknown duplicate request cache status. This should never be "
                  "reached!");
          DISP_SLOCK(xprt);
          svcerr_systemerr(xprt, req);
          DISP_SUNLOCK(xprt);
	  goto freeargs;
          return;
        }

  /* Get the export entry */
  if(req->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      /* The NFSv2 and NFSv3 functions'arguments always begin with the file
       * handle (but not the NULL function).  This hook is used to get the
       * fhandle with the arguments and so determine the export entry to be
       * used.  In NFSv4, junction traversal is managed by the protocol itself
       * so the whole export list is provided to NFSv4 request. */

      progname = "NFS";

      switch (req->rq_vers)
        {
        case NFS_V2:
          protocol_options |= EXPORT_OPTION_NFSV2;

          if(req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs2_FhandleToExportId((fhandle2 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL)
                {
                  /* Reject the request for authentication reason (incompatible
                   * file handle) */
                  if(isInfo(COMPONENT_DISPATCH))
                    {
		      char dumpfh[1024];
		      char *reason = NULL;

                      if(exportid < 0)
                        reason = "has badly formed handle";
                      else
                        reason = "has invalid export";

                      sprint_fhandle2(dumpfh, (fhandle2 *) parg_nfs);

                      LogInfo(COMPONENT_DISPATCH,
                              "NFS2 Request from client %s %s, proc=%d, FH=%s",
                              pworker_data->hostaddr_str, reason,
                              (int)req->rq_proc, dumpfh);
                    }

                  /* Bad argument */
                  auth_rc = AUTH_FAILED;
                  goto auth_failure;
                }

              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for Export_Id %d %s for client %s",
                           pexport->id, pexport->fullpath,
                           pworker_data->hostaddr_str);
            }
          else
            pexport = NULL;

          break;

        case NFS_V3:
          protocol_options |= EXPORT_OPTION_NFSV3;

          if(req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs3_FhandleToExportId((nfs_fh3 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL)
                {
                  if(isInfo(COMPONENT_DISPATCH))
                    {
		      char dumpfh[1024];
		      char *reason = NULL;

                      if(exportid < 0)
                        reason = "has badly formed handle";
                      else
                        reason = "has invalid export";

                      sprint_fhandle3(dumpfh, (nfs_fh3 *) parg_nfs);

                      LogInfo(COMPONENT_DISPATCH,
                              "NFS3 Request from client %s %s, proc=%d, FH=%s",
                              pworker_data->hostaddr_str, reason,
                              (int)req->rq_proc, dumpfh);
                    }

                  /* Bad argument */
                  auth_rc = AUTH_FAILED;
                  goto auth_failure;
                }

              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for Export_Id %d %s for client %s",
                           pexport->id, pexport->fullpath,
                           pworker_data->hostaddr_str);
            }
          else
            pexport = NULL;

          break;

        case NFS_V4:
          protocol_options |= EXPORT_OPTION_NFSV4;
          /* NFSv4 requires entire export list */
          pexport = NULL;
          break;

        default:
          /* Invalid version (which should never get here) */
          LogCrit(COMPONENT_DISPATCH,
                  "Invalid NFS version %d from client %s",
                  (int)req->rq_vers, pworker_data->hostaddr_str);

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }                       /* switch( ptr_req->rq_vers ) */
    }
#ifdef _USE_NLM
  else if(req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      netobj *pfh3 = NULL;

      protocol_options |= EXPORT_OPTION_NFSV3;

      progname = "NLM";

      switch(req->rq_proc)
        {
          case NLMPROC4_NULL:
          case NLMPROC4_TEST_RES:
          case NLMPROC4_LOCK_RES:
          case NLMPROC4_CANCEL_RES:
          case NLMPROC4_UNLOCK_RES:
          case NLMPROC4_GRANTED_RES:
          case NLMPROC4_SM_NOTIFY:
          case NLMPROC4_FREE_ALL:
            break;

          case NLMPROC4_TEST:
          case NLMPROC4_TEST_MSG:
          case NLMPROC4_GRANTED:
          case NLMPROC4_GRANTED_MSG:
            pfh3 = &parg_nfs->arg_nlm4_test.alock.fh;
            break;

          case NLMPROC4_LOCK:
          case NLMPROC4_LOCK_MSG:
          case NLMPROC4_NM_LOCK:
            pfh3 = &parg_nfs->arg_nlm4_lock.alock.fh;
            break;

          case NLMPROC4_CANCEL:
          case NLMPROC4_CANCEL_MSG:
            pfh3 = &parg_nfs->arg_nlm4_cancel.alock.fh;
            break;

          case NLMPROC4_UNLOCK:
          case NLMPROC4_UNLOCK_MSG:
            pfh3 = &parg_nfs->arg_nlm4_unlock.alock.fh;
            break;

          case NLMPROC4_SHARE:
          case NLMPROC4_UNSHARE:
            pfh3 = &parg_nfs->arg_nlm4_share.share.fh;
            break;
        }

      if(pfh3 != NULL)
        {
          exportid = nlm4_FhandleToExportId(pfh3);

          if(exportid < 0 ||
             (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                             exportid)) == NULL)
            {
              /* Reject the request for authentication reason (incompatible 
               * file handle) */
              if(isInfo(COMPONENT_DISPATCH))
                {
                  char dumpfh[1024];
                  char *reason;

                  if(exportid < 0)
                    reason = "has badly formed handle";
                  else
                    reason = "has invalid export";

                  sprint_fhandle_nlm(dumpfh, pfh3);

                  LogCrit(COMPONENT_DISPATCH,
                          "NLM4 Request from client %s %s, proc=%d, FH=%s",
                          pworker_data->hostaddr_str, reason,
                          (int)req->rq_proc, dumpfh);
                }

              /* Bad argument */
              auth_rc = AUTH_FAILED;
              goto auth_failure;
            }

          LogFullDebug(COMPONENT_DISPATCH,
                       "Found export entry for Export_Id %d %s for client %s",
                       pexport->id, pexport->fullpath,
                       pworker_data->hostaddr_str);
        }
      else
        pexport = NULL;
    }
#endif                          /* _USE_NLM */
  else
    {
      /* All other protocols do not have a specific export */
      pexport = NULL;
    }

  /* Only do access check if we have an export. */
  if((pworker_data->funcdesc->dispatch_behaviour & NEEDS_EXPORT) != 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "nfs_rpc_execute about to call nfs_export_check_access for client %s",
                   pworker_data->hostaddr_str);

      nfs_export_check_access(&pworker_data->hostaddr,
                              pexport,
                              pexport_perms);

      if(pexport_perms->options == 0)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Client %s is not allowed to access Export_Id %d %s, vers=%d, proc=%d",
                  pworker_data->hostaddr_str,
                  pexport->id, pexport->fullpath,
                  (int)req->rq_vers, (int)req->rq_proc);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }

      /* Check protocol version */
      if((protocol_options & EXPORT_OPTION_PROTOCOLS)== 0)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Problem, request requires export but does not have a protocol version");

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      if((protocol_options & pexport_perms->options) == 0)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers,
                  pexport->id, pexport->fullpath,
                  pworker_data->hostaddr_str);

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      /* Check transport type */
      if(((xprt_type == XPRT_UDP) &&
          ((pexport_perms->options & EXPORT_OPTION_UDP) == 0)) ||
         ((xprt_type == XPRT_TCP) &&
          ((pexport_perms->options & EXPORT_OPTION_TCP) == 0)))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d over %s not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers, xprt_type_to_str(xprt_type),
                  pexport->id, pexport->fullpath,
                  pworker_data->hostaddr_str);

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      /* Test if export allows the authentication provided */
      if(((pworker_data->funcdesc->dispatch_behaviour & SUPPORTS_GSS) != 0) &&
         (nfs_export_check_security(req, pexport_perms, pexport) == FALSE))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d auth not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers,
                  pexport->id, pexport->fullpath,
                  pworker_data->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }

      /* Check if client is using a privileged port, but only for NFS protocol */
      if((req->rq_prog == nfs_param.core_param.program[P_NFS]) &&
         ((pexport_perms->options & EXPORT_OPTION_PRIVILEGED_PORT) != 0) &&
         (port >= IPPORT_RESERVED))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Non-reserved Port %d is not allowed on Export_Id %d %s for client %s",
                  port, pexport->id, pexport->fullpath,
                  pworker_data->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }
    }

  /* Get user credentials */
  if (pworker_data->funcdesc->dispatch_behaviour & NEEDS_CRED)
    {
      if (get_req_uid_gid(req, &pworker_data->user_credentials) == FALSE)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "could not get uid and gid, rejecting client %s",
                  pworker_data->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }
    }

  if(pworker_data->hostaddr.ss_family == AF_INET)
    /* Increment the stats per client address (for IPv4 Only) */
    if(nfs_ip_stats_incr(pworker_data->ht_ip_stats,
                         &pworker_data->hostaddr,
                         nfs_param.core_param.program[P_NFS],
                         nfs_param.core_param.program[P_MNT],
                         req) == IP_STATS_NOT_FOUND)
      {
        if(nfs_ip_stats_add(pworker_data->ht_ip_stats,
                            &pworker_data->hostaddr,
                            ip_stats_pool) == IP_STATS_SUCCESS)
          {
            nfs_ip_stats_incr(pworker_data->ht_ip_stats,
                              &pworker_data->hostaddr,
                              nfs_param.core_param.program[P_NFS],
                              nfs_param.core_param.program[P_MNT],
                              req);
          }
      }

  /* Start operation timer, atomically store in worker thread for long running
   * thread detection.
   */
  nfs_req_timer_start(&req_timer);
#ifdef _USE_STAT_EXPORTER
    atomic_store_msectimer_t(&pworker_data->timer_start, req_timer.timer_start);
#endif

  LogDebug(COMPONENT_DISPATCH,
           "NFS DISPATCHER: Calling service function %s start_time %lu.%03lu",
           pworker_data->funcdesc->funcname,
           req_timer.timer_start / MSEC_PER_SEC,
           req_timer.timer_start % MSEC_PER_SEC);

  /*
   * It is now time for checking if export list allows the machine to perform
   * the request
   */
  if((pworker_data->funcdesc->dispatch_behaviour & MAKES_IO) != 0 &&
     (pexport_perms->options & EXPORT_OPTION_RW_ACCESS) == 0)
    {
      /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level
       * This is done by replying EDQUOT (this error is known for not disturbing
       * the client's requests cache
       */
      if(req->rq_prog == nfs_param.core_param.program[P_NFS])
        switch(req->rq_vers)
          {
          case NFS_V2:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFSERR_DQUOT because request is on an MD Only export");
            res_nfs->res_attr2.status = NFSERR_DQUOT;
            rc = NFS_REQ_OK;
            break;

          case NFS_V3:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFS3ERR_DQUOT because request is on an MD Only export");
            res_nfs->res_attr2.status = NFS3ERR_DQUOT;
            rc = NFS_REQ_OK;
            break;

          default:
            LogDebug(COMPONENT_DISPATCH,
                     "Dropping IO request on an MD Only export");
            rc = NFS_REQ_DROP;
            break;
          }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Dropping IO request on an MD Only export");
          rc = NFS_REQ_DROP;
        }
    }
  else if((pworker_data->funcdesc->dispatch_behaviour & MAKES_WRITE) != 0 &&
          (pexport_perms->options & (EXPORT_OPTION_WRITE_ACCESS |
                                     EXPORT_OPTION_MD_WRITE_ACCESS)) == 0)
    {
      if(req->rq_prog == nfs_param.core_param.program[P_NFS])
        switch(req->rq_vers)
          {
          case NFS_V2:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFSERR_ROFS because request is on a Read Only export");
            res_nfs->res_attr2.status = NFSERR_ROFS;
            rc = NFS_REQ_OK;
            break;

          case NFS_V3:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFS3ERR_ROFS because request is on a Read Only export");
            res_nfs->res_attr2.status = NFS3ERR_ROFS;
            rc = NFS_REQ_OK;
            break;

          default:
            LogDebug(COMPONENT_DISPATCH,
                     "Dropping request on a Read Only export");
            rc = NFS_REQ_DROP;
            break;
          }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Dropping request on a Read Only export");
          rc = NFS_REQ_DROP;
        }
    }
  else if((pworker_data->funcdesc->dispatch_behaviour & NEEDS_EXPORT) != 0 &&
          (pexport_perms->options & (EXPORT_OPTION_READ_ACCESS |
                                     EXPORT_OPTION_MD_READ_ACCESS)) == 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "Client %s is not allowed to access Export_Id %d %s, vers=%d, proc=%d",
              pworker_data->hostaddr_str,
              pexport->id, pexport->fullpath,
              (int)req->rq_vers, (int)req->rq_proc);

#ifdef _USE_STAT_EXPORTER
      /* this thread is done, reset the timer start to avoid long processing */
      atomic_store_msectimer_t(&pworker_data->timer_start, 0);
#endif

      auth_rc = AUTH_TOOWEAK;
      goto auth_failure;
    }
  else
    {
      /* Do the authentication stuff, if needed */
      if((pworker_data->funcdesc->dispatch_behaviour &
          (NEEDS_CRED | NEEDS_EXPORT)) == (NEEDS_CRED | NEEDS_EXPORT))
        {
          /* Swap the anonymous uid/gid if the user should be anonymous */
          nfs_check_anon(pexport_perms, pexport, &pworker_data->user_credentials);

          if(nfs_build_fsal_context(req,
                                    pexport,
                                    &pworker_data->thread_fsal_context,
                                    &pworker_data->user_credentials) == FALSE)
            {
              LogInfo(COMPONENT_DISPATCH,
                      "authentication failed, rejecting client %s",
                      pworker_data->hostaddr_str);

#ifdef _USE_STAT_EXPORTER
              /* this thread is done, reset the timer start to avoid long processing */
              atomic_store_msectimer_t(&pworker_data->timer_start, 0);
#endif

              auth_rc = AUTH_TOOWEAK;
              goto auth_failure;
            }
        }

      /* processing */

#ifdef _ERROR_INJECTION
      if(worker_delay_time != 0)
        sleep(worker_delay_time);
      else if(next_worker_delay_time != 0)
        {
          sleep(next_worker_delay_time);
          next_worker_delay_time = 0;
        }
#endif

      pfsal_op_ctx =  &pworker_data->thread_fsal_context;

      rc = pworker_data->funcdesc->service_function(parg_nfs,
						    pexport,
						    pfsal_op_ctx,
						    pworker_data,
						    req,
						    res_nfs);
      req_timer.fsal_latency = pfsal_op_ctx->latency;
      fsal_count = pfsal_op_ctx->count;
    }

#ifdef _USE_STAT_EXPORTER
  /* this thread is done, reset the timer start to avoid long processing */
  atomic_store_msectimer_t(&pworker_data->timer_start, 0);
#endif

  /* Perform statistics here */
  nfs_req_timer_stop(&req_timer, &preqnfs->time_queued);

  /* process time */
  stat_type = (rc == NFS_REQ_OK) ? GANESHA_STAT_SUCCESS : GANESHA_STAT_DROP;
#ifdef SONAS
  rpc_out++;
#endif

  /* Update the stats for the worker */
  nfs_stat_update(stat_type,
                  &(pworker_data->stats.stat_req),
                  req,
		  &req_timer,
                  fsal_count);

  /* Update total counters */
  (pworker_data->stats.nb_total_req)++;

#ifdef _USE_STAT_EXPORTER
  /* Update the stats for the export */
  if (pexport != NULL)
    {
      nfs_stat_update(stat_type,
		      &(pexport->worker_stats[pworker_data->worker_index].stat_req),
		      req,
		      &req_timer,
		      fsal_count);

      /* Update per-share total counters */
      pexport->worker_stats[pworker_data->worker_index].nb_total_req += 1;
    }
#endif

  if(req_timer.timer_diff >= nfs_param.core_param.long_processing_threshold_msec)
    LogEvent(COMPONENT_DISPATCH,
             "Function %s xid=%u exited with status %d taking %lu.%03lu seconds to process",
             pworker_data->funcdesc->funcname, req->rq_xid, rc,
             req_timer.timer_diff / MSEC_PER_SEC,
             req_timer.timer_diff % MSEC_PER_SEC);
  else
    LogDebug(COMPONENT_DISPATCH,
             "Function %s xid=%u exited with status %d taking %lu.%03lu seconds to process",
             pworker_data->funcdesc->funcname, req->rq_xid, rc,
             req_timer.timer_diff / MSEC_PER_SEC,
             req_timer.timer_diff % MSEC_PER_SEC);

#ifdef _USE_QUEUE_TIMER
  LogFullDebug(COMPONENT_DISPATCH,
               "Function %s xid=%u: await %llu.%.6llu",
               pworker_data->funcdesc->funcname, req->rq_xid,
               req_timer.queue_timer_diff / MSEC_PER_SEC,
               req_timer.queue_timer_diff % MSEC_PER_SEC);
#endif

  /* Perform NFSv4 operations statistics if required */
  if(req->rq_vers == NFS_V4)
      if(req->rq_proc == NFSPROC4_COMPOUND)
          nfs4_op_stat_update(parg_nfs, res_nfs,
                              &(pworker_data->stats.stat_req));

  /* If request is dropped, no return to the client */
  if(rc == NFS_REQ_DROP)
    {
      /* The request was dropped */
      LogDebug(COMPONENT_DISPATCH,
               "Drop request rpc_xid=%u, program %u, version %u, function %u",
               req->rq_xid, (int)req->rq_prog,
               (int)req->rq_vers, (int)req->rq_proc);

      if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
        {
	  LogCrit(COMPONENT_DISPATCH,
		  "Attempt to delete duplicate request failed on line %d",
		  __LINE__);
	}
    }
  else
    {
      DISP_SLOCK(xprt);
      if(svc_sendreply(xprt, req, pworker_data->funcdesc->xdr_encode_func,
		       (caddr_t) res_nfs) == FALSE)
        {
          LogWarn(COMPONENT_DISPATCH,
                  "NFS DISPATCHER: FAILURE: Error while calling "
                  "svc_sendreply on a new request. rpcxid=%u "
                  "socket=%d function:%s client:%s program:%d "
                  "nfs version:%d proc:%d xid:%u",
                  req->rq_xid, xprt->xp_fd,
                  pworker_data->funcdesc->funcname,
                  pworker_data->hostaddr_str, (int)req->rq_prog,
                  (int)req->rq_vers, (int)req->rq_proc, req->rq_xid);
          svcerr_systemerr(xprt, req);

          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          DISP_SUNLOCK(xprt);
          goto freeargs;
        }
      DISP_SUNLOCK(xprt);
    } /* rc == NFS_REQ_DROP */

dupreq_finish:
  /* Mark request as finished */
  if (dpq_status == DUPREQ_SUCCESS)
      dpq_status = nfs_dupreq_finish(req, res_nfs);

freeargs:
  /* Free the arguments */  
  if(!SVC_FREEARGS(xprt,
                   pworker_data->funcdesc->xdr_decode_func,
                   (caddr_t) parg_nfs))
    {
      LogCrit(COMPONENT_DISPATCH,
              "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
              pworker_data->funcdesc->funcname);
    }

  /* Finalize the request (frees reply if required) */
  if (res_nfs)
      nfs_dupreq_rele(req, pworker_data->funcdesc);

  clean_credentials(&pworker_data->user_credentials);

  return;

auth_failure:

  DISP_SLOCK(xprt);
  svcerr_auth(xprt, req, auth_rc);
  DISP_SUNLOCK(xprt);

  clean_credentials(&pworker_data->user_credentials);

  if(nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
    LogCrit(COMPONENT_DISPATCH,
            "Attempt to delete duplicate request after auth failure");

  /* Finalize the request (frees reply if required) */
  if (res_nfs)
      nfs_dupreq_rele(req, pworker_data->funcdesc);
  
/* XXX */
#ifdef SONAS
  rpc_out++;
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

int nfs_Init_worker_data(nfs_worker_data_t *data)
{
  char name[256];

  if(pthread_mutex_init(&(data->request_pool_mutex), NULL) != 0)
    return -1;

  sprintf(name, "Worker Thread #%u", (int)data->worker_index);
  if(tcb_new(&(data->wcb), name) != 0)
    return -1;

  /* init thr waitq */
  init_wait_q_entry(&data->wqe);
  data->wcb.tcb_ready = FALSE;
  data->funcdesc = INVALID_FUNCDESC;

  return 0;
}                               /* nfs_Init_worker_data */

#ifdef _USE_9P
/**
 * _9p_execute: execute a 9p request.
 *
 * Executes 9P request
 *
 * @param nfsreq      [INOUT] pointer to 9p request
 * @param pworker_data [INOUT] pointer to worker's specific data
 *
 * @return nothing (void function)
 *
 */
static void _9p_execute( _9p_request_data_t * preq9p, 
                          nfs_worker_data_t * pworker_data)
{
  _9p_process_request( preq9p, pworker_data ) ;
  return ;
} /* _9p_execute */
#endif

/* XXX include dependency issue prevented declaring in nfs_req_queue.h */
request_data_t *nfs_rpc_dequeue_req(nfs_worker_data_t *worker);

/**
 * worker_thread: The main function for a worker thread
 *
 * This is the body of the worker thread. Its starting arguments are located in
 * global array worker_data. The argument is no pointer but the worker's index.
 * It then uses this index to address its own worker data in the array.
 *
 * @param IndexArg the index for the worker thread, in fact an integer cast as
 * a void *
 *
 * @return Pointer to the result (but this function will mostly loop forever).
 *
 */
void *worker_thread(void *IndexArg)
{
  request_data_t *nfsreq;
  int rc = 0;
  unsigned long worker_index = (unsigned long) IndexArg;
  nfs_worker_data_t *pmydata = &(workers_data[worker_index]);
  char thr_name[32];
  gsh_xprt_private_t *xu = NULL;
  uint32_t refcnt;

  snprintf(thr_name, sizeof(thr_name), "Worker Thread #%lu", worker_index);
  SetNameFunction(thr_name);

  /* save current signal mask */
  rc = pthread_sigmask(SIG_SETMASK, (sigset_t *) 0, &pmydata->sigmask);
  if (rc) {
      LogFatal(COMPONENT_DISPATCH,
               "pthread_sigmask returned %d", rc);
  }

  if(mark_thread_existing(&(pmydata->wcb)) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&(pmydata->wcb));
      LogDebug(COMPONENT_DISPATCH,
               "Worker exiting before initialization");
      return NULL;
    }

  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%lu: my pthread id is %p",
           worker_index, (caddr_t) pthread_self());

  if(FSAL_IS_ERROR(FSAL_InitClientContext(&pmydata->thread_fsal_context)))
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH,
               "Error initializing thread's credential");
    }

  LogInfo(COMPONENT_DISPATCH, "Worker successfully initialized");

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

          /* reset last stat */
          pmydata->stats.last_stat_update = time(NULL);
        }

      LogFullDebug(COMPONENT_DISPATCH, "NFS WORKER #%lu PAUSE/SHUTDOWN check",
                   worker_index);

      /*
       * XXX Fiddling with states in the tcp defeats the purpose of
       * having the abstraction.  If we want to have a lockless pass,
       * that should be an inline routine, or similar.
       */

      /* Get the state without lock first, if things are fine
       * don't bother to check under lock.
       */
      if(pmydata->wcb.tcb_state != STATE_AWAKE) {
          while(1)
            {
              P(pmydata->wcb.tcb_mutex);
              switch(thread_sm_locked(&pmydata->wcb))
                {
                  case THREAD_SM_BREAK:
                      /* XXX ends wait state */
                      V(pmydata->wcb.tcb_mutex);
                      goto wbreak;
                      break;

                  case THREAD_SM_RECHECK:
                    V(pmydata->wcb.tcb_mutex);
                    continue;

                  case THREAD_SM_EXIT:
                    LogDebug(COMPONENT_DISPATCH, "Worker exiting as requested");
                    V(pmydata->wcb.tcb_mutex);
                    return NULL;
                }
            }
        }

    wbreak:
      nfsreq = nfs_rpc_dequeue_req(pmydata);

      if(!nfsreq)
          continue;

      LogFullDebug(COMPONENT_DISPATCH,
                   "Processing a new request, pause_state: %s",
                   pause_state_str[pmydata->wcb.tcb_state]);

      switch(nfsreq->rtype)
      {
       case NFS_REQUEST:
           /* check for destroyed xprts */
           xu = (gsh_xprt_private_t *) nfsreq->r_u.nfs->xprt->xp_u1;
           pthread_mutex_lock(&nfsreq->r_u.nfs->xprt->xp_lock);
           if (xu->flags & XPRT_PRIVATE_FLAG_DESTROYED) {
               pthread_mutex_unlock(&nfsreq->r_u.nfs->xprt->xp_lock);
               goto finalize_req;
           }
           refcnt = xu->refcnt;
           pthread_mutex_unlock(&nfsreq->r_u.nfs->xprt->xp_lock);
           /* execute */
           LogDebug(COMPONENT_DISPATCH,
                    "NFS protocol request, nfsreq=%p xid=%u xprt=%p refcnt=%u",
                    nfsreq,
                    nfsreq->r_u.nfs->msg.rm_xid,
                    nfsreq->r_u.nfs->xprt,
                    refcnt);
           nfs_rpc_execute(nfsreq, pmydata);
           break;

       case NFS_CALL:
           /* NFSv4 rpc call (callback) */
           nfs_rpc_dispatch_call(nfsreq->r_u.call, 0 /* XXX flags */);
           break;

#ifdef _USE_9P
       case _9P_REQUEST:
           _9p_execute(&nfsreq->r_u._9p, pmydata);
#else
           LogCrit(COMPONENT_DISPATCH, "Implementation error, 9P message "
                     "when 9P support is disabled" ) ;
           break;
#endif           
         }

    finalize_req:
      /* XXX needed?  at NIV_DEBUG? */
      LogDebug(COMPONENT_DISPATCH, "Signaling completion of request");

      /* Drop req_cnt and xprt refcnt, if appropriate */
      switch(nfsreq->rtype) {
       case NFS_REQUEST:
           pthread_mutex_lock(&nfsreq->r_u.nfs->xprt->xp_lock);
           --(xu->req_cnt);
           gsh_xprt_unref(
               nfsreq->r_u.nfs->xprt, XPRT_PRIVATE_FLAG_LOCKED);
           break;
       case NFS_CALL:
           break;
       default:
           break;
       }

      /* Free the req by releasing the entry */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Invalidating processed entry");
#ifdef _USE_9P
      if( nfsreq->rtype != _9P_REQUEST && nfsreq->r_u.nfs ) /** @todo : check if this does not produce memleak as 9P is used */
        pool_free(request_data_pool, nfsreq->r_u.nfs);
#else
      switch(nfsreq->rtype) {
       case NFS_REQUEST:
           pool_free(request_data_pool, nfsreq->r_u.nfs);
           break;
      default:
          break;
      }
#endif
      pool_free(request_pool, nfsreq);


    }                           /* while( 1 ) */
  tcb_remove(&pmydata->wcb);
  return NULL;
}                               /* worker_thread */
