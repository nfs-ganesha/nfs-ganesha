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
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "nfs_stat.h"
#include "nfs_tcb.h"
#include "SemN.h"

extern nfs_worker_data_t *workers_data;

pool_t *request_pool;
pool_t *request_data_pool;
pool_t *dupreq_pool;
pool_t *ip_stats_pool;

/* These two variables keep state of the thread that gc at this time */
unsigned int nb_current_gc_workers;
pthread_mutex_t lock_nb_current_gc_workers;

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
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR2args,
   (xdrproc_t) xdr_ATTR2res, "nfs_Setattr",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs2_Root, nfs2_Root_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs2_Root",
   NOTHING_SPECIAL},
  {nfs_Lookup, nfs2_Lookup_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_DIROP2res, "nfs_Lookup",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Readlink, nfs2_Readlink_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_READLINK2res, "nfs_Readlink",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Read, nfs2_Read_Free, (xdrproc_t) xdr_READ2args,
   (xdrproc_t) xdr_READ2res, "nfs_Read",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs2_Writecache, nfs2_Writecache_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs_Writecache",
   NOTHING_SPECIAL},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE2args,
   (xdrproc_t) xdr_ATTR2res, "nfs_Write",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE2args,
   (xdrproc_t) xdr_DIROP2res, "nfs_Create",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_nfsstat2, "nfs_Remove",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Rename",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Link",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK2args,
   (xdrproc_t) xdr_nfsstat2, "nfs_Symlink",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_CREATE2args,
   (xdrproc_t) xdr_DIROP2res, "nfs_Mkdir",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_diropargs2,
   (xdrproc_t) xdr_nfsstat2, "nfs_Rmdir",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs2_Readdir_Free, (xdrproc_t) xdr_READDIR2args,
   (xdrproc_t) xdr_READDIR2res, "nfs_Readdir",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_fhandle2,
   (xdrproc_t) xdr_STATFS2res, "nfs_Fsstat",
   NEEDS_CRED | SUPPORTS_GSS}
};

const nfs_function_desc_t nfs3_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void,
   "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_GETATTR3args,
   (xdrproc_t) xdr_GETATTR3res, "nfs_Getattr", NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Setattr, nfs_Setattr_Free, (xdrproc_t) xdr_SETATTR3args,
   (xdrproc_t) xdr_SETATTR3res, "nfs_Setattr",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Lookup, nfs3_Lookup_Free, (xdrproc_t) xdr_LOOKUP3args,
   (xdrproc_t) xdr_LOOKUP3res, "nfs_Lookup",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Access, nfs3_Access_Free, (xdrproc_t) xdr_ACCESS3args,
   (xdrproc_t) xdr_ACCESS3res, "nfs3_Access",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Readlink, nfs3_Readlink_Free, (xdrproc_t) xdr_READLINK3args,
   (xdrproc_t) xdr_READLINK3res, "nfs_Readlink",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Read, nfs3_Read_Free, (xdrproc_t) xdr_READ3args,
   (xdrproc_t) xdr_READ3res, "nfs_Read",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Write, nfs_Write_Free, (xdrproc_t) xdr_WRITE3args,
   (xdrproc_t) xdr_WRITE3res, "nfs_Write",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Create, nfs_Create_Free, (xdrproc_t) xdr_CREATE3args,
   (xdrproc_t) xdr_CREATE3res, "nfs_Create",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Mkdir, nfs_Mkdir_Free, (xdrproc_t) xdr_MKDIR3args,
   (xdrproc_t) xdr_MKDIR3res, "nfs_Mkdir",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Symlink, nfs_Symlink_Free, (xdrproc_t) xdr_SYMLINK3args,
   (xdrproc_t) xdr_SYMLINK3res, "nfs_Symlink",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs3_Mknod, nfs3_Mknod_Free, (xdrproc_t) xdr_MKNOD3args,
   (xdrproc_t) xdr_MKNOD3res, "nfs3_Mknod",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Remove, nfs_Remove_Free, (xdrproc_t) xdr_REMOVE3args,
   (xdrproc_t) xdr_REMOVE3res, "nfs_Remove",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rmdir, nfs_Rmdir_Free, (xdrproc_t) xdr_RMDIR3args,
   (xdrproc_t) xdr_RMDIR3res, "nfs_Rmdir",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Rename, nfs_Rename_Free, (xdrproc_t) xdr_RENAME3args,
   (xdrproc_t) xdr_RENAME3res, "nfs_Rename",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Link, nfs_Link_Free, (xdrproc_t) xdr_LINK3args,
   (xdrproc_t) xdr_LINK3res, "nfs_Link",
   MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS},
  {nfs_Readdir, nfs3_Readdir_Free, (xdrproc_t) xdr_READDIR3args,
   (xdrproc_t) xdr_READDIR3res, "nfs_Readdir",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Readdirplus, nfs3_Readdirplus_Free, (xdrproc_t) xdr_READDIRPLUS3args,
   (xdrproc_t) xdr_READDIRPLUS3res, "nfs3_Readdirplus",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs_Fsstat, nfs_Fsstat_Free, (xdrproc_t) xdr_FSSTAT3args,
   (xdrproc_t) xdr_FSSTAT3res, "nfs_Fsstat",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Fsinfo, nfs3_Fsinfo_Free, (xdrproc_t) xdr_FSINFO3args,
   (xdrproc_t) xdr_FSINFO3res, "nfs3_Fsinfo",
   NEEDS_CRED},
  {nfs3_Pathconf, nfs3_Pathconf_Free, (xdrproc_t) xdr_PATHCONF3args,
   (xdrproc_t) xdr_PATHCONF3res, "nfs3_Pathconf",
   NEEDS_CRED | SUPPORTS_GSS},
  {nfs3_Commit, nfs3_Commit_Free, (xdrproc_t) xdr_COMMIT3args,
   (xdrproc_t) xdr_COMMIT3res, "nfs3_Commit",
   MAKES_WRITE | NEEDS_CRED | SUPPORTS_GSS}
};

/* Remeber that NFSv4 manages authentication though junction crossing, and
 * so does it for RO FS management (for each operation) */
const nfs_function_desc_t nfs4_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs4_Compound, nfs4_Compound_Free, (xdrproc_t) xdr_COMPOUND4args,
   (xdrproc_t) xdr_COMPOUND4res, "nfs4_Compound", NEEDS_CRED}
};

const nfs_function_desc_t mnt1_func_desc[] = {
  {mnt_Null, mnt_Null_Free, (xdrproc_t) xdr_void,
   (xdrproc_t) xdr_void, "mnt_Null",
   NOTHING_SPECIAL},
  {mnt_Mnt, mnt1_Mnt_Free, (xdrproc_t) xdr_dirpath,
   (xdrproc_t) xdr_fhstatus2, "mnt_Mnt",
   NEEDS_CRED},
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
   NEEDS_CRED},
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
      NEEDS_CRED},
  [NLMPROC4_LOCK] = {
      nlm4_Lock, nlm4_Lock_Free, (xdrproc_t) xdr_nlm4_lockargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Lock",
      NEEDS_CRED},
  [NLMPROC4_CANCEL] = {
      nlm4_Cancel, nlm4_Cancel_Free, (xdrproc_t) xdr_nlm4_cancargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Cancel",
      NEEDS_CRED},
  [NLMPROC4_UNLOCK] = {
      nlm4_Unlock, nlm4_Unlock_Free, (xdrproc_t) xdr_nlm4_unlockargs,
      (xdrproc_t) xdr_nlm4_res, "nlm4_Unlock",
      NEEDS_CRED},
  [NLMPROC4_GRANTED] = {
      nlm4_Unsupported, nlm4_Unsupported_Free, (xdrproc_t) xdr_void,
      (xdrproc_t) xdr_void, "nlm4_Granted",
      NOTHING_SPECIAL},
  [NLMPROC4_TEST_MSG] = {
      nlm4_Test_Message, nlm4_Test_Free,
      (xdrproc_t) xdr_nlm4_testargs,
      (xdrproc_t) xdr_void, "nlm4_Test_msg",
      NEEDS_CRED},
  [NLMPROC4_LOCK_MSG] = {
      nlm4_Lock_Message, nlm4_Lock_Free,
      (xdrproc_t) xdr_nlm4_lockargs,
      (xdrproc_t) xdr_void, "nlm4_Lock_msg",
      NEEDS_CRED},
  [NLMPROC4_CANCEL_MSG] = {
      nlm4_Cancel_Message, nlm4_Cancel_Free,
      (xdrproc_t) xdr_nlm4_cancargs,
      (xdrproc_t) xdr_void, "nlm4_Cancel_msg",
      NEEDS_CRED},
  [NLMPROC4_UNLOCK_MSG] = {
      nlm4_Unlock_Message, nlm4_Unlock_Free,
      (xdrproc_t) xdr_nlm4_unlockargs,
      (xdrproc_t) xdr_void, "nlm4_Unlock_msg",
      NEEDS_CRED},
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
      NEEDS_CRED},
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
                    NEEDS_CRED},
  [NLMPROC4_UNSHARE] = {nlm4_Unshare, nlm4_Unshare_Free,
                        (xdrproc_t) xdr_nlm4_shareargs, (xdrproc_t) xdr_nlm4_shareres,
                        "nlm4_Unshare",
                        NEEDS_CRED},
  [NLMPROC4_NM_LOCK] = {
                        /* NLM_NM_LOCK uses the same handling as NLM_LOCK except for
                         * monitoring, nlm4_Lock will make that determination.
                         */
                        nlm4_Lock, nlm4_Lock_Free,
                        (xdrproc_t) xdr_nlm4_lockargs, (xdrproc_t) xdr_nlm4_res,
                        "nlm4_Nm_lock",
                        NEEDS_CRED},
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

/**
 * nfs_Init_gc_counter: Init the worker's gc counters.
 *
 * This functions is used to init a mutex and a counter associated
 * with it, to keep track of the number of worker currently performing
 * the garbage collection.
 *
 * @param void No parameters
 *
 * @return 0 if successfull, -1 otherwise.
 *
 */

int nfs_Init_gc_counter(void)
{
  pthread_mutexattr_t mutexattr;

  if(pthread_mutexattr_init(&mutexattr) != 0)
    return -1;

  if(pthread_mutex_init(&lock_nb_current_gc_workers, &mutexattr) != 0)
    return -1;

  nb_current_gc_workers = 0;

  return 0;                     /* Success */
}                               /* nfs_Init_gc_counter */

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
 * is_rpc_call_valid: helper function to validate rpc calls.
 *
 */
int is_rpc_call_valid(SVCXPRT *xprt, struct svc_req *preq)
{
  int lo_vers, hi_vers;

  if(preq->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      /* If we go there, preq->rq_prog ==  nfs_param.core_param.program[P_NFS] */
      if(((preq->rq_vers != NFS_V2) || ((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) == 0)) &&
         ((preq->rq_vers != NFS_V3) || ((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) == 0)) &&
         ((preq->rq_vers != NFS_V4) || ((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) == 0)))
        {
          if(xprt != NULL)
            {
              LogEvent(COMPONENT_DISPATCH,
                       "Invalid NFS Version #%d",
                       (int)preq->rq_vers);
              lo_vers = NFS_V4;
              hi_vers = NFS_V2;
              if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
                lo_vers = NFS_V2;
              if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
                {
                  if(lo_vers == NFS_V4)
                    lo_vers = NFS_V3;
                  hi_vers = NFS_V3;
                }
              if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
                hi_vers = NFS_V4;
              svcerr_progvers2(xprt, preq, lo_vers, hi_vers);  /* Bad NFS version */
            }
          return FALSE;
        }
      else if(((preq->rq_vers == NFS_V2) && (preq->rq_proc > NFSPROC_STATFS)) ||
              ((preq->rq_vers == NFS_V3) && (preq->rq_proc > NFSPROC3_COMMIT)) ||
              ((preq->rq_vers == NFS_V4) && (preq->rq_proc > NFSPROC4_COMPOUND)))
        {
          if(xprt != NULL)
            svcerr_noproc2(xprt, preq);
          return FALSE;
        }
      return TRUE;
    }

  if(preq->rq_prog == nfs_param.core_param.program[P_MNT] &&
     ((nfs_param.core_param.core_options & (CORE_OPTION_NFSV2 | CORE_OPTION_NFSV3)) != 0))
    {
      /* Call is with MOUNTPROG */
      /* Verify mount version and report error if invalid */
      lo_vers = MOUNT_V1;
      hi_vers = MOUNT_V3;

      /* Some clients may use the wrong mount version to umount, so always allow umount,
       * otherwise only allow request if the appropriate mount version is enabled.
       * also need to allow dump and export, so just disallow mount if version not supported
       */
      if((preq->rq_vers == MOUNT_V1) &&
         (((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0) ||
          (preq->rq_proc != MOUNTPROC2_MNT)))
        {
          if(preq->rq_proc > MOUNTPROC2_EXPORT)
            {
              if(xprt != NULL)
                svcerr_noproc2(xprt, preq);
              return FALSE;
            }
          return TRUE;
        }
      else if((preq->rq_vers == MOUNT_V3) &&
              (((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0) ||
               (preq->rq_proc != MOUNTPROC2_MNT)))
        {
          if(preq->rq_proc > MOUNTPROC3_EXPORT)
            {
              if(xprt != NULL)
                  svcerr_noproc2(xprt, preq);
              return FALSE;
            }
          return TRUE;
        }

      if(xprt != NULL)
        {
          /* Bad MOUNT version - set the hi and lo versions and report error */
          if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) == 0)
            lo_vers = MOUNT_V3;
          if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) == 0)
            hi_vers = MOUNT_V1;

          LogEvent(COMPONENT_DISPATCH,
                   "Invalid Mount Version #%d",
                   (int)preq->rq_vers);
          svcerr_progvers2(xprt, preq, lo_vers, hi_vers);
        }
      return FALSE;
    }

#ifdef _USE_NLM
  if(preq->rq_prog == nfs_param.core_param.program[P_NLM] &&
          ((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0))
    {
      /* Call is with NLMPROG */
      if(preq->rq_vers != NLM4_VERS)
        {
          /* Bad NLM version */
          LogEvent(COMPONENT_DISPATCH,
                   "Invalid NLM Version #%d",
                   (int)preq->rq_vers);
          if(xprt != NULL)
            svcerr_progvers2(xprt, preq, NLM4_VERS, NLM4_VERS);
          return FALSE;
        }
      if(preq->rq_proc > NLMPROC4_FREE_ALL)
        {
          if(xprt != NULL)
             svcerr_noproc2(xprt, preq);
          return FALSE;
        }
      return TRUE;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_RQUOTA
   if(preq->rq_prog == nfs_param.core_param.program[P_RQUOTA])
     {
       /* Call is with NLMPROG */
       if((preq->rq_vers != RQUOTAVERS) &&
          (preq->rq_vers != EXT_RQUOTAVERS))
         {
           /* Bad RQUOTA version */
           if(xprt != NULL)
             {
               LogEvent(COMPONENT_DISPATCH,
                        "Invalid RQUOTA Version #%d",
                        (int)preq->rq_vers);
               svcerr_progvers2(xprt, preq, RQUOTAVERS, EXT_RQUOTAVERS);
             }
           return FALSE;
         }
       if (((preq->rq_vers == RQUOTAVERS) &&
            (preq->rq_proc > RQUOTAPROC_SETACTIVEQUOTA)) ||
	   ((preq->rq_vers == EXT_RQUOTAVERS) &&
            (preq->rq_proc > RQUOTAPROC_SETACTIVEQUOTA)))
        {
          if(xprt != NULL)
            svcerr_noproc2(xprt, preq);
          return FALSE;
        }
      return TRUE;
     }
#endif                          /* _USE_QUOTA */

  /* No such program */
  if(xprt != NULL)
    {
      LogEvent(COMPONENT_DISPATCH,
               "Invalid Program number #%d",
               (int)preq->rq_prog);
      svcerr_noprog2(xprt, preq);        /* This is no NFS, MOUNT program, exit... */
    }
  return FALSE;
}

/*
 * Extract nfs function descriptor from nfs request.
 */
const nfs_function_desc_t *nfs_rpc_get_funcdesc(nfs_request_data_t *preqnfs)
{
  struct svc_req *req = &preqnfs->req;

  /* Validate rpc call, but don't report any errors here */
  if(is_rpc_call_valid(preqnfs->xprt, req) == FALSE)
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
        return &nfs2_func_desc[req->rq_proc];
      else if(req->rq_vers == NFS_V3)
        return &nfs3_func_desc[req->rq_proc];
      else
        return &nfs4_func_desc[req->rq_proc];
    }

  if(req->rq_prog == nfs_param.core_param.program[P_MNT])
    {
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
  svcerr_noprog2(preqnfs->xprt, req);
  LogEvent(COMPONENT_DISPATCH,
               "INVALID_FUNCDESC for Program %d, Version %d, Function %d",
               (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc);
  return INVALID_FUNCDESC;
}

/*
 * Extract RPC argument.
 */
int nfs_rpc_get_args(nfs_request_data_t *preqnfs, const nfs_function_desc_t *pfuncdesc)
{
  SVCXPRT *xprt = preqnfs->xprt;
  nfs_arg_t *parg_nfs = &preqnfs->arg_nfs;

  memset(parg_nfs, 0, sizeof(nfs_arg_t));

  LogFullDebug(COMPONENT_DISPATCH,
               "Before svc_getargs on socket %d, xprt=%p",
               xprt->xp_fd, xprt);

  if(svc_getargs(xprt, pfuncdesc->xdr_decode_func, (caddr_t) parg_nfs) == FALSE)
    {
      struct svc_req *req = &preqnfs->req;
      LogMajor(COMPONENT_DISPATCH,
                   "svc_getargs failed for Program %d, Version %d, Function %d xid=%u",
               (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc,
               req->rq_xid);
      svcerr_decode2(xprt, req);
      return FALSE;
    }

  return TRUE;
}

/**
 * nfs_rpc_execute: main rpc dispatcher routine
 *
 * This is the regular RPC dispatcher that every RPC server should include.
 *
 * @param[in,out] preq NFS request
 *
 */
static void nfs_rpc_execute(request_data_t *preq,
                            nfs_worker_data_t *pworker_data)
{
  unsigned int export_check_result;
  exportlist_t *pexport = NULL;
  nfs_request_data_t *preqnfs = preq->r_u.nfs;
  nfs_arg_t *parg_nfs = &preqnfs->arg_nfs;
  nfs_res_t res_nfs;
  short exportid;
  LRU_list_t *lru_dupreq = NULL;
  struct svc_req *req = &preqnfs->req;
  SVCXPRT *xprt = preqnfs->xprt;
  nfs_stat_type_t stat_type;
  sockaddr_t hostaddr;
  int port;
  int rc;
  int do_dupreq_cache;
  dupreq_status_t dpq_status;
  exportlist_client_entry_t related_client;
  struct user_cred user_credentials;
  int   update_per_share_stats;
  fsal_op_context_t * pfsal_op_ctx = NULL ;

  struct timeval *timer_start = &pworker_data->timer_start;
  struct timeval timer_end;
  struct timeval timer_diff;
  struct timeval queue_timer_diff;
  nfs_request_latency_stat_t latency_stat;

  memset(&related_client, 0, sizeof(exportlist_client_entry_t));

  /* Get the value from the worker data */
  lru_dupreq = pworker_data->duplicate_request;

  /* initializing RPC structure */
  memset(&res_nfs, 0, sizeof(res_nfs));

  /* If we reach this point, there was no dupreq cache hit or no dup req cache
   * was necessary.  Get NFS function descriptor. */
  pworker_data->pfuncdesc = nfs_rpc_get_funcdesc(preqnfs);
  if(pworker_data->pfuncdesc == INVALID_FUNCDESC)
    return;

  /* XXX must hold lock when calling any TI-RPC channel function,
   * including svc_sendreply2 and the svcerr_* calls */

  if(copy_xprt_addr(&hostaddr, xprt) == 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "copy_xprt_addr failed for Program %d, Version %d, "
                   "Function %d",
                   (int)req->rq_prog, (int)req->rq_vers,
                   (int)req->rq_proc);
      /* XXX move lock wrapper into RPC API */
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      svcerr_systemerr2(xprt, req);
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
      return;
    }

  port = get_port(&hostaddr);

  if(isDebug(COMPONENT_DISPATCH))
    {
      char addrbuf[SOCK_NAME_MAX];
      sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
      LogDebug(COMPONENT_DISPATCH,
               "Request from %s for Program %d, Version %d, Function %d "
               "has xid=%u",
               addrbuf,
               (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc,
               req->rq_xid);
    }

  do_dupreq_cache = pworker_data->pfuncdesc->dispatch_behaviour & CAN_BE_DUP;
  LogFullDebug(COMPONENT_DISPATCH, "do_dupreq_cache = %d", do_dupreq_cache);
  dpq_status = nfs_dupreq_add_not_finished(req, &res_nfs);
  switch(dpq_status)
    {
      /* a new request, continue processing it */
    case DUPREQ_SUCCESS:
      LogFullDebug(COMPONENT_DISPATCH, "Current request is not duplicate.");
      break;
      /* Found the reuqest in the dupreq cache. It's an old request so resend
       * old reply. */
    case DUPREQ_ALREADY_EXISTS:
      if(do_dupreq_cache)
        {
          /* Request was known, use the previous reply */
          LogFullDebug(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: DupReq Cache Hit: using previous "
                       "reply, rpcxid=%u",
                       req->rq_xid);

          LogFullDebug(COMPONENT_DISPATCH,
                       "Before svc_sendreply on socket %d (dup req)",
                       xprt->xp_fd);

          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          if(svc_sendreply2
             (xprt, req, pworker_data->pfuncdesc->xdr_encode_func,
              (caddr_t) &res_nfs) == FALSE)
            {
              LogWarn(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: FAILURE: Error while calling "
                       "svc_sendreply");
              svcerr_systemerr2(xprt, req);
            }
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);

          LogFullDebug(COMPONENT_DISPATCH,
                       "After svc_sendreply on socket %d (dup req)",
                       xprt->xp_fd);
          return;
        }
      else
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Error: Duplicate request rejected because it was found "
                  "in the cache but is not allowed to be cached.");
          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          svcerr_systemerr2(xprt, req);
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          return;
        }
      break;

      /* Another thread owns the request */
    case DUPREQ_BEING_PROCESSED:
      LogFullDebug(COMPONENT_DISPATCH,
                   "Dupreq xid=%u was asked for process since another thread "
                   "manage it, reject for avoiding threads starvation...",
                   req->rq_xid);
      /* Free the arguments */
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      if((preqnfs->req.rq_vers == 2) ||
         (preqnfs->req.rq_vers == 3) ||
         (preqnfs->req.rq_vers == 4)) 
        if(!SVC_FREEARGS(xprt, pworker_data->pfuncdesc->xdr_decode_func,
                         (caddr_t) parg_nfs))
          {
            LogCrit(COMPONENT_DISPATCH,
                    "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                    pworker_data->pfuncdesc->funcname);
          }
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
      /* Ignore the request, send no error */
      return;

      /* something is very wrong with the duplicate request cache */
    case DUPREQ_NOT_FOUND:
      LogCrit(COMPONENT_DISPATCH,
              "Did not find the request in the duplicate request cache and "
              "couldn't add the request.");
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      svcerr_systemerr2(xprt, req);
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
      return;

      /* oom */
    case DUPREQ_INSERT_MALLOC_ERROR:
      LogCrit(COMPONENT_DISPATCH,
              "Cannot process request, not enough memory available!");
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      svcerr_systemerr2(xprt, req);
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
      return;

    default:
      LogCrit(COMPONENT_DISPATCH,
              "Unknown duplicate request cache status. This should never be "
              "reached!");
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      svcerr_systemerr2(xprt, req);
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
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

      char dumpfh[1024];
      char *reason = NULL;
      char addrbuf[SOCK_NAME_MAX];

      switch (req->rq_vers)
        {
        case NFS_V2:
          if(req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs2_FhandleToExportId((fhandle2 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL ||
                 (pexport->options & EXPORT_OPTION_NFSV2) == 0)
                {
                  /* Reject the request for authentication reason (incompatible
                   * file handle) */
                  if(isInfo(COMPONENT_DISPATCH))
                    {
                      sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
                      if(exportid < 0)
                        reason = "has badly formed handle";
                      else if(pexport == NULL)
                        reason = "has invalid export";
                      else
                        reason = "V2 not allowed on this export";
                      sprint_fhandle2(dumpfh, (fhandle2 *) parg_nfs);
                      LogMajor(COMPONENT_DISPATCH,
                               "NFS2 Request from host %s %s, proc=%d, FH=%s",
                               addrbuf, reason,
                               (int)req->rq_proc, dumpfh);
                    }
                  /* Bad argument */
                  svc_dplx_lock_x(xprt, &pworker_data->sigmask);
                  svcerr_auth2(xprt, req, AUTH_FAILED);
                  svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
                  if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
                    {
                      LogCrit(COMPONENT_DISPATCH,
                              "Attempt to delete duplicate request failed on "
                              "line %d", __LINE__);
                    }
                  return;
                }

              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for dirname=%s as exportid=%d",
                           pexport->dirname, pexport->id);
            }
          else
            pexport = nfs_param.pexportlist;

          break;

        case NFS_V3:
          if(req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs3_FhandleToExportId((nfs_fh3 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL ||
                 (pexport->options & EXPORT_OPTION_NFSV3) == 0)
                {

                  if(exportid < 0)
                      reason = "has badly formed handle";
                  else if(pexport == NULL)
                    reason = "has invalid export";
                  else
                    reason = "V3 not allowed on this export";
                }
              else
                {
                  LogFullDebug(COMPONENT_DISPATCH,
                               "Found export entry for dirname=%s as exportid=%d",
                                pexport->dirname, pexport->id);
                  break;
                }
            }
          else if (nfs_param.pexportlist != NULL)
            {
              pexport = nfs_param.pexportlist;
              break;
            }
          else
            reason = "has invalid export";

          /* Reject the request for authentication reason (incompatible
           * file handle) */
          if(isInfo(COMPONENT_DISPATCH))
            {
              sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
              sprint_fhandle3(dumpfh, (nfs_fh3 *) parg_nfs);
              LogMajor(COMPONENT_DISPATCH,
                      "NFS3 Request from host %s %s, proc=%d, FH=%s",
                       addrbuf, reason,
                       (int)req->rq_proc, dumpfh);
            }
          /* Bad argument */
          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          svcerr_auth2(xprt, req, AUTH_FAILED);
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on "
                      "line %d", __LINE__);
             }
          return;

        case NFS_V4:
          /* NFSv4 requires entire export list */
          pexport = nfs_param.pexportlist;
          break;

        default:
          /* NFSv4 or invalid version (which should never get here) */
          pexport = nfs_param.pexportlist;
          break;
        }                       /* switch( ptr_req->rq_vers ) */
    }
#ifdef _USE_NLM
  else if(req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      netobj *pfh3 = NULL;

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
                                             exportid)) == NULL ||
             (pexport->options & EXPORT_OPTION_NFSV3) == 0)
            {
              /* Reject the request for authentication reason (incompatible 
               * file handle) */
              if(isInfo(COMPONENT_DISPATCH))
                {
                  char dumpfh[1024];
                  char *reason;
                  char addrbuf[SOCK_NAME_MAX];
                  sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
                  if(exportid < 0)
                    reason = "has badly formed handle";
                  else if(pexport == NULL)
                    reason = "has invalid export";
                  else
                    reason = "V3 not allowed on this export";
                  sprint_fhandle_nlm(dumpfh, pfh3);
                  LogMajor(COMPONENT_DISPATCH,
                           "NLM4 Request from host %s %s, proc=%d, FH=%s",
                           addrbuf, reason,
                           (int)req->rq_proc, dumpfh);
                }
              /* Bad argument */
              svc_dplx_lock_x(xprt, &pworker_data->sigmask);
              svcerr_auth2(xprt, req, AUTH_FAILED);
              svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
              if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
                {
                  LogCrit(COMPONENT_DISPATCH,
                          "Attempt to delete duplicate request failed on line "
                          "%d", __LINE__);
                }
              return;
            }

          LogFullDebug(COMPONENT_DISPATCH,
                       "Found export entry for dirname=%s as exportid=%d",
                       pexport->dirname, pexport->id);
        }
      else
        pexport = nfs_param.pexportlist;
    }
#endif                          /* _USE_NLM */
  else
    {
      /* All other protocols use the whole export list */
      pexport = nfs_param.pexportlist;
    }

  if(pworker_data->pfuncdesc->dispatch_behaviour & SUPPORTS_GSS)
    {
      /* Test if export allows the authentication provided */
      if (nfs_export_check_security(req, pexport) == FALSE)
        {
          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          svcerr_auth2(xprt, req, AUTH_TOOWEAK);
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on "
                      "line %d", __LINE__);
            }
          return;
        }
    }

  /* Zero out timers prior to starting processing */
  memset(&timer_end, 0, sizeof(struct timeval));
  memset(&timer_diff, 0, sizeof(struct timeval));
  memset(&queue_timer_diff, 0, sizeof(struct timeval));

  /*
   * It is now time for checking if export list allows the machine to perform
   * the request
   */
  pworker_data->hostaddr = hostaddr;

  /* Check if client is using a privileged port, but only for NFS protocol */
  if ((req->rq_prog == nfs_param.core_param.program[P_NFS]) &&
      (req->rq_proc != 0))
    {
      if ((pexport->options & EXPORT_OPTION_PRIVILEGED_PORT) &&
         (port >= IPPORT_RESERVED))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Port %d is too high for this export entry, rejecting client",
                  port);
          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          svcerr_auth2(xprt, req, AUTH_TOOWEAK);
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          /* XXX */
          pworker_data->current_xid = 0;    /* No more xid managed */

          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          return;
        }
    }

  if (pworker_data->pfuncdesc->dispatch_behaviour & NEEDS_CRED)
    {
      if (get_req_uid_gid(req, pexport, &user_credentials) == FALSE)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "could not get uid and gid, rejecting client");
          svc_dplx_lock_x(xprt, &pworker_data->sigmask);
          svcerr_auth2(xprt, req, AUTH_TOOWEAK);
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          /* XXX */
          pworker_data->current_xid = 0;    /* No more xid managed */

          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          return;
        }
    }

  /* Be careful (Issue #66) : it makes no sense to check access for
   * a MOUNT request */
  if((req->rq_prog != nfs_param.core_param.program[P_MNT]) &&
     !((req->rq_prog == nfs_param.core_param.program[P_NFS]) &&
      (req->rq_vers == NFS_V4)))
   {
     LogFullDebug(COMPONENT_DISPATCH,
                  "nfs_rpc_execute about to call nfs_export_check_access");
     export_check_result = nfs_export_check_access(&pworker_data->hostaddr,
                                                   req,
                                                   pexport,
                                                   nfs_param.core_param.program[P_NFS],
                                                   nfs_param.core_param.program[P_MNT],
                                                   pworker_data->ht_ip_stats,
                                                   ip_stats_pool,
                                                   &related_client,
                                                   &user_credentials,
                                                   (pworker_data->pfuncdesc->dispatch_behaviour & MAKES_WRITE) == MAKES_WRITE);
   }
  else
   {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Call to a function from the MOUNT protocol, no call to nfs_export_check_access() required" ) ;
      export_check_result = EXPORT_PERMISSION_GRANTED ;
   }

  if (export_check_result == EXPORT_PERMISSION_DENIED)
    {
      char addrbuf[SOCK_NAME_MAX];
      sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
      LogInfo(COMPONENT_DISPATCH,
              "Host %s is not allowed to access this export entry, vers=%d, proc=%d",
              addrbuf,
              (int)req->rq_vers, (int)req->rq_proc);
      svc_dplx_lock_x(xprt, &pworker_data->sigmask);
      svcerr_auth2(xprt, req, AUTH_TOOWEAK);
      svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
      /* XXX */
      pworker_data->current_xid = 0;        /* No more xid managed */

      if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Attempt to delete duplicate request failed on line %d",
                  __LINE__);
        }
      return;
    }
  else if ((export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_RO) ||
           (export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO))
    {
      LogDebug(COMPONENT_DISPATCH,
               "Dropping request because nfs_export_check_access() reported this is a RO filesystem.");
      if(req->rq_prog == nfs_param.core_param.program[P_NFS])
        {
          if(req->rq_vers == NFS_V2)
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
  else if ((export_check_result != EXPORT_PERMISSION_GRANTED) &&
           (export_check_result != EXPORT_MDONLY_GRANTED))
    {
      /* If not EXPORT_PERMISSION_GRANTED, then we are all out of options! */
      LogMajor(COMPONENT_DISPATCH,
               "nfs_export_check_access() returned none of the expected flags. This is an unexpected state!");
      rc = NFS_REQ_DROP;
    }
  else  /* export_check_result == EXPORT_PERMISSION_GRANTED is TRUE */
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "nfs_export_check_access() reported PERMISSION GRANTED.");

      /* Do the authentication stuff, if needed */
      if(pworker_data->pfuncdesc->dispatch_behaviour & NEEDS_CRED)
        {
          /* Swap the anonymous uid/gid if the user should be anonymous */
          if(nfs_check_anon(&related_client, pexport, &user_credentials) == FALSE
             || nfs_build_fsal_context(req,
                                       pexport,
                                       &pworker_data->thread_fsal_context,
                                       &user_credentials) == FALSE)
            {
              LogInfo(COMPONENT_DISPATCH,
                      "authentication failed, rejecting client");
              svc_dplx_lock_x(xprt, &pworker_data->sigmask);
              svcerr_auth2(xprt, req, AUTH_TOOWEAK);
              svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
              /* XXX */
              pworker_data->current_xid = 0;    /* No more xid managed */

              if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
                {
                  LogCrit(COMPONENT_DISPATCH,
                         "Attempt to delete duplicate request failed on line %d",
                         __LINE__);
                }
              return;
            }
        }

      /* processing */
      P(pworker_data->request_pool_mutex);
      gettimeofday(timer_start, NULL);

      LogDebug(COMPONENT_DISPATCH,
               "NFS DISPATCHER: Calling service function %s start_time %llu.%.6llu",
               pworker_data->pfuncdesc->funcname,
               (unsigned long long)timer_start->tv_sec,
               (unsigned long long)timer_start->tv_usec);

      V(pworker_data->request_pool_mutex);

#ifdef _ERROR_INJECTION
      if(worker_delay_time != 0)
        sleep(worker_delay_time);
      else if(next_worker_delay_time != 0)
        {
          sleep(next_worker_delay_time);
          next_worker_delay_time = 0;
        }
#endif

      pfsal_op_ctx =  &pworker_data->thread_fsal_context ;

      rc = pworker_data->pfuncdesc->service_function(parg_nfs,
                                                     pexport,
                                                     pfsal_op_ctx,
                                                     pworker_data,
                                                     req,
                                                     &res_nfs);
    }

  /* Perform statistics here */
  gettimeofday(&timer_end, NULL);

  /* process time */
  stat_type = (rc == NFS_REQ_OK) ? GANESHA_STAT_SUCCESS : GANESHA_STAT_DROP;
  P(pworker_data->request_pool_mutex);
  timer_diff = time_diff(*timer_start, timer_end);

  /* this thread is done, reset the timer start to avoid long processing */
  memset(timer_start, 0, sizeof(struct timeval));
  V(pworker_data->request_pool_mutex);
  latency_stat.type = SVC_TIME;
  latency_stat.latency = timer_diff.tv_sec * 1000000
    + timer_diff.tv_usec; /* microseconds */
  nfs_stat_update(stat_type, &(pworker_data->stats.stat_req), req,
                  &latency_stat);

  if ((req->rq_prog == nfs_param.core_param.program[P_MNT]) ||
      ((req->rq_prog == nfs_param.core_param.program[P_NFS]) &&
       (req->rq_proc == 0 /*NULL RPC*/ ))) {
      update_per_share_stats = FALSE;
  } else {
      update_per_share_stats = TRUE;
  }
  /* Update per-share counter and process time */
  if (update_per_share_stats) {
      nfs_stat_update(stat_type,
		      &(pexport->worker_stats[pworker_data->worker_index].stat_req),
		      req, &latency_stat);
  }

  /* process time + queue time */
  queue_timer_diff = time_diff(preqnfs->time_queued, timer_end);
  latency_stat.type = AWAIT_TIME;
  latency_stat.latency = queue_timer_diff.tv_sec * 1000000
    + queue_timer_diff.tv_usec; /* microseconds */
  nfs_stat_update(GANESHA_STAT_SUCCESS, &(pworker_data->stats.stat_req), req,
                  &latency_stat);

  /* Update per-share process time + queue time */
  if (update_per_share_stats) {
      nfs_stat_update(GANESHA_STAT_SUCCESS,
		      &(pexport->worker_stats[pworker_data->worker_index].stat_req),
		      req, &latency_stat);

      /* Update per-share total counters */
      pexport->worker_stats[pworker_data->worker_index].nb_total_req += 1;
  }

  /* Update total counters */
  pworker_data->stats.nb_total_req += 1;

  if(timer_diff.tv_sec >= nfs_param.core_param.long_processing_threshold)
    LogEvent(COMPONENT_DISPATCH,
             "Function %s xid=%u exited with status %d taking %llu.%.6llu seconds to process",
             pworker_data->pfuncdesc->funcname, req->rq_xid, rc,
             (unsigned long long)timer_diff.tv_sec,
             (unsigned long long)timer_diff.tv_usec);
  else
    LogDebug(COMPONENT_DISPATCH,
             "Function %s xid=%u exited with status %d taking %llu.%.6llu seconds to process",
             pworker_data->pfuncdesc->funcname, req->rq_xid, rc,
             (unsigned long long)timer_diff.tv_sec,
             (unsigned long long)timer_diff.tv_usec);

  LogFullDebug(COMPONENT_DISPATCH,
               "Function %s xid=%u: process %llu.%.6llu await %llu.%.6llu",
               pworker_data->pfuncdesc->funcname, req->rq_xid,
               (unsigned long long int)timer_diff.tv_sec,
               (unsigned long long int)timer_diff.tv_usec,
               (unsigned long long int)queue_timer_diff.tv_sec,
               (unsigned long long int)queue_timer_diff.tv_usec);
  
  /* Perform NFSv4 operations statistics if required */
  if(req->rq_vers == NFS_V4)
      if(req->rq_proc == NFSPROC4_COMPOUND)
          nfs4_op_stat_update(parg_nfs, &res_nfs,
                              &(pworker_data->stats.stat_req));

  /* XXX */
  pworker_data->current_xid = 0;        /* No more xid managed */

  /* If request is dropped, no return to the client */
  if(rc == NFS_REQ_DROP)
    {
      /* The request was dropped */
      LogDebug(COMPONENT_DISPATCH,
               "Drop request rpc_xid=%u, program %u, version %u, function %u",
               req->rq_xid, (int)req->rq_prog,
               (int)req->rq_vers, (int)req->rq_proc);

      /* If the request is not normally cached, then the entry will be removed
       * later. We only remove a reply that is normally cached that has been
       * dropped. */
      if(do_dupreq_cache)
        if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
          {
            LogCrit(COMPONENT_DISPATCH,
                    "Attempt to delete duplicate request failed on line %d",
                    __LINE__);
          }
    }
  else
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Before svc_sendreply on socket %d",
                   xprt->xp_fd);

      svc_dplx_lock_x(xprt, &pworker_data->sigmask);

      /* encoding the result on xdr output */
      if(svc_sendreply2(xprt, req, pworker_data->pfuncdesc->xdr_encode_func,
                        (caddr_t) &res_nfs) == FALSE)
        {
          LogEvent(COMPONENT_DISPATCH,
                   "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply");
          svcerr_systemerr2(xprt, req);

          if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
          return;
        }

      LogFullDebug(COMPONENT_DISPATCH,
                   "After svc_sendreply on socket %d",
                   xprt->xp_fd);

      /* Mark request as finished */
      LogFullDebug(COMPONENT_DUPREQ, "YES?: %d", do_dupreq_cache);
      if(do_dupreq_cache)
        {
          dpq_status = nfs_dupreq_finish(req, &res_nfs, lru_dupreq);
        }
    } /* rc == NFS_REQ_DROP */

  /* Free the allocated resources once the work is done */
  /* Free the arguments */
  if((preqnfs->req.rq_vers == 2) ||
     (preqnfs->req.rq_vers == 3) ||
     (preqnfs->req.rq_vers == 4))
      if(!SVC_FREEARGS(xprt, pworker_data->pfuncdesc->xdr_decode_func,
                       (caddr_t) parg_nfs))
      {
        LogCrit(COMPONENT_DISPATCH,
                "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                pworker_data->pfuncdesc->funcname);
      }

  /* XXX we must hold xprt lock across SVC_FREEARGS */
  svc_dplx_unlock_x(xprt, &pworker_data->sigmask);
    
  /* Free the reply.
   * This should not be done if the request is dupreq cached because this will
   * mark the dupreq cached info eligible for being reuse by other requests */
  if(!do_dupreq_cache)
    {
      if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Attempt to delete duplicate request failed on line %d",
                  __LINE__);
        }
      /* Free only the non dropped requests */
      if(rc == NFS_REQ_OK) {
          pworker_data->pfuncdesc->free_function(&res_nfs);
      }
    }

  /* By now the dupreq cache entry should have been completed w/ a request
   * that is reusable or the dupreq cache entry should have been removed. */
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
  char name[256];

  if(pthread_mutex_init(&(pdata->request_pool_mutex), NULL) != 0)
    return -1;

  sprintf(name, "Worker Thread #%u", (int)pdata->worker_index);
  if(tcb_new(&(pdata->wcb), name) != 0)
    return -1;

  init_glist(&pdata->pending_request);
  pdata->pending_request_len = 0;

  sprintf(name, "Worker Thread #%u Duplicate Request", pdata->worker_index);
  nfs_param.worker_param.lru_dupreq.lp_name = name;

  if((pdata->duplicate_request =
      LRU_Init(nfs_param.worker_param.lru_dupreq, &status)) == NULL)
    {
      LogError(COMPONENT_DISPATCH, ERR_LRU, ERR_LRU_LIST_INIT, status);
      return -1;
    }

  pdata->passcounter = 0;
  pdata->wcb.tcb_ready = FALSE;
  pdata->gc_in_progress = FALSE;
  pdata->pfuncdesc = INVALID_FUNCDESC;

  return 0;
}                               /* nfs_Init_worker_data */

void DispatchWorkNFS(request_data_t *nfsreq, unsigned int worker_index)
{
  struct svc_req *req = NULL;
  uint32_t rpcxid = 0;

  switch (nfsreq->rtype) {
  case NFS_CALL:
      break;
  default:
      req = &nfsreq->r_u.nfs->req;
      rpcxid = req->rq_xid;
  }

  LogDebug(COMPONENT_DISPATCH,
           "Awaking Worker Thread #%u for request %p, rtype=%d xid=%u",
           worker_index, nfsreq, nfsreq->rtype, rpcxid);

  P(workers_data[worker_index].wcb.tcb_mutex);
  P(workers_data[worker_index].request_pool_mutex);

  glist_add_tail(&workers_data[worker_index].pending_request, &nfsreq->pending_req_queue);
  workers_data[worker_index].pending_request_len++;

  if(pthread_cond_signal(&(workers_data[worker_index].wcb.tcb_condvar)) == -1)
    {
      V(workers_data[worker_index].request_pool_mutex);
      V(workers_data[worker_index].wcb.tcb_mutex);
      LogMajor(COMPONENT_THREAD,
               "Error %d (%s) while signalling Worker Thread #%u... Exiting",
               errno, strerror(errno), worker_index);
      Fatal();
    }
  V(workers_data[worker_index].request_pool_mutex);
  V(workers_data[worker_index].wcb.tcb_mutex);
}

enum auth_stat AuthenticateRequest(nfs_request_data_t *nfsreq,
                                   bool_t *no_dispatch)
{
  struct rpc_msg *msg;
  struct svc_req *req;
  SVCXPRT *xprt;
  enum auth_stat why;

  /* A few words of explanation are required here:
   * In authentication is AUTH_NONE or AUTH_UNIX, then the value of no_dispatch
   * remains FALSE and the request is proceeded normally.
   * If authentication is RPCSEC_GSS, no_dispatch may have value TRUE, this
   * means that gc->gc_proc != RPCSEC_GSS_DATA and that the message is in fact
   * an internal negociation message from RPCSEC_GSS using GSSAPI. It then
   * should not be proceed by the worker and SVC_STAT should be returned to
   * the dispatcher.
   */

  *no_dispatch = FALSE;

  /* Set pointers */
  msg = &(nfsreq->msg);
  req = &(nfsreq->req);
  xprt = nfsreq->xprt;

  req->rq_xprt = nfsreq->xprt;
  req->rq_prog = msg->rm_call.cb_prog;
  req->rq_vers = msg->rm_call.cb_vers;
  req->rq_proc = msg->rm_call.cb_proc;
  req->rq_xid = msg->rm_xid;

  LogFullDebug(COMPONENT_DISPATCH,
               "About to authenticate Prog=%d, vers=%d, proc=%d xid=%u xprt=%p",
               (int)req->rq_prog, (int)req->rq_vers,
               (int)req->rq_proc, req->rq_xid, req->rq_xprt);

  /* XXX Restore previously saved GssData.
   * I'm not clear what we're restoring here.  Operating on xprt means that
   * we do (did) not share buffers with xprt_copy.
   */
#ifdef _HAVE_GSSAPI
  if((why = Rpcsecgss__authenticate(req, msg, no_dispatch)) != AUTH_OK)
#else
  if((why = _authenticate(req, msg)) != AUTH_OK)
#endif
    {
      char auth_str[AUTH_STR_LEN];
      auth_stat2str(why, auth_str);
      LogInfo(COMPONENT_DISPATCH,
              "Could not authenticate request... rejecting with AUTH_STAT=%s",
              auth_str);
      svcerr_auth2(xprt, req, why);
      *no_dispatch = TRUE;
      return why;
    }
  else
    {
#ifdef _HAVE_GSSAPI
      struct rpc_gss_cred *gc;

      if(req->rq_xprt->xp_verf.oa_flavor == RPCSEC_GSS)
        {
          gc = (struct rpc_gss_cred *) req->rq_clntcred;
          LogFullDebug(COMPONENT_DISPATCH,
                       "AuthenticateRequest no_dispatch=%d gc->gc_proc=(%u) %s",
                       *no_dispatch, gc->gc_proc, str_gc_proc(gc->gc_proc));
        }
#endif
    }             /* else from if( ( why = _authenticate( preq, pmsg) ) != AUTH_OK) */
  return AUTH_OK;
}

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

static inline enum xprt_stat
cond_multi_dispatch(nfs_worker_data_t *pmydata, request_data_t *nfsreq,
                    bool *locked)
{
    enum xprt_stat stat;
    bool_t try_multi = FALSE, dispatched = FALSE;
    SVCXPRT *xprt = nfsreq->r_u.nfs->xprt;

    stat = SVC_STAT(xprt);
    svc_dplx_unlock_x(xprt, &pmydata->sigmask);
    *locked = FALSE;

    if (stat == XPRT_MOREREQS)
        try_multi = TRUE;

#if 0 /* XXX */
    try_multi = FALSE;
#endif

    if (try_multi) {
        process_status_t rc_multi __attribute__((unused));
        gsh_xprt_private_t *xu;
        pthread_rwlock_wrlock(&xprt->lock);
        xu = (gsh_xprt_private_t *) xprt->xp_u1;

        LogDebug(COMPONENT_DISPATCH, "xprt=%p try_multi=TRUE multi_cnt=%u "
                "refcnt=%u",
                xprt,
                xu->multi_cnt,
                xu->refcnt);

        /* we need an atomic total-outstanding counter, check against hiwat */
        if (xu->multi_cnt < nfs_param.core_param.dispatch_multi_xprt_max) {
            ++(xu->multi_cnt);
            /* dispatch it */
            rc_multi = dispatch_rpc_subrequest(pmydata, nfsreq);
            dispatched = TRUE;
        }
        pthread_rwlock_unlock(&xprt->lock);
    }

    if (! dispatched) {
        /* Execute it */
        nfs_rpc_execute(nfsreq, pmydata);
    }

    return (stat);
}

/**
 * nfs_worker_process_rpc_requests: read and process a sequence of RPC
 * requests.
 */

#define DISP_LOCK(x) do { \
    if (! locked) { \
        svc_dplx_lock_x(xprt, &pmydata->sigmask); \
        locked = TRUE; \
      }\
    } while (0);

#define DISP_UNLOCK(x) do { \
    if (locked) { \
        svc_dplx_unlock_x(xprt, &pmydata->sigmask); \
        locked = FALSE; \
      }\
    } while (0);

process_status_t
nfs_worker_process_rpc_requests(nfs_worker_data_t *pmydata,
                                request_data_t *nfsreq)
{
  enum xprt_stat stat = XPRT_IDLE;
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  const nfs_function_desc_t *pfuncdesc;
  bool_t no_dispatch = TRUE, recv_status;
  process_status_t rc = PROCESS_DONE;
  SVCXPRT *xprt;
  bool locked = FALSE;
again:
  /*
   * Receive from socket.
   * Will block until the client operates on the socket
   */
  xprt = nfsreq->r_u.nfs->xprt;

  LogFullDebug(COMPONENT_DISPATCH,
               "Before calling SVC_RECV on socket %d",
               xprt->xp_fd);

  rc = PROCESS_DONE;

  preq = &nfsreq->r_u.nfs->req;
  pmsg = &nfsreq->r_u.nfs->msg;

  DISP_LOCK(xprt);
  recv_status = SVC_RECV(xprt, pmsg);

  LogFullDebug(COMPONENT_DISPATCH,
               "Status for SVC_RECV on socket %d is %d, xid=%lu",
               xprt->xp_fd,
               recv_status,
               (unsigned long)pmsg->rm_xid);

  /* If status is ok, the request will be processed by the related
   * worker, otherwise, it should be released by being tagged as invalid. */
  if (!recv_status)
    {
      /* RPC over TCP specific: RPC/UDP's xprt know only one state: XPRT_IDLE,
       * because UDP is mostly a stateless protocol.  With RPC/TCP, they can be
       * XPRT_DIED especially when the client closes the peer's socket. We
       * have to cope with this aspect in the next lines.  Finally, xdrrec
       * uses XPRT_MOREREQS to indicate that additional records are ready to
       * be consumed immediately. */

        /* XXXX */
      sockaddr_t addr;
      char addrbuf[SOCK_NAME_MAX];

      if(copy_xprt_addr(&addr, xprt) == 1)
        sprint_sockaddr(&addr, addrbuf, sizeof(addrbuf));
      else
        sprintf(addrbuf, "<unresolved>");

      stat = SVC_STAT(xprt);
      DISP_UNLOCK(xprt);

      if(stat == XPRT_DIED)
        {

          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s disappeared...",
                   xprt->xp_fd, addrbuf);
          DISP_LOCK(xprt);
          gsh_xprt_destroy(xprt);
          rc = PROCESS_LOST_CONN;
        }
      else if(stat == XPRT_MOREREQS)
        {
          /* unexpected case */
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status XPRT_MOREREQS",
                   xprt->xp_fd, addrbuf);
        }
      else if(stat == XPRT_IDLE)
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status XPRT_IDLE",
                   xprt->xp_fd, addrbuf);
        }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Client on socket=%d, addr=%s has status unknown (%d)",
                   xprt->xp_fd, addrbuf, (int)stat);
        }
      goto unblock;
    }
  else
    {
      nfsreq->r_u.nfs->req.rq_prog = pmsg->rm_call.cb_prog;
      nfsreq->r_u.nfs->req.rq_vers = pmsg->rm_call.cb_vers;
      nfsreq->r_u.nfs->req.rq_proc = pmsg->rm_call.cb_proc;
      nfsreq->r_u.nfs->req.rq_xid = pmsg->rm_xid;

      pfuncdesc = nfs_rpc_get_funcdesc(nfsreq->r_u.nfs);
      if(pfuncdesc == INVALID_FUNCDESC)
          goto unblock;

      DISP_LOCK(xprt);
      if(AuthenticateRequest(nfsreq->r_u.nfs,
                             &no_dispatch) != AUTH_OK || no_dispatch) {
          goto unblock;
      }

      if(!nfs_rpc_get_args(nfsreq->r_u.nfs, pfuncdesc))
          goto unblock;

      preq->rq_xprt = xprt;

      /* Validate the rpc request as being a valid program, version,
       * and proc. If not, report the error. Otherwise, execute the
       * funtion. */
      if(is_rpc_call_valid(preq->rq_xprt, preq) == TRUE)
          stat = cond_multi_dispatch(pmydata, nfsreq, &locked);

      rc = PROCESS_DISPATCHED;
    }

unblock:
  /* continue receiving if data is available--this isn't optional, because
   * irrespective of TI-RPC "nonblock" mode, we will frequently have consumed
   * additional RPC records (TCP).  Also, we expect to move the SVC_RECV
   * into the worker thread, so this will asynchronous wrt to the shared
   * event loop */
  if (rc == PROCESS_DISPATCHED) {
      if (stat == XPRT_MOREREQS)
          goto again;
      else {
          /* XXX dont bother re-arming epoll for xprt if there is data 
           * waiting */
          struct pollfd fd;
          fd.fd = xprt->xp_fd;
          fd.events = POLLIN;
          if (poll(&fd, 1, 0 /* ms, ie, now */) > 0)
              goto again;
      }
  }

  DISP_UNLOCK(xprt);

  if (rc != PROCESS_LOST_CONN)
      (void) svc_rqst_unblock_events(nfsreq->r_u.nfs->xprt,
                                     SVC_RQST_FLAG_NONE);

  return (rc);
}

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
  unsigned int gc_allowed = FALSE;
  unsigned long worker_index = (unsigned long) IndexArg;
  nfs_worker_data_t *pmydata = &(workers_data[worker_index]);
  char thr_name[32];
  gsh_xprt_private_t *xu = NULL;

#ifdef _USE_SHARED_FSAL
  unsigned int i = 0 ;
  unsigned int fsalid = 0 ;
#endif

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

  LogFullDebug(COMPONENT_DISPATCH,
               "Starting, pending=%d", pmydata->pending_request_len);

  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%lu: my pthread id is %p",
           worker_index, (caddr_t) pthread_self());

  /* Initialisation of credential for current thread */
  LogFullDebug(COMPONENT_DISPATCH,
               "NFS WORKER #%lu: Initialization of thread's credential",
               worker_index);

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

      /* Wait on condition variable for work to be done */
      LogFullDebug(COMPONENT_DISPATCH,
                   "waiting for requests to process, pending=%d",
                   pmydata->pending_request_len);

      /* Get the state without lock first, if things are fine
       * don't bother to check under lock.
       */
      if((pmydata->wcb.tcb_state != STATE_AWAKE) ||
          (pmydata->pending_request_len == 0)) {
          while(1)
            {
              P(pmydata->wcb.tcb_mutex);
              if(pmydata->wcb.tcb_state == STATE_AWAKE &&
                 (pmydata->pending_request_len != 0)) {
                  V(pmydata->wcb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&pmydata->wcb))
                {
                  case THREAD_SM_RECHECK:
                    V(pmydata->wcb.tcb_mutex);
                    continue;

                  case THREAD_SM_BREAK:
                    if(pmydata->pending_request_len == 0) {
                        /* No work; wait */
                        pthread_cond_wait(&(pmydata->wcb.tcb_condvar),
                                          &(pmydata->wcb.tcb_mutex));
                        V(pmydata->wcb.tcb_mutex);
                        continue;
                      }

                  case THREAD_SM_EXIT:
                    LogDebug(COMPONENT_DISPATCH, "Worker exiting as requested");
                    V(pmydata->wcb.tcb_mutex);
                    return NULL;
                }
            }
        }

      LogFullDebug(COMPONENT_DISPATCH,
                   "Processing a new request, pause_state: %s, pending=%u",
                   pause_state_str[pmydata->wcb.tcb_state],
                   pmydata->pending_request_len);

      P(pmydata->request_pool_mutex);
      nfsreq = glist_first_entry(&pmydata->pending_request, request_data_t,
                                  pending_req_queue);
      if (nfsreq == NULL) {
        V(pmydata->request_pool_mutex);
        LogMajor(COMPONENT_DISPATCH, "No pending request available");
        continue;             /* return to main loop */
      }
      glist_del(&nfsreq->pending_req_queue);
      pmydata->pending_request_len--;
      V(pmydata->request_pool_mutex);

      /* Check for destroyed xprts */
      switch(nfsreq->rtype) {
      case NFS_REQUEST_LEADER:
      case NFS_REQUEST:
          xu = (gsh_xprt_private_t *) nfsreq->r_u.nfs->xprt->xp_u1;
          pthread_rwlock_rdlock(&nfsreq->r_u.nfs->xprt->lock);
          if (xu->flags & XPRT_PRIVATE_FLAG_DESTROYED) {
              pthread_rwlock_unlock(&nfsreq->r_u.nfs->xprt->lock);
              goto finalize_req;
          }
          pthread_rwlock_unlock(&nfsreq->r_u.nfs->xprt->lock);
          break;
      default:
          break;
      }

      switch(nfsreq->rtype)
       {
          case NFS_REQUEST_LEADER:
              LogDebug(COMPONENT_DISPATCH,
                       "Multi-dispatch leader, nfsreq=%p, pending=%d, "
                       "xid=%u xprt=%p refcnt=%u",
                       nfsreq,
                       pmydata->pending_request_len,
                       nfsreq->r_u.nfs->msg.rm_xid,
                       nfsreq->r_u.nfs->xprt,
                       xu->refcnt);

           if(nfsreq->r_u.nfs->xprt->xp_fd == 0)
             {
               LogFullDebug(COMPONENT_DISPATCH,
                            "RPC dispatch error:  nfsreq=%p, xp_fd==0",
                            nfsreq);
             }
           else
             {
               /* Process the sequence */
               (void) nfs_worker_process_rpc_requests(pmydata, nfsreq);
             }
           break;

       case NFS_REQUEST:
           LogDebug(COMPONENT_DISPATCH,
                    "Multi-dispatch subrequest, nfsreq=%p, pending=%d, "
                    "xid=%u xprt=%p refcnt=%u",
                    nfsreq,
                    pmydata->pending_request_len,
                    nfsreq->r_u.nfs->msg.rm_xid,
                    nfsreq->r_u.nfs->xprt,
                    xu->refcnt);
           nfs_rpc_execute(nfsreq, pmydata);
           break;

       case NFS_CALL:
           /* NFSv4 rpc call (callback) */
           nfs_rpc_dispatch_call(nfsreq->r_u.call, 0 /* XXX flags */);
           break;

       case _9P_REQUEST:
#ifdef _USE_9P
           _9p_execute(&nfsreq->r_u._9p, pmydata);
#else
           LogCrit(COMPONENT_DISPATCH, "Implementation error, 9P message "
                     "when 9P support is disabled" ) ;
#endif
           break;
         }

    finalize_req:
      /* XXX Signal the request processing has completed, though at
       * present there may be no effect. */
      LogInfo(COMPONENT_DISPATCH, "Signaling completion of request");

      /* Drop multi_cnt and xprt refcnt, if appropriate */
      switch(nfsreq->rtype) {
       case NFS_REQUEST_LEADER:
           gsh_xprt_unref(
               nfsreq->r_u.nfs->xprt, XPRT_PRIVATE_FLAG_NONE);
           break;
       case NFS_REQUEST:
           pthread_rwlock_wrlock(&nfsreq->r_u.nfs->xprt->lock);
           --(xu->multi_cnt);
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
      if( nfsreq->rtype != _9P_REQUEST && nfsreq->r_u.nfs ) /** @todo : check if this does not produce memleak as 9P is used */
        pool_free(request_data_pool, nfsreq->r_u.nfs);
      pool_free(request_pool, nfsreq);

      if(pmydata->passcounter > nfs_param.worker_param.nb_before_gc)
        {
          /* Garbage collection on dup req cache */
          LogFullDebug(COMPONENT_DISPATCH,
                       "before dupreq invalidation nb_entry=%d nb_invalid=%d",
                       pmydata->duplicate_request->nb_entry,
                       pmydata->duplicate_request->nb_invalid);
          if((rc =
              LRU_invalidate_by_function(pmydata->duplicate_request,
                                         nfs_dupreq_gc_function,
                                         NULL)) != LRU_LIST_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "FAILURE: Impossible to invalidate entries for duplicate "
                      "request cache (error %d)",
                      rc);
            }
          LogFullDebug(COMPONENT_DISPATCH,
                       "after dupreq invalidation nb_entry=%d nb_invalid=%d",
                       pmydata->duplicate_request->nb_entry,
                       pmydata->duplicate_request->nb_invalid);
          if((rc =
              LRU_gc_invalid(pmydata->duplicate_request, NULL)
              != LRU_LIST_SUCCESS))
            LogCrit(COMPONENT_DISPATCH,
                    "FAILURE: Impossible to gc entries for duplicate request "
                    "cache (error %d)",
                    rc);
          else
            LogFullDebug(COMPONENT_DISPATCH,
                         "gc entries for duplicate request cache OK");

          LogFullDebug(COMPONENT_DISPATCH,
                       "after dupreq gc nb_entry=%d nb_invalid=%d",
                       pmydata->duplicate_request->nb_entry,
                       pmydata->duplicate_request->nb_invalid);

          /* Performing garbabbge collection */
          LogFullDebug(COMPONENT_DISPATCH,
                       "Garbage collecting on pending request list");
          pmydata->passcounter = 0;
        }
      else
        LogFullDebug(COMPONENT_DISPATCH,
                     "garbage collection isn't necessary count=%d, max=%d",
                     pmydata->passcounter,
                     nfs_param.worker_param.nb_before_gc);
      pmydata->passcounter += 1;

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

      P(pmydata->wcb.tcb_mutex);
      if(gc_allowed == TRUE && pmydata->wcb.tcb_state == STATE_AWAKE)
        {
          pmydata->gc_in_progress = TRUE;
          V(pmydata->wcb.tcb_mutex);
          LogFullDebug(COMPONENT_DISPATCH,
                       "There are %d concurrent garbage collection",
                       nb_current_gc_workers);

          P(lock_nb_current_gc_workers);
          nb_current_gc_workers -= 1;
          V(lock_nb_current_gc_workers);

          P(pmydata->wcb.tcb_mutex);
          pmydata->gc_in_progress = FALSE;
        }
      V(pmydata->wcb.tcb_mutex);

    }                           /* while( 1 ) */
  tcb_remove(&pmydata->wcb);
  return NULL;
}                               /* worker_thread */
