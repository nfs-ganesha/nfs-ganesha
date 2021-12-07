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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs_proto_functions.h
 * @brief   Prototypes for NFS protocol functions.
 * @note    not called by other header files.
 */

#ifndef NFS_PROTO_FUNCTIONS_H
#define NFS_PROTO_FUNCTIONS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "nfs_core.h"
#include "sal_data.h"
#include "nfs_proto_data.h"

static inline enum nfs_req_result nfsstat4_to_nfs_req_result(nfsstat4 stat)
{
	return stat == NFS4_OK ? NFS_REQ_OK : NFS_REQ_ERROR;
}

void nfs_rpc_complete_async_request(nfs_request_t *reqdata,
				    enum nfs_req_result rc);

enum xprt_stat drc_resume(struct svc_req *req);

#ifdef _USE_NFS3
extern const nfs_function_desc_t nfs3_func_desc[];
#endif
extern const nfs_function_desc_t nfs4_func_desc[];
#ifdef _USE_NFS3
extern const nfs_function_desc_t mnt1_func_desc[];
extern const nfs_function_desc_t mnt3_func_desc[];
#endif
#ifdef _USE_NLM
extern const nfs_function_desc_t nlm4_func_desc[];
#endif
#ifdef _USE_RQUOTA
extern const nfs_function_desc_t rquota1_func_desc[];
extern const nfs_function_desc_t rquota2_func_desc[];
#endif
#ifdef USE_NFSACL3
extern const nfs_function_desc_t nfsacl_func_desc[];
#endif


#ifdef _USE_NFS3
int mnt_Null(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int mnt_Mnt(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int mnt_Dump(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int mnt_Umnt(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int mnt_UmntAll(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int mnt_Export(nfs_arg_t *, struct svc_req *, nfs_res_t *);

/* @}
 * -- End of MNT protocol functions. --
 */
#endif

#ifdef _USE_NLM
int nlm_Null(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Test(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Lock(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Cancel(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unlock(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Sm_Notify(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Test_Message(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Cancel_Message(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Lock_Message(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unlock_Message(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Granted_Res(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Share(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unshare(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nlm4_Free_All(nfs_arg_t *, struct svc_req *, nfs_res_t *);

/* @}
 * -- End of NLM protocol functions. --
 */
#endif

#ifdef _USE_RQUOTA
int rquota_Null(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int rquota_getquota(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int rquota_getactivequota(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int rquota_setquota(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int rquota_setactivequota(nfs_arg_t *, struct svc_req *, nfs_res_t *);

/* @}
 *  * -- End of RQUOTA protocol functions. --
 *  */
#endif

#ifdef USE_NFSACL3
int nfsacl_Null(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfsacl_getacl(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfsacl_setacl(nfs_arg_t *, struct svc_req *, nfs_res_t *);

/* @}
 *  * -- End of NFSACL protocol functions. --
 *  */
#endif

int nfs_null(nfs_arg_t *, struct svc_req *, nfs_res_t *);

#ifdef _USE_NFS3
int nfs3_getattr(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_setattr(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_lookup(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_readlink(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_read(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_write(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_create(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_remove(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_rename(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_link(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_symlink(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_mkdir(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_rmdir(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_readdir(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_fsstat(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_access(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_readdirplus(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_fsinfo(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_pathconf(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_commit(nfs_arg_t *, struct svc_req *, nfs_res_t *);

int nfs3_mknod(nfs_arg_t *, struct svc_req *, nfs_res_t *);
#endif

/* Functions needed for nfs v4 */

int nfs4_Compound(nfs_arg_t *, struct svc_req *, nfs_res_t *);

enum nfs_req_result nfs4_op_read_resume(struct nfs_argop4 *op,
					compound_data_t *data,
					struct nfs_resop4 *resp);

enum nfs_req_result nfs4_op_write_resume(struct nfs_argop4 *op,
					 compound_data_t *data,
					 struct nfs_resop4 *resp);

enum nfs_req_result nfs4_op_read_plus_resume(struct nfs_argop4 *op,
					     compound_data_t *data,
					     struct nfs_resop4 *resp);

enum nfs_req_result nfs4_op_access(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_close(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_commit(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_create(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_delegpurge(struct nfs_argop4 *, compound_data_t *,
				       struct nfs_resop4 *);

enum nfs_req_result nfs4_op_delegreturn(struct nfs_argop4 *, compound_data_t *,
					struct nfs_resop4 *);

enum nfs_req_result nfs4_op_getattr(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_getfh(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_link(struct nfs_argop4 *, compound_data_t *,
				 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_lock(struct nfs_argop4 *, compound_data_t *,
				 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_lockt(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_locku(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_lookup(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_lookupp(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_nverify(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_open(struct nfs_argop4 *, compound_data_t *,
				 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_open_confirm(struct nfs_argop4 *, compound_data_t *,
					 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_open_downgrade(struct nfs_argop4 *,
					   compound_data_t *data,
					   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_openattr(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_putfh(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_putpubfh(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_putrootfh(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

enum nfs_req_result nfs4_op_read(struct nfs_argop4 *, compound_data_t *,
				 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_readdir(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_remove(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_renew(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_rename(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_restorefh(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

enum nfs_req_result nfs4_op_readlink(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_savefh(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_secinfo(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_secinfo_no_name(struct nfs_argop4 *,
					    compound_data_t *,
					    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_setattr(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_setclientid(struct nfs_argop4 *, compound_data_t *,
					struct nfs_resop4 *);

enum nfs_req_result nfs4_op_setclientid_confirm(struct nfs_argop4 *,
						compound_data_t *,
						struct nfs_resop4 *);

enum nfs_req_result nfs4_op_verify(struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_release_lockowner(struct nfs_argop4 *,
					      compound_data_t *,
					      struct nfs_resop4 *);

enum nfs_req_result nfs4_op_illegal(struct nfs_argop4 *, compound_data_t *data,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_notsupp(struct nfs_argop4 *, compound_data_t *data,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_bind_conn(struct nfs_argop4 *op,
				      compound_data_t *data,
				      struct nfs_resop4 *resp);

enum nfs_req_result nfs4_op_exchange_id(struct nfs_argop4 *, compound_data_t *,
					struct nfs_resop4 *);

enum nfs_req_result nfs4_op_create_session(struct nfs_argop4 *,
					   compound_data_t *,
					   struct nfs_resop4 *);

enum nfs_req_result nfs4_op_getdevicelist(struct nfs_argop4 *,
					  compound_data_t *,
					  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_free_stateid(struct nfs_argop4 *, compound_data_t *,
					 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_getdeviceinfo(struct nfs_argop4 *,
					  compound_data_t *,
					  struct nfs_resop4 *);

enum nfs_req_result nfs4_op_destroy_clientid(struct nfs_argop4 *,
					     compound_data_t *,
					     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_destroy_session(struct nfs_argop4 *,
					    compound_data_t *,
					    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_layoutget(struct nfs_argop4 *,
				      compound_data_t *,
				      struct nfs_resop4 *);

enum nfs_req_result nfs4_op_layoutcommit(struct nfs_argop4 *, compound_data_t *,
					 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_layoutreturn(struct nfs_argop4 *, compound_data_t *,
					 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_reclaim_complete(struct nfs_argop4 *,
					     compound_data_t *,
					     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_sequence(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

enum nfs_req_result nfs4_op_set_ssv(struct nfs_argop4 *, compound_data_t *,
				    struct nfs_resop4 *);

enum nfs_req_result nfs4_op_test_stateid(struct nfs_argop4 *, compound_data_t *,
					 struct nfs_resop4 *);

enum nfs_req_result nfs4_op_write(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

/* NFSv4.2 */
enum nfs_req_result nfs4_op_write_same(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_write_same_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_read_plus(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_read_plus_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_allocate(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_allocate_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_deallocate(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_deallocate_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_seek(struct nfs_argop4 *, compound_data_t *,
				 struct nfs_resop4 *);

void nfs4_op_seek_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_io_advise(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_io_advise_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_layouterror(struct nfs_argop4 *, compound_data_t *,
				       struct nfs_resop4 *);

void nfs4_op_layouterror_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_layoutstats(struct nfs_argop4 *, compound_data_t *,
					struct nfs_resop4 *);

void nfs4_op_layoutstats_Free(nfs_resop4 *resp);

/* NFSv4.3 */
enum nfs_req_result nfs4_op_getxattr(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

void nfs4_op_getxattr_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_setxattr(struct nfs_argop4 *, compound_data_t *,
				     struct nfs_resop4 *);

void nfs4_op_setxattr_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_listxattr(struct nfs_argop4 *, compound_data_t *,
				      struct nfs_resop4 *);

void nfs4_op_listxattr_Free(nfs_resop4 *resp);

enum nfs_req_result nfs4_op_removexattr(struct nfs_argop4 *, compound_data_t *,
				       struct nfs_resop4 *);

void nfs4_op_removexattr_Free(nfs_resop4 *resp);

/* @}
 * -- End of NFS protocols functions. --
 */

/* Free functions */
#ifdef _USE_NFS3
void mnt1_Mnt_Free(nfs_res_t *);
void mnt3_Mnt_Free(nfs_res_t *);

void mnt_Dump_Free(nfs_res_t *);
void mnt_Export_Free(nfs_res_t *);
void mnt_Null_Free(nfs_res_t *);
void mnt_Umnt_Free(nfs_res_t *);
void mnt_UmntAll_Free(nfs_res_t *);
#endif

#ifdef _USE_NLM
void nlm_Null_Free(nfs_res_t *);
void nlm4_Test_Free(nfs_res_t *);
void nlm4_Lock_Free(nfs_res_t *);
void nlm4_NM_Lock_Free(nfs_res_t *);
void nlm4_Share_Free(nfs_res_t *);
void nlm4_Unshare_Free(nfs_res_t *);
void nlm4_Cancel_Free(nfs_res_t *);
void nlm4_Unlock_Free(nfs_res_t *);
void nlm4_Sm_Notify_Free(nfs_res_t *);
void nlm4_Granted_Res_Free(nfs_res_t *);
void nlm4_Free_All_Free(nfs_res_t *);
#endif

#ifdef _USE_RQUOTA
void rquota_Null_Free(nfs_res_t *);
void rquota_getquota_Free(nfs_res_t *);
void rquota_getactivequota_Free(nfs_res_t *);
void rquota_setquota_Free(nfs_res_t *);
void rquota_setactivequota_Free(nfs_res_t *);
#endif

#ifdef USE_NFSACL3
void nfsacl_Null_Free(nfs_res_t *);
void nfsacl_getacl_Free(nfs_res_t *);
void nfsacl_setacl_Free(nfs_res_t *);
#endif

void nfs_null_free(nfs_res_t *);

#ifdef _USE_NFS3
void nfs3_getattr_free(nfs_res_t *);
void nfs3_setattr_free(nfs_res_t *);
void nfs3_lookup_free(nfs_res_t *);
void nfs3_access_free(nfs_res_t *);
void nfs3_readlink_free(nfs_res_t *);
void nfs3_write_free(nfs_res_t *);
void nfs3_create_free(nfs_res_t *);
void nfs3_mkdir_free(nfs_res_t *);
void nfs3_symlink_free(nfs_res_t *);
void nfs3_mknod_free(nfs_res_t *);
void nfs3_remove_free(nfs_res_t *);
void nfs3_rmdir_free(nfs_res_t *);
void nfs3_rename_free(nfs_res_t *);
void nfs3_link_free(nfs_res_t *);
void nfs3_readdir_free(nfs_res_t *);
void nfs3_readdirplus_free(nfs_res_t *);
void nfs3_fsstat_free(nfs_res_t *);
void nfs3_fsinfo_free(nfs_res_t *);
void nfs3_pathconf_free(nfs_res_t *);
void nfs3_commit_free(nfs_res_t *);
void nfs3_read_free(nfs_res_t *);
#endif

void nfs4_Compound_FreeOne(nfs_resop4 *);
void release_nfs4_res_compound(struct COMPOUND4res_extended *res_compound4_ex);
void nfs4_Compound_Free(nfs_res_t *);
void nfs4_Compound_CopyResOne(nfs_resop4 *, nfs_resop4 *);

void nfs4_op_access_Free(nfs_resop4 *);
void nfs4_op_close_Free(nfs_resop4 *);
void nfs4_op_commit_Free(nfs_resop4 *);
void nfs4_op_create_Free(nfs_resop4 *);
void nfs4_op_delegreturn_Free(nfs_resop4 *);
void nfs4_op_delegpurge_Free(nfs_resop4 *);
void nfs4_op_getattr_Free(nfs_resop4 *);
void nfs4_op_getfh_Free(nfs_resop4 *);
void nfs4_op_illegal_Free(nfs_resop4 *);
void nfs4_op_notsupp_Free(nfs_resop4 *);
void nfs4_op_link_Free(nfs_resop4 *);
void nfs4_op_lock_Free(nfs_resop4 *);
void nfs4_op_lockt_Free(nfs_resop4 *);
void nfs4_op_locku_Free(nfs_resop4 *);
void nfs4_op_lookup_Free(nfs_resop4 *);
void nfs4_op_lookupp_Free(nfs_resop4 *);
void nfs4_op_nverify_Free(nfs_resop4 *);
void nfs4_op_open_Free(nfs_resop4 *);
void nfs4_op_open_confirm_Free(nfs_resop4 *);
void nfs4_op_open_downgrade_Free(nfs_resop4 *);
void nfs4_op_openattr_Free(nfs_resop4 *);
void nfs4_op_openattr_Free(nfs_resop4 *);
void nfs4_op_putfh_Free(nfs_resop4 *);
void nfs4_op_putpubfh_Free(nfs_resop4 *);
void nfs4_op_putrootfh_Free(nfs_resop4 *);
void nfs4_op_read_Free(nfs_resop4 *);
void nfs4_op_readdir_Free(nfs_resop4 *);
void nfs4_op_readlink_Free(nfs_resop4 *);
void nfs4_op_release_lockowner_Free(nfs_resop4 *);
void nfs4_op_rename_Free(nfs_resop4 *);
void nfs4_op_remove_Free(nfs_resop4 *);
void nfs4_op_renew_Free(nfs_resop4 *);
void nfs4_op_restorefh_Free(nfs_resop4 *);
void nfs4_op_savefh_Free(nfs_resop4 *);
void nfs4_op_secinfo_Free(nfs_resop4 *);
void nfs4_op_secinfo_no_name_Free(nfs_resop4 *);
void nfs4_op_setattr_Free(nfs_resop4 *);
void nfs4_op_setclientid_Free(nfs_resop4 *);
void nfs4_op_setclientid_confirm_Free(nfs_resop4 *);
void nfs4_op_verify_Free(nfs_resop4 *);
void nfs4_op_write_Free(nfs_resop4 *);

void nfs4_op_close_CopyRes(CLOSE4res *, CLOSE4res *);
void nfs4_op_lock_CopyRes(LOCK4res *, LOCK4res *);
void nfs4_op_locku_CopyRes(LOCKU4res *, LOCKU4res *);
void nfs4_op_open_CopyRes(OPEN4res *, OPEN4res *);
void nfs4_op_open_confirm_CopyRes(OPEN_CONFIRM4res *,
				  OPEN_CONFIRM4res *);
void nfs4_op_open_downgrade_CopyRes(OPEN_DOWNGRADE4res *,
				    OPEN_DOWNGRADE4res *);

void nfs4_op_nfs4_op_bind_conn_Free(nfs_resop4 *resp);
void nfs4_op_exchange_id_Free(nfs_resop4 *);
void nfs4_op_close_Free(nfs_resop4 *);
void nfs4_op_create_session_Free(nfs_resop4 *);
void nfs4_op_getdevicelist_Free(nfs_resop4 *);
void nfs4_op_getdeviceinfo_Free(nfs_resop4 *);
void nfs4_op_free_stateid_Free(nfs_resop4 *);
void nfs4_op_destroy_session_Free(nfs_resop4 *);
void nfs4_op_lock_Free(nfs_resop4 *);
void nfs4_op_lockt_Free(nfs_resop4 *);
void nfs4_op_locku_Free(nfs_resop4 *);
void nfs4_op_read_Free(nfs_resop4 *);
void nfs4_op_sequence_Free(nfs_resop4 *);
void nfs4_op_set_ssv_Free(nfs_resop4 *);
void nfs4_op_test_stateid_Free(nfs_resop4 *);
void nfs4_op_write_Free(nfs_resop4 *);
void nfs4_op_destroy_clientid_Free(nfs_resop4 *);
void nfs4_op_reclaim_complete_Free(nfs_resop4 *);

void compound_data_Free(compound_data_t *);
bool xdr_COMPOUND4res_extended(XDR *xdrs, struct COMPOUND4res_extended **objp);

/* Pseudo FS functions */
bool pseudo_mount_export(struct gsh_export *exp);
void create_pseudofs(void);
void pseudo_unmount_export_tree(struct gsh_export *exp);
void prune_pseudofs_subtree(struct gsh_export *exp,
			    uint64_t generation,
			    bool ancestor_is_defunct);

/* Slot functions */
static inline void release_slot(nfs41_session_slot_t *slot)
{
	if (slot->cached_result != NULL) {
		/* Release slot cache reference to the result. */
		release_nfs4_res_compound(slot->cached_result);

		/* And empty the slot cache */
		slot->cached_result = NULL;
	}
}

#endif	/* NFS_PROTO_FUNCTIONS_H */
