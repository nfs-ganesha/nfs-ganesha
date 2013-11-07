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
 */

#ifndef NFS_PROTO_FUNCTIONS_H
#define NFS_PROTO_FUNCTIONS_H

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
#include "err_hashtable.h"

#define  NFS4_ATTRVALS_BUFFLEN  1024

/* ------------------------------ Typedefs and structs----------------------- */

typedef union nfs_arg__ {
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

struct COMPOUND4res_extended {
	COMPOUND4res res_compound4;
	bool res_cached;
};

typedef union nfs_res__ {
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
	nlm4_testres res_nlm4test;
	nlm4_res res_nlm4;
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

/* flags related to the behaviour of the requests (to be stored in the dispatch
 * behaviour field)
 */
#define NOTHING_SPECIAL 0x0000	/* Nothing to be done for this kind of
				   request */
#define MAKES_WRITE	0x0001	/* The function modifyes the FSAL (not
				   permitted for RO FS) */
#define NEEDS_CRED	0x0002	/* A credential is needed for this
				   operation */
#define CAN_BE_DUP	0x0004	/* Handling of dup request can be done
				   for this request */
#define SUPPORTS_GSS	0x0008	/* Request may be authenticated by
				   RPCSEC_GSS */
#define MAKES_IO	0x0010	/* Request may do I/O (not allowed on
				   MD ONLY exports */
#define NEEDS_EXPORT	0x0020	/* Request needs an export */

typedef int (*nfs_protocol_function_t) (nfs_arg_t *, exportlist_t *,
					struct req_op_context *req_ctx,
					nfs_worker_data_t *, struct svc_req *,
					nfs_res_t *);

typedef int (*nfsremote_protocol_function_t) (CLIENT *, nfs_arg_t *,
					      nfs_res_t *);

typedef void (*nfs_protocol_free_t) (nfs_res_t *);

typedef struct nfs_function_desc__ {
	nfs_protocol_function_t service_function;
	nfs_protocol_free_t free_function;
	xdrproc_t xdr_decode_func;
	xdrproc_t xdr_encode_func;
	char *funcname;
	unsigned int dispatch_behaviour;
} nfs_function_desc_t;

extern const nfs_function_desc_t nfs3_func_desc[];
extern const nfs_function_desc_t nfs4_func_desc[];
extern const nfs_function_desc_t mnt1_func_desc[];
extern const nfs_function_desc_t mnt3_func_desc[];
extern const nfs_function_desc_t nlm4_func_desc[];
extern const nfs_function_desc_t rquota1_func_desc[];
extern const nfs_function_desc_t rquota2_func_desc[];

int mnt_Null(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int mnt_Mnt(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	    nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int mnt_Dump(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int mnt_Umnt(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int mnt_UmntAll(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int mnt_Export(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

/* @}
 * -- End of MNT protocol functions. --
 */

int nlm_Null(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Test(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Lock(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Cancel(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unlock(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Sm_Notify(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		   nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Test_Message(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Cancel_Message(nfs_arg_t *, exportlist_t *, struct req_op_context *,
			nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Lock_Message(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unlock_Message(nfs_arg_t *, exportlist_t *, struct req_op_context *,
			nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Granted_Res(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Share(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Unshare(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		 nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nlm4_Free_All(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		  nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

/* @}
 * -- End of NLM protocol functions. --
 */

int rquota_Null(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int rquota_getquota(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		    nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int rquota_getactivequota(nfs_arg_t *, exportlist_t *, struct req_op_context *,
			  nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int rquota_setquota(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		    nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int rquota_setactivequota(nfs_arg_t *, exportlist_t *, struct req_op_context *,
			  nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

/* @}
 *  * -- End of RQUOTA protocol functions. --
 *   */

int nfs_Null(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Getattr(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Setattr(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Lookup(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Readlink(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		 nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Read(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Write(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Create(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Remove(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Rename(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Link(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Symlink(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Mkdir(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Rmdir(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	      nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Readdir(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs_Fsstat(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Access(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Readdirplus(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		     nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Fsinfo(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Pathconf(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		  nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Commit(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

int nfs3_Mknod(nfs_arg_t *, exportlist_t *, struct req_op_context *,
	       nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

/* Functions needed for nfs v4 */

int nfs4_Compound(nfs_arg_t *, exportlist_t *, struct req_op_context *,
		  nfs_worker_data_t *, struct svc_req *, nfs_res_t *);

typedef int (*nfs4_op_function_t) (struct nfs_argop4 *, compound_data_t *,
				   struct nfs_resop4 *);

int nfs4_op_access(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_close(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_commit(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_create(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_delegpurge(struct nfs_argop4 *, compound_data_t *,
		       struct nfs_resop4 *);

int nfs4_op_delegreturn(struct nfs_argop4 *, compound_data_t *,
			struct nfs_resop4 *);

int nfs4_op_getattr(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_getfh(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_link(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_lock(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_lockt(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_locku(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_lookup(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_lookupp(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_lookupp(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_nverify(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_open(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_open_confirm(struct nfs_argop4 *, compound_data_t *,
			 struct nfs_resop4 *);

int nfs4_op_open_downgrade(struct nfs_argop4 *, compound_data_t *data,
			   struct nfs_resop4 *);

int nfs4_op_openattr(struct nfs_argop4 *, compound_data_t *,
		     struct nfs_resop4 *);

int nfs4_op_putfh(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_putpubfh(struct nfs_argop4 *, compound_data_t *,
		     struct nfs_resop4 *);
int nfs4_op_putrootfh(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

int nfs4_op_read(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_readdir(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_remove(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_renew(struct nfs_argop4 *, compound_data_t *,
		  struct nfs_resop4 *);

int nfs4_op_rename(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_restorefh(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

int nfs4_op_readlink(struct nfs_argop4 *, compound_data_t *,
		     struct nfs_resop4 *);

int nfs4_op_savefh(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_secinfo(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_secinfo_no_name(struct nfs_argop4 *, compound_data_t *,
			    struct nfs_resop4 *);

int nfs4_op_setattr(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_setclientid(struct nfs_argop4 *, compound_data_t *,
			struct nfs_resop4 *);

int nfs4_op_setclientid_confirm(struct nfs_argop4 *, compound_data_t *,
				struct nfs_resop4 *);

int nfs4_op_verify(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_release_lockowner(struct nfs_argop4 *, compound_data_t *,
			      struct nfs_resop4 *);

int nfs4_op_illegal(struct nfs_argop4 *, compound_data_t *data,
		    struct nfs_resop4 *);

int nfs4_op_notsupp(struct nfs_argop4 *, compound_data_t *data,
		    struct nfs_resop4 *);

int nfs4_op_exchange_id(struct nfs_argop4 *, compound_data_t *,
			struct nfs_resop4 *);

int nfs4_op_commit(struct nfs_argop4 *, compound_data_t *,
		   struct nfs_resop4 *);

int nfs4_op_close(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_create_session(struct nfs_argop4 *, compound_data_t *,
			   struct nfs_resop4 *);

int nfs4_op_getdevicelist(struct nfs_argop4 *, compound_data_t *,
			  struct nfs_resop4 *);

int nfs4_op_free_stateid(struct nfs_argop4 *, compound_data_t *,
			 struct nfs_resop4 *);

int nfs4_op_getdeviceinfo(struct nfs_argop4 *, compound_data_t *,
			  struct nfs_resop4 *);

int nfs4_op_destroy_clientid(struct nfs_argop4 *, compound_data_t *,
			     struct nfs_resop4 *);

int nfs4_op_destroy_session(struct nfs_argop4 *, compound_data_t *,
			    struct nfs_resop4 *);

int nfs4_op_lock(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_lockt(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_locku(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

int nfs4_op_layoutget(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

int nfs4_op_layoutcommit(struct nfs_argop4 *, compound_data_t *,
			 struct nfs_resop4 *);

int nfs4_op_layoutreturn(struct nfs_argop4 *, compound_data_t *,
			 struct nfs_resop4 *);

int nfs4_op_reclaim_complete(struct nfs_argop4 *, compound_data_t *,
			     struct nfs_resop4 *);

int nfs4_op_sequence(struct nfs_argop4 *, compound_data_t *,
		     struct nfs_resop4 *);

int nfs4_op_set_ssv(struct nfs_argop4 *, compound_data_t *,
		    struct nfs_resop4 *);

int nfs4_op_test_stateid(struct nfs_argop4 *, compound_data_t *,
			 struct nfs_resop4 *);

int nfs4_op_write(struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);

/* Available operations on pseudo fs */
int nfs4_op_getattr_pseudo(struct nfs_argop4 *, compound_data_t *,
			   struct nfs_resop4 *);

int nfs4_op_access_pseudo(struct nfs_argop4 *, compound_data_t *,
			  struct nfs_resop4 *);

int set_compound_data_for_pseudo(compound_data_t *);

int nfs4_op_lookup_pseudo(struct nfs_argop4 *, compound_data_t *,
			  struct nfs_resop4 *);

int nfs4_op_lookupp_pseudo(struct nfs_argop4 *, compound_data_t *,
			   struct nfs_resop4 *);

int nfs4_op_lookupp_pseudo_by_exp(struct nfs_argop4 *, compound_data_t *,
				  struct nfs_resop4 *);

int nfs4_op_readdir_pseudo(struct nfs_argop4 *, compound_data_t *,
			   struct nfs_resop4 *);

/* NFSv4.2 */
int nfs4_op_write_plus(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

void nfs4_op_write_plus_Free(nfs_resop4 *resp);

int nfs4_op_read_plus(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

void nfs4_op_read_plus_Free(nfs_resop4 *resp);

int nfs4_op_seek(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

void nfs4_op_seek_Free(nfs_resop4 *resp);

int nfs4_op_io_advise(struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);

void nfs4_op_io_advise_Free(nfs_resop4 *resp);

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
	FATTR_XDR_SUCCESS_EXP,
	FATTR_XDR_FAILED,
	FATTR_BADOWNER
} fattr_xdr_result;

struct xdr_attrs_args {
	struct attrlist *attrs;
	nfs_fh4 *hdl4;
	uint32_t rdattr_error;
	uint64_t mounted_on_fileid;	/*< If this is the root directory
					   of a filesystem, the fileid of
					   the directory on which the
					   filesystem is mounted. */
	int nfs_status;
	compound_data_t *data;
	bool statfscalled;
	fsal_dynamicfsinfo_t *dynamicinfo;
};

typedef struct fattr4_dent {
	char *name;		/* The name of the operation */
	unsigned int supported;	/* Is this action supported? */
	unsigned int size_fattr4; /* The size of the dedicated attr subtype */
	unsigned int access;	/* The access type for this attributes */
	attrmask_t attrmask;	/* attr bit for decoding to attrs */
	attrmask_t exp_attrmask; /* attr bit for decoding to attrs in
				    case of exepction */
	fattr_xdr_result(*encode) (XDR * xdr, struct xdr_attrs_args *args);
	fattr_xdr_result(*decode) (XDR * xdr, struct xdr_attrs_args *args);
	fattr_xdr_result(*compare) (XDR * xdr1, XDR * xdr2);
} fattr4_dent_t;

extern const struct fattr4_dent fattr4tab[];

#define WORD0_FATTR4_RDATTR_ERROR (1 << FATTR4_RDATTR_ERROR)
#define WORD1_FATTR4_MOUNTED_ON_FILEID (1 << (FATTR4_MOUNTED_ON_FILEID - 32))

static inline int check_for_wrongsec_ok_attr(struct bitmap4 *attr_request)
{
	if (attr_request->bitmap4_len < 1)
		return true;
	if ((attr_request->map[0] & ~WORD0_FATTR4_RDATTR_ERROR) != 0)
		return false;
	if (attr_request->bitmap4_len < 2)
		return true;
	if ((attr_request->map[1] & ~WORD1_FATTR4_MOUNTED_ON_FILEID) != 0)
		return false;
	if (attr_request->bitmap4_len < 3)
		return true;
	if (attr_request->map[2] != 0)
		return false;
	return true;
}

static inline int check_for_rdattr_error(struct bitmap4 *attr_request)
{
	if (attr_request->bitmap4_len < 1)
		return false;
	if ((attr_request->map[0] & WORD0_FATTR4_RDATTR_ERROR) != 0)
		return true;
	return false;
}

/* BUGAZOMEU: Some definitions to be removed. FSAL parameters to be
   used instead */
#define FSINFO_MAX_FILESIZE  0xFFFFFFFFFFFFFFFFll
#define MAX_HARD_LINK_VALUE (0xffff)
#define NFS4_PSEUDOFS_MAX_READ_SIZE  1048576
#define NFS4_PSEUDOFS_MAX_WRITE_SIZE 1048576
#define NFS4_ROOT_UID 0

#define NFS_REQ_OK   0
#define NFS_REQ_DROP 1

/* Free functions */
void mnt1_Mnt_Free(nfs_res_t *);
void mnt3_Mnt_Free(nfs_res_t *);

void mnt_Dump_Free(nfs_res_t *);
void mnt_Export_Free(nfs_res_t *);
void mnt_Null_Free(nfs_res_t *);
void mnt_Umnt_Free(nfs_res_t *);
void mnt_UmntAll_Free(nfs_res_t *);

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

void rquota_Null_Free(nfs_res_t *);
void rquota_getquota_Free(nfs_res_t *);
void rquota_getactivequota_Free(nfs_res_t *);
void rquota_setquota_Free(nfs_res_t *);
void rquota_setactivequota_Free(nfs_res_t *);

void nfs_Null_Free(nfs_res_t *);
void nfs_Getattr_Free(nfs_res_t *);
void nfs_Setattr_Free(nfs_res_t *);
void nfs2_Lookup_Free(nfs_res_t *);
void nfs3_Lookup_Free(nfs_res_t *);
void nfs3_Access_Free(nfs_res_t *);
void nfs3_Readlink_Free(nfs_res_t *);
void nfs2_Read_Free(nfs_res_t *);
void nfs_Write_Free(nfs_res_t *);
void nfs_Create_Free(nfs_res_t *);
void nfs_Mkdir_Free(nfs_res_t *);
void nfs_Symlink_Free(nfs_res_t *);
void nfs3_Mknod_Free(nfs_res_t *);
void nfs_Remove_Free(nfs_res_t *);
void nfs_Rmdir_Free(nfs_res_t *);
void nfs_Rename_Free(nfs_res_t *);
void nfs_Link_Free(nfs_res_t *);
void nfs3_Readdir_Free(nfs_res_t *);
void nfs3_Readdirplus_Free(nfs_res_t *);
void nfs_Fsstat_Free(nfs_res_t *);
void nfs3_Fsinfo_Free(nfs_res_t *);
void nfs3_Pathconf_Free(nfs_res_t *);
void nfs3_Commit_Free(nfs_res_t *);
void nfs2_Readdir_Free(nfs_res_t *);
void nfs3_Read_Free(nfs_res_t *);
void nfs2_Readlink_Free(nfs_res_t *);
void nfs4_Compound_FreeOne(nfs_resop4 *);
void nfs4_Compound_Free(nfs_res_t *);
void nfs4_Compound_CopyResOne(nfs_resop4 *, nfs_resop4 *);
void nfs4_Compound_CopyRes(nfs_res_t *, nfs_res_t *);

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
void nfs4_op_delegreturn_CopyRes(DELEGRETURN4res *,
				 DELEGRETURN4res *);

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

/* Pseudo FS functions */
bool pseudo_mount_export(struct gsh_export *exp,
			 struct req_op_context *req_ctx);
void create_pseudofs(void);
int nfs4_ExportToPseudoFS(void);
pseudofs_t *nfs4_GetPseudoFs(void);

int nfs4_MakeCred(compound_data_t *);

nfsstat4 cache_entry_To_Fattr(cache_entry_t *, fattr4 *,
			      compound_data_t *, nfs_fh4 *,
			      struct bitmap4 *);

int nfs4_fsal_attr_To_Fattr(const struct attrlist *, fattr4 *,
			    compound_data_t *, struct bitmap4 *);
int nfs4_Fattr_To_fsal_attr(struct attrlist *, fattr4 *,
			    compound_data_t *, struct bitmap4 *);
bool nfs4_Fattr_Check_Access(fattr4 *, int);
bool nfs4_Fattr_Check_Access_Bitmap(struct bitmap4 *, int);
bool nfs4_Fattr_Supported(fattr4 *);
bool nfs4_Fattr_Supported_Bitmap(struct bitmap4 *);
int nfs4_Fattr_cmp(fattr4 *, fattr4 *);

bool nfs3_FSALattr_To_Fattr(exportlist_t *, const struct attrlist *,
			    fattr3 *);

bool cache_entry_to_nfs3_Fattr(cache_entry_t *, struct req_op_context *,
			       fattr3 *);

bool nfs3_Sattr_To_FSALattr(struct attrlist *, sattr3 *);

int nfs4_PseudoToFattr(pseudofs_entry_t *, fattr4 *, compound_data_t *,
		       nfs_fh4 *, struct bitmap4 *);

void nfs4_PseudoToFhandle(nfs_fh4 *, pseudofs_entry_t *);

int nfs4_Fattr_To_FSAL_attr(struct attrlist *, fattr4 *, compound_data_t *);

int nfs4_Fattr_To_fsinfo(fsal_dynamicfsinfo_t *, fattr4 *);

int nfs4_Fattr_Fill_Error(fattr4 *, nfsstat4);

int nfs4_FSALattr_To_Fattr(struct xdr_attrs_args *, struct bitmap4 *,
			   fattr4 *);

uint64_t nfs_htonl64(uint64_t);
uint64_t nfs_ntohl64(uint64_t);
void nfs4_bitmap4_Remove_Unsupported(struct bitmap4 *);

/* Error conversion routines */
nfsstat4 nfs4_Errno_verbose(cache_inode_status_t, const char *);
nfsstat3 nfs3_Errno_verbose(cache_inode_status_t, const char *);
#define nfs4_Errno(e) nfs4_Errno_verbose(e, __func__)
#define nfs3_Errno(e) nfs3_Errno_verbose(e, __func__)
int nfs3_AllocateFH(nfs_fh3 *);
int nfs4_AllocateFH(nfs_fh4 *);
#endif	/* NFS_PROTO_FUNCTIONS_H */
