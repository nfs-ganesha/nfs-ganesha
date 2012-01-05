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
#include "rpc.h"
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
#include "nfs_tcb.h"
#include "SemN.h"

#ifdef _USE_PNFS
#include "pnfs.h"
#include "pnfs_service.h"
#endif

#if !defined(_NO_BUDDY_SYSTEM) && defined(_DEBUG_MEMLEAKS)
void nfs_debug_debug_label_info();
#endif

extern nfs_worker_data_t *workers_data;

/* These two variables keep state of the thread that gc at this time */
unsigned int nb_current_gc_workers;
pthread_mutex_t lock_nb_current_gc_workers;

const nfs_function_desc_t invalid_funcdesc =
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "invalid_function",
   NOTHING_SPECIAL};

  const nfs_function_desc_t *INVALID_FUNCDESC = &invalid_funcdesc;

/* Static array : all the function pointer per nfs v2 functions */
const nfs_function_desc_t nfs2_func_desc[] = {
  {nfs_Null, nfs_Null_Free, (xdrproc_t) xdr_void, (xdrproc_t) xdr_void, "nfs_Null",
   NOTHING_SPECIAL},
  {nfs_Getattr, nfs_Getattr_Free, (xdrproc_t) xdr_fhandle2, (xdrproc_t) xdr_ATTR2res,
   "nfs_Getattr", NEEDS_CRED | SUPPORTS_GSS},
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
   "nfs_Fsstat", NEEDS_CRED | SUPPORTS_GSS}
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
   (xdrproc_t) xdr_COMPOUND4res, "nfs4_Compound", NEEDS_CRED }
   /* SUPPORTS_GSS is missing from this list because while NFS v4 does indeed support GSS, we won't check it yet */
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
                         nlm4_Test_Message, nlm4_Test_Free,
                         (xdrproc_t) xdr_nlm4_testargs,
                         (xdrproc_t) xdr_void, "nlm4_Test_msg", NEEDS_CRED},
  [NLMPROC4_LOCK_MSG] = {
                         nlm4_Lock_Message, nlm4_Lock_Free,
                         (xdrproc_t) xdr_nlm4_lockargs,
                         (xdrproc_t) xdr_void, "nlm4_Lock_msg", NEEDS_CRED},
  [NLMPROC4_CANCEL_MSG] = {
                           nlm4_Cancel_Message, nlm4_Cancel_Free,
                           (xdrproc_t) xdr_nlm4_cancargs,
                           (xdrproc_t) xdr_void, "nlm4_Cancel_msg", NEEDS_CRED},
  [NLMPROC4_UNLOCK_MSG] = {
                           nlm4_Unlock_Message, nlm4_Unlock_Free,
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
                            nlm4_Granted_Res, nlm4_Granted_Res_Free, (xdrproc_t) xdr_nlm4_res,
                            (xdrproc_t) xdr_void, "nlm4_Granted_res", NEEDS_CRED},
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
                    "nlm4_Share", NEEDS_CRED},
  [NLMPROC4_UNSHARE] = {
                        nlm4_Unsupported, nlm4_Unsupported_Free,
                        (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                        "nlm4_Unshare", NEEDS_CRED},
  [NLMPROC4_NM_LOCK] = {
                        nlm4_Unsupported, nlm4_Unsupported_Free,
                        (xdrproc_t) xdr_void, (xdrproc_t) xdr_void,
                        "nlm4_Nm_lock", NEEDS_CRED},
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

extern const char *pause_state_str[];

/**
 * nfs_Init_gc_counter: Init the worker's gc counters.
 *
 * This functions is used to init a mutex and a counter associated with it, to keep track of the number of worker currently
 * performing the garbagge collection.
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
              LogFullDebug(COMPONENT_DISPATCH,
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
              svcerr_progvers(xprt, lo_vers, hi_vers);  /* Bad NFS version */
            }
          return FALSE;
        }
      else if(((preq->rq_vers == NFS_V2) && (preq->rq_proc > NFSPROC_STATFS)) ||
              ((preq->rq_vers == NFS_V3) && (preq->rq_proc > NFSPROC3_COMMIT)) ||
              ((preq->rq_vers == NFS_V4) && (preq->rq_proc > NFSPROC4_COMPOUND)))
        {
          if(xprt != NULL)
            svcerr_noproc(xprt);
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
      if((preq->rq_vers == MOUNT_V1) && (((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0) ||
         (preq->rq_proc != MOUNTPROC2_MNT)))
        {
          if(preq->rq_proc > MOUNTPROC2_EXPORT)
            {
              if(xprt != NULL)
                svcerr_noproc(xprt);
              return FALSE;
            }
          return TRUE;
        }
      else if((preq->rq_vers == MOUNT_V3) && (((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0) ||
              (preq->rq_proc != MOUNTPROC2_MNT)))
        {
          if(preq->rq_proc > MOUNTPROC3_EXPORT)
            {
              if(xprt != NULL)
                svcerr_noproc(xprt);
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

          LogFullDebug(COMPONENT_DISPATCH,
                       "Invalid Mount Version #%d",
                       (int)preq->rq_vers);
          svcerr_progvers(xprt, lo_vers, hi_vers);
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
          LogFullDebug(COMPONENT_DISPATCH,
                       "Invalid NLM Version #%d",
                       (int)preq->rq_vers);
          if(xprt != NULL)
            svcerr_progvers(xprt, NLM4_VERS, NLM4_VERS);
          return FALSE;
        }
      if(preq->rq_proc > NLMPROC4_FREE_ALL)
        {
          if(xprt != NULL)
            svcerr_noproc(xprt);
          return FALSE;
        }
      return TRUE;
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
   if(preq->rq_prog == nfs_param.core_param.program[P_RQUOTA])
     {
       /* Call is with NLMPROG */
       if((preq->rq_vers != RQUOTAVERS) &&
          (preq->rq_vers != EXT_RQUOTAVERS))
         {
           /* Bad NLM version */
           if(xprt != NULL)
             {
               LogFullDebug(COMPONENT_DISPATCH,
                            "Invalid RQUOTA Version #%d",
                            (int)preq->rq_vers);
               svcerr_progvers(xprt, RQUOTAVERS, EXT_RQUOTAVERS);
             }
           return FALSE;
         }
       if((preq->rq_vers == RQUOTAVERS) && (preq->rq_proc > RQUOTAPROC_SETACTIVEQUOTA) ||
          (preq->rq_vers == EXT_RQUOTAVERS) && (preq->rq_proc > RQUOTAPROC_SETACTIVEQUOTA))
        {
          if(xprt != NULL)
            svcerr_noproc(xprt);
          return FALSE;
        }
      return TRUE;
     }
#endif                          /* _USE_QUOTA */

  /* No such program */
  if(xprt != NULL)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Invalid Program number #%d",
                   (int)preq->rq_prog);
      svcerr_noprog(xprt);        /* This is no NFS, MOUNT program, exit... */
    }
  return FALSE;
}

/*
 * Extract nfs function descriptor from nfs request.
 */
const nfs_function_desc_t *nfs_rpc_get_funcdesc(nfs_request_data_t *preqnfs)
{
  struct svc_req *ptr_req = &preqnfs->req;

  /* Validate rpc call, but don't report any errors here */
  if(is_rpc_call_valid(preqnfs->xprt, ptr_req) == FALSE)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "INVALID_FUNCDESC for Program %d, Version %d, Function %d after is_rpc_call_valid",
                   (int)ptr_req->rq_prog, (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);
      return INVALID_FUNCDESC;
    }

  if(ptr_req->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      if(ptr_req->rq_vers == NFS_V2)
        return &nfs2_func_desc[ptr_req->rq_proc];
      else if(ptr_req->rq_vers == NFS_V3)
        return &nfs3_func_desc[ptr_req->rq_proc];
      else
        return &nfs4_func_desc[ptr_req->rq_proc];
    }

  if(ptr_req->rq_prog == nfs_param.core_param.program[P_MNT])
    {
      if(ptr_req->rq_vers == MOUNT_V1)
        return &mnt1_func_desc[ptr_req->rq_proc];
      else
        return &mnt3_func_desc[ptr_req->rq_proc];
    }

#ifdef _USE_NLM
  if(ptr_req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      return &nlm4_func_desc[ptr_req->rq_proc];
    }
#endif                          /* _USE_NLM */

#ifdef _USE_QUOTA
  if(ptr_req->rq_prog == nfs_param.core_param.program[P_RQUOTA])
    {
      if(ptr_req->rq_vers == RQUOTAVERS)
        return &rquota1_func_desc[ptr_req->rq_proc];
      else
        return &rquota2_func_desc[ptr_req->rq_proc];
    }
#endif                          /* _USE_QUOTA */

  /* Oops, should never get here! */
  svcerr_noprog(preqnfs->xprt);
  LogFullDebug(COMPONENT_DISPATCH,
               "INVALID_FUNCDESC for Program %d, Version %d, Function %d",
               (int)ptr_req->rq_prog, (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);
  return INVALID_FUNCDESC;
}

/*
 * Extract RPC argument.
 */
int nfs_rpc_get_args(nfs_request_data_t * preqnfs, const nfs_function_desc_t *pfuncdesc)
{
  SVCXPRT *ptr_svc = preqnfs->xprt;
  nfs_arg_t *parg_nfs = &preqnfs->arg_nfs;

  memset(parg_nfs, 0, sizeof(nfs_arg_t));

  LogFullDebug(COMPONENT_DISPATCH,
               "Before svc_getargs on socket %d, xprt=%p",
               ptr_svc->XP_SOCK, ptr_svc);

  if(svc_getargs(ptr_svc, pfuncdesc->xdr_decode_func, (caddr_t) parg_nfs) == FALSE)
    {
      struct svc_req *ptr_req = &preqnfs->req;
      LogMajor(COMPONENT_DISPATCH,
                   "svc_getargs failed for Program %d, Version %d, Function %d",
                   (int)ptr_req->rq_prog, (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);
      svcerr_decode(ptr_svc);
      return FALSE;
    }

  return TRUE;
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
  unsigned int export_check_result;

  exportlist_t *pexport = NULL;
  nfs_arg_t *parg_nfs = &preqnfs->arg_nfs;
  nfs_res_t res_nfs;
  short exportid;
  LRU_list_t *lru_dupreq = NULL;
  struct svc_req *ptr_req = &preqnfs->req;
  SVCXPRT *ptr_svc = preqnfs->xprt;
  nfs_stat_type_t stat_type;
  sockaddr_t hostaddr;
  int port;
  int rc;
  int do_dupreq_cache;
  int status;
  exportlist_client_entry_t related_client;
  struct user_cred user_credentials;

  fsal_op_context_t * pfsal_op_ctx = NULL ;

#ifdef _DEBUG_MEMLEAKS
  static int nb_iter_memleaks = 0;
#endif

  struct timeval *timer_start = &pworker_data->timer_start;
  struct timeval timer_end;
  struct timeval timer_diff;
  nfs_request_latency_stat_t latency_stat;

  /* Get the value from the worker data */
  lru_dupreq = pworker_data->duplicate_request;

  /* initializing RPC structure */
  memset(&res_nfs, 0, sizeof(res_nfs));

  /* If we reach this point, there was no dupreq cache hit or no dup req cache was necessary */
  /* Get NFS function descriptor. */
  pworker_data->pfuncdesc = nfs_rpc_get_funcdesc(preqnfs);
  if(pworker_data->pfuncdesc == INVALID_FUNCDESC)
    return;

  if(copy_xprt_addr(&hostaddr, ptr_svc) == 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "copy_xprt_addr failed for Program %d, Version %d, Function %d",
                   (int)ptr_req->rq_prog, (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);
      svcerr_systemerr(ptr_svc);
      return;
    }

  port = get_port(&hostaddr);
  rpcxid = get_rpc_xid(ptr_req);

  if(isDebug(COMPONENT_DISPATCH))
    {
      char addrbuf[SOCK_NAME_MAX];
      sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
      LogDebug(COMPONENT_DISPATCH,
               "Request from %s for Program %d, Version %d, Function %d has xid=%u",
               addrbuf,
               (int)ptr_req->rq_prog, (int)ptr_req->rq_vers, (int)ptr_req->rq_proc,
               rpcxid);
    }

  do_dupreq_cache = pworker_data->pfuncdesc->dispatch_behaviour & CAN_BE_DUP;
  LogFullDebug(COMPONENT_DISPATCH, "do_dupreq_cache = %d", do_dupreq_cache);
  status = nfs_dupreq_add_not_finished(rpcxid,
                                       ptr_req,
                                       preqnfs->xprt,
                                       &pworker_data->dupreq_pool,
                                       &res_nfs);
  switch(status)
    {
      /* a new request, continue processing it */
    case DUPREQ_SUCCESS:
      LogFullDebug(COMPONENT_DISPATCH, "Current request is not duplicate.");
      break;
      /* Found the reuqest in the dupreq cache. It's an old request so resend old reply. */
    case DUPREQ_ALREADY_EXISTS:
      if(do_dupreq_cache)
        {
          /* Request was known, use the previous reply */
          LogFullDebug(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: DupReq Cache Hit: using previous reply, rpcxid=%u",
                       rpcxid);

          LogFullDebug(COMPONENT_DISPATCH,
                       "Before svc_sendreply on socket %d (dup req)",
                       ptr_svc->XP_SOCK);

          P(mutex_cond_xprt[ptr_svc->XP_SOCK]);

          if(svc_sendreply
             (ptr_svc, pworker_data->pfuncdesc->xdr_encode_func, (caddr_t) & res_nfs) == FALSE)
            {
              LogDebug(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply");
              svcerr_systemerr(ptr_svc);
            }

          V(mutex_cond_xprt[ptr_svc->XP_SOCK]);

          LogFullDebug(COMPONENT_DISPATCH,
                       "After svc_sendreply on socket %d (dup req)",
                       ptr_svc->XP_SOCK);
          return;
        }
      else
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Error: Duplicate request rejected because it was found in the cache but is not allowed to be cached.");
          svcerr_systemerr(ptr_svc);
          return;
        }
      break;

      /* Another thread owns the request */
    case DUPREQ_BEING_PROCESSED:
      LogFullDebug(COMPONENT_DISPATCH,
                   "Dupreq xid=%u was asked for process since another thread manage it, reject for avoiding threads starvation...",
                   rpcxid);
      /* Free the arguments */
      if(preqnfs->req.rq_vers == 2 || preqnfs->req.rq_vers == 3 || preqnfs->req.rq_vers == 4)
        if(!SVC_FREEARGS(ptr_svc, pworker_data->pfuncdesc->xdr_decode_func, (caddr_t) parg_nfs))
          {
            LogCrit(COMPONENT_DISPATCH,
                    "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                    pworker_data->pfuncdesc->funcname);
          }
      /* Ignore the request, send no error */
      return;

      /* something is very wrong with the duplicate request cache */
    case DUPREQ_NOT_FOUND:
      LogCrit(COMPONENT_DISPATCH,
              "Did not find the request in the duplicate request cache and couldn't add the request.");
      svcerr_systemerr(ptr_svc);
      return;

      /* oom */
    case DUPREQ_INSERT_MALLOC_ERROR:
      LogCrit(COMPONENT_DISPATCH,
              "Cannot process request, not enough memory available!");
      svcerr_systemerr(ptr_svc);
      return;

    default:
      LogCrit(COMPONENT_DISPATCH,
              "Unknown duplicate request cache status. This should never be reached!");
      svcerr_systemerr(ptr_svc);
      return;
    }

  /* Get the export entry */
  if(ptr_req->rq_prog == nfs_param.core_param.program[P_NFS])
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
              exportid = nfs2_FhandleToExportId((fhandle2 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL ||
                 (pexport->options & EXPORT_OPTION_NFSV2) == 0)
                {
                  /* Reject the request for authentication reason (incompatible file handle) */
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
                        reason = "V2 not allowed on this export";
                      sprint_fhandle2(dumpfh, (fhandle2 *) parg_nfs);
                      LogMajor(COMPONENT_DISPATCH,
                               "NFS2 Request from host %s %s, proc=%d, FH=%s",
                               addrbuf, reason,
                               (int)ptr_req->rq_proc, dumpfh);
                    }
                  /* Bad argument */
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                  if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                        &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                    {
                      LogCrit(COMPONENT_DISPATCH,
                              "Attempt to delete duplicate request failed on line %d",
                              __LINE__);
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
          if(ptr_req->rq_proc != NFSPROC_NULL)
            {
              exportid = nfs3_FhandleToExportId((nfs_fh3 *) parg_nfs);

              if(exportid < 0 ||
                 (pexport = nfs_Get_export_by_id(nfs_param.pexportlist,
                                                 exportid)) == NULL ||
                 (pexport->options & EXPORT_OPTION_NFSV3) == 0)
                {
                  /* Reject the request for authentication reason (incompatible file handle) */
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
                      sprint_fhandle3(dumpfh, (nfs_fh3 *) parg_nfs);
                      LogMajor(COMPONENT_DISPATCH,
                               "NFS3 Request from host %s %s, proc=%d, FH=%s",
                               addrbuf, reason,
                               (int)ptr_req->rq_proc, dumpfh);
                    }
                  /* Bad argument */
                  svcerr_auth(ptr_svc, AUTH_FAILED);
                  if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                        &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                    {
                      LogCrit(COMPONENT_DISPATCH,
                              "Attempt to delete duplicate request failed on line %d",
                              __LINE__);
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
  else if(ptr_req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      netobj *pfh3 = NULL;

      switch(ptr_req->rq_proc)
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
            /* Not supported... */
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
              /* Reject the request for authentication reason (incompatible file handle) */
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
                           (int)ptr_req->rq_proc, dumpfh);
                }
              /* Bad argument */
              svcerr_auth(ptr_svc, AUTH_FAILED);
              if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                    &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                {
                  LogCrit(COMPONENT_DISPATCH,
                          "Attempt to delete duplicate request failed on line %d",
                          __LINE__);
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
      switch (ptr_req->rq_cred.oa_flavor)
        {
          case AUTH_NONE:
            if((pexport->options & EXPORT_OPTION_AUTH_NONE) == 0)
              {
                LogInfo(COMPONENT_DISPATCH,
                        "Export %s does not support AUTH_NONE",
                        pexport->dirname);
                svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                      &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                  {
                    LogCrit(COMPONENT_DISPATCH,
                            "Attempt to delete duplicate request failed on line %d",
                            __LINE__);
                  }
                return;
              }
            break;

          case AUTH_UNIX:
            if((pexport->options & EXPORT_OPTION_AUTH_UNIX) == 0)
              {
                LogInfo(COMPONENT_DISPATCH,
                        "Export %s does not support AUTH_UNIX",
                        pexport->dirname);
                svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                      &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                  {
                    LogCrit(COMPONENT_DISPATCH,
                            "Attempt to delete duplicate request failed on line %d",
                            __LINE__);
                  }
                return;
              }
            break;

#ifdef _HAVE_GSSAPI
          case RPCSEC_GSS:
            if((pexport->options &
                 (EXPORT_OPTION_RPCSEC_GSS_NONE |
                  EXPORT_OPTION_RPCSEC_GSS_INTG |
                  EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0)
              {
                LogInfo(COMPONENT_DISPATCH,
                        "Export %s does not support RPCSEC_GSS",
                        pexport->dirname);
                if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                      &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                  {
                    LogCrit(COMPONENT_DISPATCH,
                            "Attempt to delete duplicate request failed on line %d",
                            __LINE__);
                  }
                return;
                svcerr_auth(ptr_svc, AUTH_TOOWEAK);
              }
            else
              {
                struct svc_rpc_gss_data *gd;
                rpc_gss_svc_t svc;
                gd = SVCAUTH_PRIVATE(ptr_req->rq_xprt->xp_auth);
                svc = gd->sec.svc;
                LogFullDebug(COMPONENT_DISPATCH,
                             "Testing svc %d", (int) svc);
                switch(svc)
                  {
                    case RPCSEC_GSS_SVC_NONE:
                      if((pexport->options & EXPORT_OPTION_RPCSEC_GSS_NONE) == 0)
                        {
                          LogInfo(COMPONENT_DISPATCH,
                                  "Export %s does not support RPCSEC_GSS_SVC_NONE",
                                  pexport->dirname);
                          svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                            {
                              LogCrit(COMPONENT_DISPATCH,
                                      "Attempt to delete duplicate request failed on line %d",
                                      __LINE__);
                            }
                          return;
                        }
                      break;

                    case RPCSEC_GSS_SVC_INTEGRITY:
                      if((pexport->options & EXPORT_OPTION_RPCSEC_GSS_INTG) == 0)
                        {
                          LogInfo(COMPONENT_DISPATCH,
                                  "Export %s does not support RPCSEC_GSS_SVC_INTEGRITY",
                                  pexport->dirname);
                          svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                            {
                              LogCrit(COMPONENT_DISPATCH,
                                      "Attempt to delete duplicate request failed on line %d",
                                      __LINE__);
                            }
                          return;
                        }
                      break;

                    case RPCSEC_GSS_SVC_PRIVACY:
                      if((pexport->options & EXPORT_OPTION_RPCSEC_GSS_PRIV) == 0)
                        {
                          LogInfo(COMPONENT_DISPATCH,
                                  "Export %s does not support RPCSEC_GSS_SVC_PRIVACY",
                                  pexport->dirname);
                          svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                            {
                              LogCrit(COMPONENT_DISPATCH,
                                      "Attempt to delete duplicate request failed on line %d",
                                      __LINE__);
                            }
                          return;
                        }
                      break;

                    default:
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support unknown RPCSEC_GSS_SVC %d",
                              pexport->dirname, (int) svc);
                      svcerr_auth(ptr_svc, AUTH_TOOWEAK);
                      if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                            &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                        {
                          LogCrit(COMPONENT_DISPATCH,
                                  "Attempt to delete duplicate request failed on line %d",
                                  __LINE__);
                        }
                      return;
                  }
              }
            break;
#endif
          default:
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support unknown oa_flavor %d",
                    pexport->dirname, (int) ptr_req->rq_cred.oa_flavor);
            svcerr_auth(ptr_svc, AUTH_TOOWEAK);
            if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                  &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
              {
                LogCrit(COMPONENT_DISPATCH,
                        "Attempt to delete duplicate request failed on line %d",
                        __LINE__);
              }
            return;
        }
    }

  /* Zero out timers prior to starting processing */
  memset(timer_start, 0, sizeof(struct timeval));
  memset(&timer_end, 0, sizeof(struct timeval));
  memset(&timer_diff, 0, sizeof(struct timeval));

  /*
   * It is now time for checking if export list allows the machine to perform the request
   */
  pworker_data->hostaddr = hostaddr;

  /* Check if client is using a privileged port, but only for NFS protocol */
  if(ptr_req->rq_prog == nfs_param.core_param.program[P_NFS] && ptr_req->rq_proc != 0)
    {
      if((pexport->options & EXPORT_OPTION_PRIVILEGED_PORT) &&
         (port >= IPPORT_RESERVED))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Port %d is too high for this export entry, rejecting client",
                  port);
          svcerr_auth(ptr_svc, AUTH_TOOWEAK);
          pworker_data->current_xid = 0;    /* No more xid managed */

          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          return;
        }
    }

  if(pworker_data->pfuncdesc->dispatch_behaviour & NEEDS_CRED)
    {
      if(get_req_uid_gid(ptr_req, pexport, &user_credentials) == FALSE)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "could not get uid and gid, rejecting client");
          svcerr_auth(ptr_svc, AUTH_TOOWEAK);
          pworker_data->current_xid = 0;    /* No more xid managed */

          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          return;
        }
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "nfs_rpc_execute about to call nfs_export_check_access");
  export_check_result = nfs_export_check_access(&pworker_data->hostaddr,
                                                ptr_req,
                                                pexport,
                                                nfs_param.core_param.program[P_NFS],
                                                nfs_param.core_param.program[P_MNT],
                                                pworker_data->ht_ip_stats,
                                                &pworker_data->ip_stats_pool,
                                                &related_client,
                                                &user_credentials,
                                                (pworker_data->pfuncdesc->dispatch_behaviour & MAKES_WRITE) == MAKES_WRITE);
  if (export_check_result == EXPORT_PERMISSION_DENIED)
    {
      char addrbuf[SOCK_NAME_MAX];
      sprint_sockaddr(&hostaddr, addrbuf, sizeof(addrbuf));
      LogInfo(COMPONENT_DISPATCH,
              "Host %s is not allowed to access this export entry, vers=%d, proc=%d",
              addrbuf,
              (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);
      svcerr_auth( ptr_svc, AUTH_TOOWEAK );
      pworker_data->current_xid = 0;        /* No more xid managed */

      if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                            &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
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
      if(ptr_req->rq_prog == nfs_param.core_param.program[P_NFS])
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
#ifdef _USE_SHARED_FSAL
	  FSAL_SetId( pexport->fsalid ) ;

	  /* Swap the anonymous uid/gid if the user should be anonymous */
          if(nfs_check_anon(&related_client, pexport, &user_credentials) == FALSE
	     || nfs_build_fsal_context(ptr_req,
                                       pexport,
				       &pworker_data->thread_fsal_context[pexport->fsalid],
				       &user_credentials) == FALSE)
#else
	  /* Swap the anonymous uid/gid if the user should be anonymous */
          if(nfs_check_anon(&related_client, pexport, &user_credentials) == FALSE
	     || nfs_build_fsal_context(ptr_req,
                                       pexport,
				       &pworker_data->thread_fsal_context,
                                       &user_credentials) == FALSE)
#endif
            {
              LogInfo(COMPONENT_DISPATCH,
                      "authentication failed, rejecting client");
              svcerr_auth(ptr_svc, AUTH_TOOWEAK);
              pworker_data->current_xid = 0;    /* No more xid managed */

              if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                    &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
                {
                  LogCrit(COMPONENT_DISPATCH,
                         "Attempt to delete duplicate request failed on line %d",
                         __LINE__);
                }
              return;
            }
        }

      /* processing */
      gettimeofday(timer_start, NULL);

      LogDebug(COMPONENT_DISPATCH,
               "NFS DISPATCHER: Calling service function %s start_time %llu.%.6llu",
               pworker_data->pfuncdesc->funcname,
               (unsigned long long)timer_start->tv_sec,
               (unsigned long long)timer_start->tv_usec);


#ifdef _ERROR_INJECTION
      if(worker_delay_time != 0)
        sleep(worker_delay_time);
      else if(next_worker_delay_time != 0)
        {
          sleep(next_worker_delay_time);
          next_worker_delay_time = 0;
        }
#endif

#ifdef _USE_SHARED_FSAL
      if( pexport != NULL )
       {
         pfsal_op_ctx = &pworker_data->thread_fsal_context[pexport->fsalid] ; 
         FSAL_SetId( pexport->fsalid ) ;
       }
      else
	pfsal_op_ctx = NULL ; /* Only for mount protocol (pexport is then meaningless */
#else
      pfsal_op_ctx =  &pworker_data->thread_fsal_context ;
#endif

      rc = pworker_data->pfuncdesc->service_function(parg_nfs, 
						     pexport, 
						     pfsal_op_ctx,
                                                     &(pworker_data->cache_inode_client), 
                                                     pworker_data->ht, 
                                                     ptr_req, 
                                                     &res_nfs); 

      gettimeofday(&timer_end, NULL);
      timer_diff = time_diff(*timer_start, timer_end);
      memset(timer_start, 0, sizeof(struct timeval));

      if(timer_diff.tv_sec >= nfs_param.core_param.long_processing_threshold)
        LogEvent(COMPONENT_DISPATCH,
                 "Function %s xid=%u exited with status %d taking %llu.%.6llu seconds to process",
                 pworker_data->pfuncdesc->funcname, rpcxid, rc,
                 (unsigned long long)timer_diff.tv_sec,
                 (unsigned long long)timer_diff.tv_usec);
      else
        LogDebug(COMPONENT_DISPATCH,
                 "Function %s xid=%u exited with status %d taking %llu.%.6llu seconds to process",
                 pworker_data->pfuncdesc->funcname, rpcxid, rc,
                 (unsigned long long)timer_diff.tv_sec,
                 (unsigned long long)timer_diff.tv_usec);
    }

  /* Perform statistics here */
  stat_type = (rc == NFS_REQ_OK) ? GANESHA_STAT_SUCCESS : GANESHA_STAT_DROP;

  latency_stat.type = SVC_TIME;
  latency_stat.latency = timer_diff.tv_sec * 1000000 + timer_diff.tv_usec; /* microseconds */

  nfs_stat_update(stat_type, &(pworker_data->stats.stat_req), ptr_req, &latency_stat);
  pworker_data->stats.nb_total_req += 1;

  /* Perform NFSv4 operations statistics if required */
  if(ptr_req->rq_vers == NFS_V4)
    if(ptr_req->rq_proc == NFSPROC4_COMPOUND)
      nfs4_op_stat_update(parg_nfs, &res_nfs, &(pworker_data->stats.stat_req));

  pworker_data->current_xid = 0;        /* No more xid managed */

  /* If request is dropped, no return to the client */
  if(rc == NFS_REQ_DROP)
    {
      /* The request was dropped */
      LogDebug(COMPONENT_DISPATCH,
               "Drop request rpc_xid=%u, program %u, version %u, function %u",
               rpcxid, (int)ptr_req->rq_prog,
               (int)ptr_req->rq_vers, (int)ptr_req->rq_proc);

      /* If the request is not normally cached, then the entry will be removed
       * later. We only remove a reply that is normally cached that has been
       * dropped. */
      if(do_dupreq_cache)
        if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                              &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
          {
            LogCrit(COMPONENT_DISPATCH,
                    "Attempt to delete duplicate request failed on line %d",
                    __LINE__);
          }
    }
  else
    {
      P(mutex_cond_xprt[ptr_svc->XP_SOCK]);

      LogFullDebug(COMPONENT_DISPATCH,
                   "Before svc_sendreply on socket %d",
                   ptr_svc->XP_SOCK);

      /* encoding the result on xdr output */
      CheckXprt(ptr_svc);
      if(svc_sendreply(ptr_svc, pworker_data->pfuncdesc->xdr_encode_func, (caddr_t) & res_nfs) == FALSE)
        {
          LogDebug(COMPONENT_DISPATCH,
                   "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply");
          svcerr_systemerr(ptr_svc);

          V(mutex_cond_xprt[ptr_svc->XP_SOCK]);

          if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                                &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Attempt to delete duplicate request failed on line %d",
                      __LINE__);
            }
          return;
        }

      LogFullDebug(COMPONENT_DISPATCH,
                   "After svc_sendreply on socket %d",
                   ptr_svc->XP_SOCK);

      V(mutex_cond_xprt[ptr_svc->XP_SOCK]);

      /* Mark request as finished */
      LogFullDebug(COMPONENT_DUPREQ, "YES?: %d", do_dupreq_cache);
      if(do_dupreq_cache)
        {
          status = nfs_dupreq_finish(rpcxid,
                                     ptr_req,
                                     preqnfs->xprt,
                                     &res_nfs,
                                     lru_dupreq);
        }
    } /* rc == NFS_REQ_DROP */

  /* Free the allocated resources once the work is done */
  /* Free the arguments */
  if(preqnfs->req.rq_vers == 2 || preqnfs->req.rq_vers == 3 || preqnfs->req.rq_vers == 4)
    if(!SVC_FREEARGS(ptr_svc, pworker_data->pfuncdesc->xdr_decode_func, (caddr_t) parg_nfs))
      {
        LogCrit(COMPONENT_DISPATCH,
                "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                pworker_data->pfuncdesc->funcname);
      }

  /* Free the reply.
   * This should not be done if the request is dupreq cached because this will
   * mark the dupreq cached info eligible for being reuse by other requests */
  if(!do_dupreq_cache)
    {
      if (nfs_dupreq_delete(rpcxid, ptr_req, preqnfs->xprt,
                            &pworker_data->dupreq_pool) != DUPREQ_SUCCESS)
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
#ifdef _DEBUG_MEMLEAKS
  if(nb_iter_memleaks > 1000)
    {
      nb_iter_memleaks = 0;

#ifndef _NO_BUDDY_SYSTEM
      /* BuddyDumpMem( stdout ) ; */
      nfs_debug_debug_label_info();
#endif

      LogFullDebug(COMPONENT_MEMLEAKS,
                   "Stats for thread: total mnt1=%u mnt3=%u nfsv2=%u nfsv3=%u nfsv4=%u",
                   pworker_data->stats.stat_req.nb_mnt1_req,
                   pworker_data->stats.stat_req.nb_mnt3_req,
                   pworker_data->stats.stat_req.nb_nfs2_req,
                   pworker_data->stats.stat_req.nb_nfs3_req,
                   pworker_data->stats.stat_req.nb_nfs4_req);

    }
  else
    nb_iter_memleaks += 1;
#endif

  /* By now the dupreq cache entry should have been completed w/ a request that is reusable
   * or the dupreq cache entry should have been removed. */
  return;
}                               /* nfs_rpc_execute */

worker_available_rc worker_available(unsigned long worker_index, unsigned int avg_number_pending)
{
  worker_available_rc rc = WORKER_AVAILABLE;
  P(workers_data[worker_index].wcb.tcb_mutex);
  switch(workers_data[worker_index].wcb.tcb_state)
    {
      case STATE_AWAKE:
      case STATE_AWAKEN:
        /* Choose only fully initialized workers and that does not gc. */
        if(workers_data[worker_index].wcb.tcb_ready == FALSE)
          {
            LogFullDebug(COMPONENT_THREAD,
                         "worker thread #%lu is not ready", worker_index);
            rc = WORKER_PAUSED;
          }
        else if(workers_data[worker_index].gc_in_progress == TRUE)
          {
            LogFullDebug(COMPONENT_THREAD,
                         "worker thread #%lu is doing garbage collection", worker_index);
            rc = WORKER_GC;
          }
        else if(workers_data[worker_index].pending_request->nb_entry >= avg_number_pending)
          {
            rc = WORKER_BUSY;
          }
        break;

      case STATE_STARTUP:
      case STATE_PAUSE:
      case STATE_PAUSED:
        rc = WORKER_ALL_PAUSED;
        break;

      case STATE_EXIT:
        rc = WORKER_EXIT;
        break;
    }
  V(workers_data[worker_index].wcb.tcb_mutex);

  return rc;
}

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

  sprintf(name, "Worker Thread #%u Pending Request", pdata->worker_index);
  nfs_param.worker_param.lru_param.name = Str_Dup(name);

  if((pdata->pending_request =
      LRU_Init(nfs_param.worker_param.lru_param, &status)) == NULL)
    {
      LogError(COMPONENT_DISPATCH, ERR_LRU, ERR_LRU_LIST_INIT, status);
      return -1;
    }

  sprintf(name, "Worker Thread #%u Duplicate Request", pdata->worker_index);
  nfs_param.worker_param.lru_dupreq.name = Str_Dup(name);

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

void DispatchWorkNFS(request_data_t *pnfsreq, unsigned int worker_index)
{
  LRU_entry_t *pentry = NULL;
  LRU_status_t status;
  struct svc_req *ptr_req = &pnfsreq->rcontent.nfs.req;
  unsigned int rpcxid = get_rpc_xid(ptr_req);

  LogDebug(COMPONENT_DISPATCH,
           "Awaking Worker Thread #%u for request %p, xid=%u",
           worker_index, pnfsreq, rpcxid);

  P(workers_data[worker_index].wcb.tcb_mutex);
  P(workers_data[worker_index].request_pool_mutex);

  pentry = LRU_new_entry(workers_data[worker_index].pending_request, &status);

  if(pentry == NULL)
    {
      V(workers_data[worker_index].request_pool_mutex);
      V(workers_data[worker_index].wcb.tcb_mutex);
      LogMajor(COMPONENT_DISPATCH,
               "Error while inserting pending request to Worker Thread #%u... Exiting",
               worker_index);
      Fatal();
    }

  pentry->buffdata.pdata = (caddr_t) pnfsreq;
  pentry->buffdata.len = sizeof(*pnfsreq);

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

enum auth_stat AuthenticateRequest(nfs_request_data_t *pnfsreq,
                                   bool_t *no_dispatch)
{
  struct rpc_msg *pmsg;
  struct svc_req *preq;
  SVCXPRT *xprt;
  enum auth_stat why;

  /* A few words of explanation are required here:
   * In authentication is AUTH_NONE or AUTH_UNIX, then the value of no_dispatch remains FALSE and the request is proceeded normally
   * If authentication is RPCSEC_GSS, no_dispatch may have value TRUE, this means that gc->gc_proc != RPCSEC_GSS_DATA and that the
   * message is in fact an internal negociation message from RPCSEC_GSS using GSSAPI. It then should not be proceed by the worker and
   * SVC_STAT should be returned to the dispatcher.
   */

  *no_dispatch = FALSE;

  /* Set pointers */
  pmsg = &(pnfsreq->msg);
  preq = &(pnfsreq->req);
  xprt = pnfsreq->xprt;

  preq->rq_xprt = pnfsreq->xprt;
  preq->rq_prog = pmsg->rm_call.cb_prog;
  preq->rq_vers = pmsg->rm_call.cb_vers;
  preq->rq_proc = pmsg->rm_call.cb_proc;
  LogFullDebug(COMPONENT_DISPATCH,
               "About to authenticate Prog = %d, vers = %d, proc = %d xprt=%p",
               (int)preq->rq_prog, (int)preq->rq_vers,
               (int)preq->rq_proc, preq->rq_xprt);
  /* Restore previously save GssData */
#ifdef _HAVE_GSSAPI
  if((why = Rpcsecgss__authenticate(preq, pmsg, no_dispatch)) != AUTH_OK)
#else
  if((why = _authenticate(preq, pmsg)) != AUTH_OK)
#endif
    {
      char auth_str[AUTH_STR_LEN];
      auth_stat2str(why, auth_str);
      LogInfo(COMPONENT_DISPATCH,
              "Could not authenticate request... rejecting with AUTH_STAT=%s",
              auth_str);
      svcerr_auth(xprt, why);
      *no_dispatch = TRUE;
      return why;
    }
  else
    {
#ifdef _HAVE_GSSAPI
      struct rpc_gss_cred *gc;

      if(preq->rq_xprt->xp_verf.oa_flavor == RPCSEC_GSS)
        {
          gc = (struct rpc_gss_cred *)preq->rq_clntcred;
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
 * @param pnfsreq      [INOUT] pointer to 9p request
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
  request_data_t *pnfsreq;
  LRU_entry_t *pentry;
  struct svc_req *preq;
  unsigned long worker_index;
  bool_t found = FALSE;
  int rc = 0;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  unsigned int gc_allowed = FALSE;
  char thr_name[32];

#ifdef _USE_MFSL
  fsal_status_t fsal_status ;
#endif

  worker_index = (unsigned long)IndexArg;
  pmydata = &(workers_data[worker_index]);
#ifdef _USE_SHARED_FSAL 
  unsigned int i = 0 ;
  unsigned int fsalid = 0 ;
#endif

  snprintf(thr_name, sizeof(thr_name), "Worker Thread #%lu", worker_index);
  SetNameFunction(thr_name);

  if(mark_thread_existing(&(pmydata->wcb)) == PAUSE_EXIT)
    {
      /* Oops, that didn't last long... exit. */
      mark_thread_done(&(pmydata->wcb));
      LogDebug(COMPONENT_DISPATCH,
               "Worker exiting before initialization");
      return NULL;
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Starting, nb_entry=%d",
               pmydata->pending_request->nb_entry);
  /* Initialisation of the Buddy Malloc */
#ifndef _NO_BUDDY_SYSTEM
  if((rc = BuddyInit(&nfs_param.buddy_param_worker)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH,
               "Memory manager could not be initialized");
    }
  LogFullDebug(COMPONENT_DISPATCH,
               "Memory manager successfully initialized");
#endif

  LogDebug(COMPONENT_DISPATCH, "NFS WORKER #%lu: my pthread id is %p",
           worker_index, (caddr_t) pthread_self());

  /* Initialisation of credential for current thread */
  LogFullDebug(COMPONENT_DISPATCH,
               "NFS WORKER #%lu: Initialization of thread's credential",
               worker_index);

#ifdef _USE_SHARED_FSAL
  for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
   {
      fsalid =  nfs_param.loaded_fsal[i] ;

      FSAL_SetId( fsalid ) ;

      if(FSAL_IS_ERROR(FSAL_InitClientContext(&(pmydata->thread_fsal_context[fsalid]))))
       {
         /* Failed init */
         LogMajor(COMPONENT_DISPATCH,
                  "NFS  WORKER #%lu: Error initializing thread's credential for FSAL %s",
                 worker_index, FSAL_fsalid2name( fsalid ) );
         exit(1);
       }
   } /* for */
#else
  if(FSAL_IS_ERROR(FSAL_InitClientContext(&pmydata->thread_fsal_context)))
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH,
               "Error initializing thread's credential");
    }
#endif /* _USE_SHARED_FSAL */

  /* Init the Cache inode client for this worker */
  if(cache_inode_client_init(&pmydata->cache_inode_client,
                             nfs_param.cache_layers_param.cache_inode_client_param,
                             worker_index, pmydata))
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH,
               "Cache Inode client could not be initialized");
    }
  LogFullDebug(COMPONENT_DISPATCH,
               "Cache Inode client successfully initialized");

#ifdef _USE_MFSL

#ifdef _USE_SHARED_FSAL
#error "For the moment, no MFSL are supported with dynamic FSALs"
#else
  if(FSAL_IS_ERROR(MFSL_GetContext(&pmydata->cache_inode_client.mfsl_context, (&(pmydata->thread_fsal_context) ) ) ) ) 
#endif
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH, "Error initing MFSL");
    }
#endif

  /* Init the Cache content client for this worker */
  if(cache_content_client_init(&pmydata->cache_content_client,
                               nfs_param.cache_layers_param.cache_content_client_param,
                               thr_name))
    {
      /* Failed init */
      LogFatal(COMPONENT_DISPATCH,
               "Cache Content client could not be initialized");
    }
  LogFullDebug(COMPONENT_DISPATCH,
               "Cache Content client successfully initialized");

  /* _USE_PNFS */

  /* Bind the data cache client to the inode cache client */
  pmydata->cache_inode_client.pcontent_client = (caddr_t) & pmydata->cache_content_client;

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

#ifndef _NO_BUDDY_SYSTEM
          BuddyGetStats(&pmydata->stats.buddy_stats);
#endif
          /* reset last stat */
          pmydata->stats.last_stat_update = time(NULL);
        }

      /* Wait on condition variable for work to be done */
      LogFullDebug(COMPONENT_DISPATCH,
                   "waiting for requests to process, nb_entry=%d, nb_invalid=%d",
                   pmydata->pending_request->nb_entry,
                   pmydata->pending_request->nb_invalid);

      /* Get the state without lock first, if things are fine
       * don't bother to check under lock.
       */
      if((pmydata->wcb.tcb_state != STATE_AWAKE) ||
          (pmydata->pending_request->nb_entry ==
           pmydata->pending_request->nb_invalid))
        {
          while(1)
            {
              P(pmydata->wcb.tcb_mutex);
              if(pmydata->wcb.tcb_state == STATE_AWAKE &&
                 (pmydata->pending_request->nb_entry !=
                  pmydata->pending_request->nb_invalid))
                {
                  V(pmydata->wcb.tcb_mutex);
                  break;
                }
              switch(thread_sm_locked(&pmydata->wcb))
                {
                  case THREAD_SM_RECHECK:
                    V(pmydata->wcb.tcb_mutex);
                    continue;

                  case THREAD_SM_BREAK:
                    if(pmydata->pending_request->nb_entry ==
                        pmydata->pending_request->nb_invalid)
                      {
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
                   "Processing a new request, pause_state: %s, nb_entry=%u, nb_invalid=%u",
                   pause_state_str[pmydata->wcb.tcb_state],
                   pmydata->pending_request->nb_entry,
                   pmydata->pending_request->nb_invalid);

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
          LogMajor(COMPONENT_DISPATCH,
                   "No pending request available");
          continue;             /* return to main loop */
        }

      pnfsreq = (request_data_t *) (pentry->buffdata.pdata);

     
      switch( pnfsreq->rtype )
       {
          case NFS_REQUEST:
           LogFullDebug(COMPONENT_DISPATCH,
                        "I have some work to do, pnfsreq=%p, length=%d, invalid=%d, xid=%lu",
                        pnfsreq,
                        pmydata->pending_request->nb_entry,
                        pmydata->pending_request->nb_invalid,
                        (unsigned long) pnfsreq->rcontent.nfs.msg.rm_xid);

           if(pnfsreq->rcontent.nfs.xprt->XP_SOCK == 0)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "No RPC management, xp_sock==0");
            }
           else
            {
              /* Set pointers */
              preq = &(pnfsreq->rcontent.nfs.req);

              /* Validate the rpc request as being a valid program, version, and proc. If not, report the error.
               * Otherwise, execute the funtion.
               */
              LogFullDebug(COMPONENT_DISPATCH,
                           "About to execute Prog = %d, vers = %d, proc = %d xprt=%p",
                           (int)preq->rq_prog, (int)preq->rq_vers,
                           (int)preq->rq_proc, preq->rq_xprt);

              if(is_rpc_call_valid(preq->rq_xprt, preq) == TRUE)
                  nfs_rpc_execute(&pnfsreq->rcontent.nfs, pmydata);
            }
           break ;

	  case _9P_REQUEST:
#ifdef _USE_9P
	     _9p_execute( &pnfsreq->rcontent._9p, pmydata ) ;
#else
	     LogCrit(COMPONENT_DISPATCH, "Implementation error, 9P message when 9P support is disabled" ) ; 
#endif
	    break ;
         }

      /* Free the req by releasing the entry */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Invalidating processed entry");
      P(pmydata->request_pool_mutex);
      if(LRU_invalidate(pmydata->pending_request, pentry) != LRU_LIST_SUCCESS)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Incoherency: released entry for dispatch could not be tagged invalid");
        }
      V(pmydata->request_pool_mutex);

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
                      "FAILURE: Impossible to invalidate entries for duplicate request cache (error %d)",
                      rc);
            }
          LogFullDebug(COMPONENT_DISPATCH,
                       "after dupreq invalidation nb_entry=%d nb_invalid=%d",
                       pmydata->duplicate_request->nb_entry,
                       pmydata->duplicate_request->nb_invalid);
          if((rc =
              LRU_gc_invalid(pmydata->duplicate_request,
                             (void *)&pmydata->dupreq_pool)) != LRU_LIST_SUCCESS)
            LogCrit(COMPONENT_DISPATCH,
                    "FAILURE: Impossible to gc entries for duplicate request cache (error %d)",
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

          P(pmydata->request_pool_mutex);

          if(LRU_gc_invalid(pmydata->pending_request, (void *)&pmydata->request_pool) !=
             LRU_LIST_SUCCESS)
            LogCrit(COMPONENT_DISPATCH,
                    "ERROR: Impossible garbage collection on pending request list");
          else
            LogFullDebug(COMPONENT_DISPATCH,
                         "Garbage collection on pending request list OK");

          V(pmydata->request_pool_mutex);

        }
      else
        LogFullDebug(COMPONENT_DISPATCH,
                     "garbage collection isn't necessary count=%d, max=%d",
                     pmydata->passcounter, nfs_param.worker_param.nb_before_gc);
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

          P(pmydata->wcb.tcb_mutex);
          pmydata->gc_in_progress = FALSE;
        }
#ifdef _USE_MFSL
      /* As MFSL context are refresh, and because this could be a time consuming operation, the worker is
       * set as "making garbagge collection" to avoid new requests to come in its pending queue */
      if(pmydata->wcb.tcb_state == STATE_AWAKE)
        {
          pmydata->gc_in_progress = TRUE;

      fsal_status = MFSL_RefreshContext(&pmydata->cache_inode_client.mfsl_context,
#ifdef _USE_SHARED_FSAL
                                        &pmydata->thread_fsal_context[pexport->fsalid]);
#else
                                        &pmydata->thread_fsal_context);
#endif

          if(FSAL_IS_ERROR(fsal_status))
            {
              /* Failed init */
              V(pmydata->wcb.tcb_mutex);
              LogFatal(COMPONENT_DISPATCH, "Error regreshing MFSL context");
            }

          pmydata->gc_in_progress = FALSE;
        }

#endif
      V(pmydata->wcb.tcb_mutex);

    }                           /* while( 1 ) */
  tcb_remove(&pmydata->wcb);
  return NULL;
}                               /* worker_thread */
