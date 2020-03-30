/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 * @file    fsal_nfsv4_macros.h
 * @author  Author: deniel
 * @date    06/05/2007
 * @brief   Usefull macros to manage NFSv4 call from FSAL_PROXY
 *
 *
 */
#ifndef _FSAL_NFSV4_MACROS_H
#define _FSAL_NFSV4_MACROS_H

#include "gsh_rpc.h"
#include "nfs4.h"

#include "fsal.h"
#if 0
#include "fsal_convert.h"
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

#define TIMEOUTRPC {2, 0}

#define PRINT_HANDLE(tag, handle) \
do { \
	if (isFullDebug(COMPONENT_FSAL)) {				\
		char outstr[1024];					\
		snprintHandle(outstr, 1024, handle);			\
		LogFullDebug(COMPONENT_FSAL, \
			     "============> %s : handle=%s\n", tag, outstr); \
	}								\
} while (0)

/* Free a compound */
#define COMPOUNDV4_ARG_FREE \
do {gsh_free(argcompound.argarray_val); } while (0)

/* OP specific macros */

/**
 * Notice about NFS4_OP_SEQUENCE argop filling :
 * As rpc_context and slot are mutualized, sa_slotid and related sa_sequenceid
 * are place holder filled later on pxy_compoundv4_execute function, only when
 * the free pxy_rpc_io_context is chosen.
 */
#define COMPOUNDV4_ARG_ADD_OP_SEQUENCE(opcnt, argarray, sessionid, nb_slot) \
do {									\
	nfs_argop4 *op = argarray + opcnt; opcnt++;			\
	op->argop = NFS4_OP_SEQUENCE;					\
	memcpy(op->nfs_argop4_u.opsequence.sa_sessionid, sessionid,	\
	       sizeof(sessionid4));					\
	op->nfs_argop4_u.opsequence.sa_highest_slotid = nb_slot - 1;	\
	op->nfs_argop4_u.opsequence.sa_cachethis = false;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_GLOBAL_RECLAIM_COMPLETE(opcnt, argarray)	\
do {									\
	nfs_argop4 *op = argarray + opcnt; opcnt++;			\
	op->argop = NFS4_OP_RECLAIM_COMPLETE;				\
	op->nfs_argop4_u.opreclaim_complete.rca_one_fs = false;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_CREATE_SESSION(opcnt, argarray, cid,      \
		seqid, info, sec_parms4)				\
do {									\
	struct channel_attrs4 *fore_attrs;				\
	struct channel_attrs4 *back_attrs;				\
	CREATE_SESSION4args *opcreate_session;				\
									\
	nfs_argop4 *op = argarray + opcnt; opcnt++;			\
	op->argop = NFS4_OP_CREATE_SESSION;				\
	opcreate_session = &op->nfs_argop4_u.opcreate_session;		\
	opcreate_session->csa_clientid = cid;				\
	opcreate_session->csa_sequence = seqid;				\
	opcreate_session->csa_flags = CREATE_SESSION4_FLAG_CONN_BACK_CHAN; \
	fore_attrs = &opcreate_session->csa_fore_chan_attrs;		\
	fore_attrs->ca_headerpadsize = 0;				\
	fore_attrs->ca_maxrequestsize = info->srv_sendsize;		\
	fore_attrs->ca_maxresponsesize = info->srv_recvsize;		\
	fore_attrs->ca_maxresponsesize_cached = info->srv_recvsize;	\
	fore_attrs->ca_maxoperations = NB_MAX_OPERATIONS;		\
	fore_attrs->ca_maxrequests = NB_RPC_SLOT;			\
	fore_attrs->ca_rdma_ird.ca_rdma_ird_len = 0;			\
	fore_attrs->ca_rdma_ird.ca_rdma_ird_val = NULL;			\
	back_attrs = &opcreate_session->csa_back_chan_attrs;		\
	back_attrs->ca_headerpadsize = 0;				\
	back_attrs->ca_maxrequestsize = info->srv_recvsize;		\
	back_attrs->ca_maxresponsesize = info->srv_sendsize;		\
	back_attrs->ca_maxresponsesize_cached = info->srv_recvsize;	\
	back_attrs->ca_maxoperations = NB_MAX_OPERATIONS;		\
	back_attrs->ca_maxrequests = NB_RPC_SLOT;			\
	back_attrs->ca_rdma_ird.ca_rdma_ird_len = 0;			\
	back_attrs->ca_rdma_ird.ca_rdma_ird_val = NULL;			\
	opcreate_session->csa_cb_program = info->srv_prognum;		\
	opcreate_session->csa_sec_parms.csa_sec_parms_len = 1;		\
	(sec_parms4)->cb_secflavor = AUTH_NONE;				\
	opcreate_session->csa_sec_parms.csa_sec_parms_val = (sec_parms4); \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, argarray)  \
do {                                                       \
	argarray[opcnt].argop = NFS4_OP_PUTROOTFH;	   \
	opcnt++;					   \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_OPEN_CONFIRM(argcompound, __openseqid, \
					   __other, __seqid)	     \
do { \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].argop \
		= NFS4_OP_OPEN_CONFIRM;					\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opopen_confirm.seqid = __seqid;			\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opopen_confirm.open_stateid.seqid = __openseqid;	\
	memcpy(argcompound.argarray.argarray_val[\
		       argcompound.argarray.argarray_len].nfs_argop4_u.\
	       opopen_confirm.open_stateid.other,			\
	       _other, 12);						\
	argcompound.argarray.argarray_len += 1;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_OPEN_NOCREATE(argcompound, __seqid, inclientid, \
					    inaccess, inname, __owner_val, \
					    __owner_len)		\
do {  \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].argop = NFS4_OP_OPEN; \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		seqid = __seqid;				       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		share_access = OPEN4_SHARE_ACCESS_BOTH;		       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		share_deny = OPEN4_SHARE_DENY_NONE;		       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		owner.clientid = inclientid;			       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		owner.owner.owner_len =  __owner_len;		       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		owner.owner.owner_val = __owner_val;		       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		openhow.opentype = OPEN4_NOCREATE;		       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		claim.claim = CLAIM_NULL;			       \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.opopen.\
		claim.open_claim4_u.file = inname;		       \
	argcompound.argarray.argarray_len += 1;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_CLOSE_4_1(opcnt, argarray, __stateid)	\
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;		\
	op->argop = NFS4_OP_CLOSE;				\
	op->nfs_argop4_u.opclose.open_stateid.seqid		\
		= __stateid.seqid;				\
	memcpy(op->nfs_argop4_u.opclose.open_stateid.other,	\
	       __stateid.other, 12);				\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_CLOSE_4_1_STATELESS(opcnt, argarray)\
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;		\
	op->argop = NFS4_OP_CLOSE;				\
	memset(&op->nfs_argop4_u.opclose.open_stateid, 0,	\
	       sizeof(stateid4));				\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_GETATTR(opcnt, argarray, bitmap) \
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;		\
	op->argop = NFS4_OP_GETATTR;				\
	op->nfs_argop4_u.opgetattr.attr_request = bitmap;	\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, argarray, inattr, __other)	\
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;			\
	op->argop = NFS4_OP_SETATTR;					\
	op->nfs_argop4_u.opsetattr.stateid.seqid = 0;			\
	memcpy(op->nfs_argop4_u.opsetattr.stateid.other, __other,	\
	       sizeof(op->nfs_argop4_u.opsetattr.stateid.other));	\
	op->nfs_argop4_u.opsetattr.obj_attributes = inattr;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SETATTR_STATELESS(opcnt, argarray, inattr)\
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;			\
	op->argop = NFS4_OP_SETATTR;					\
	memset(&op->nfs_argop4_u.opsetattr.stateid, 0, sizeof(stateid4)); \
	op->nfs_argop4_u.opsetattr.obj_attributes = inattr;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_GETFH(opcnt, argarray) \
do { \
	argarray[opcnt].argop = NFS4_OP_GETFH;	     \
	opcnt++;				     \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_PUTFH(opcnt, argarray, nfs4fh) \
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;	     \
	op->argop = NFS4_OP_PUTFH;			     \
	op->nfs_argop4_u.opputfh.object = nfs4fh;	     \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_LOOKUP(opcnt, argarray, name) \
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;  \
	op->argop = NFS4_OP_LOOKUP;		     \
	op->nfs_argop4_u.oplookup.objname.utf8string_val = (char *)name; \
	op->nfs_argop4_u.oplookup.objname.utf8string_len = strlen(name); \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_LOOKUPP(opcnt, argarray) \
do { \
	argarray[opcnt].argop = NFS4_OP_LOOKUPP;     \
	opcnt++;				     \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SETCLIENTID(argcompound, inclient, incallback) \
do { \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].argop		\
		= NFS4_OP_SETCLIENTID;					\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opsetclientid.client = inclient;		\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opsetclientid.callback = incallback;		\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opsetclientid.callback_ident = 0;		\
	argcompound.argarray.argarray_len += 1;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SETCLIENTID_CONFIRM(argcompound, inclientid, \
						  inverifier)		\
do { \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].argop		\
		= NFS4_OP_SETCLIENTID_CONFIRM;				\
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].nfs_argop4_u.\
		opsetclientid_confirm.clientid = inclientid;	\
	strncpy(argcompound.argarray.argarray_val[\
			argcompound.argarray.argarray_len].nfs_argop4_u.\
		opsetclientid_confirm.setclientid_confirm,		\
		inverifier, NFS4_VERIFIER_SIZE);			\
	argcompound.argarray.argarray_len += 1;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_ACCESS(argcompound, inaccessflag) \
do { \
	argcompound.argarray.argarray_val[argcompound.argarray.argarray_len]\
		.argop = NFS4_OP_ACCESS;				\
	argcompound.argarray.argarray_val[argcompound.argarray.argarray_len]\
		.nfs_argop4_u.opaccess.access = inaccessflag;		\
	argcompound.argarray.argarray_len += 1;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_READDIR(opcnt, args, c4, inbitmap) \
do { \
	nfs_argop4 *op = args + opcnt; opcnt++;				\
	op->argop = NFS4_OP_READDIR;					\
	op->nfs_argop4_u.opreaddir.cookie = c4;				\
	memset(&op->nfs_argop4_u.opreaddir.cookieverf, \
	       0, NFS4_VERIFIER_SIZE);					\
	op->nfs_argop4_u.opreaddir.dircount = 2048;			\
	op->nfs_argop4_u.opreaddir.maxcount = 4096;			\
	op->nfs_argop4_u.opreaddir.attr_request = inbitmap;		\
} while (0)

#define COMPOUNDV4_ARGS_ADD_OP_OPEN_4_1(opcnt, args,  __share_access,	\
				     __share_deny, __owner_val,		\
				     __owner_len, __openhow, __claim)	\
do { \
	nfs_argop4 *op = args + opcnt; opcnt++;				\
	op->argop = NFS4_OP_OPEN;					\
	op->nfs_argop4_u.opopen.share_access = __share_access;		\
	op->nfs_argop4_u.opopen.share_deny = __share_deny;		\
	op->nfs_argop4_u.opopen.owner.owner.owner_len =  __owner_len;	\
	op->nfs_argop4_u.opopen.owner.owner.owner_val =  __owner_val;	\
	op->nfs_argop4_u.opopen.openhow = __openhow;			\
	op->nfs_argop4_u.opopen.claim = __claim;			\
} while (0)

#define COMPOUNDV4_ARGS_ADD_OP_OPEN(opcnt, args, oo_seqid, __share_access,\
				    __share_deny, inclientid, __owner_val,\
				    __owner_len, __openhow, __claim)	\
do { \
	nfs_argop4 *op = args + opcnt; opcnt++;				\
	op->argop = NFS4_OP_OPEN;					\
	op->nfs_argop4_u.opopen.seqid = oo_seqid;			\
	op->nfs_argop4_u.opopen.share_access = __share_access;		\
	op->nfs_argop4_u.opopen.share_deny = __share_deny;		\
	op->nfs_argop4_u.opopen.owner.clientid = inclientid;		\
	op->nfs_argop4_u.opopen.owner.owner.owner_len =  __owner_len;	\
	op->nfs_argop4_u.opopen.owner.owner.owner_val =  __owner_val;	\
	op->nfs_argop4_u.opopen.openhow = __openhow;			\
	op->nfs_argop4_u.opopen.claim = __claim;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_OPEN_CREATE(opcnt, args, inname, inattrs, \
					  inclientid, __owner_val, \
					  __owner_len)			\
do { \
	nfs_argop4 *op = args + opcnt; opcnt++;				\
	op->argop = NFS4_OP_OPEN;					\
	op->nfs_argop4_u.opopen.share_access = OPEN4_SHARE_ACCESS_BOTH;	\
	op->nfs_argop4_u.opopen.share_deny = OPEN4_SHARE_DENY_NONE;	\
	op->nfs_argop4_u.opopen.owner.clientid = inclientid;		\
	op->nfs_argop4_u.opopen.owner.owner.owner_len =  __owner_len;	\
	op->nfs_argop4_u.opopen.owner.owner.owner_val =  __owner_val;	\
	op->nfs_argop4_u.opopen.openhow.opentype = OPEN4_CREATE;	\
	op->nfs_argop4_u.opopen.openhow.openflag4_u.how.mode = GUARDED4; \
	op->nfs_argop4_u.opopen.openhow.openflag4_u.how.\
		createhow4_u.createattrs = inattrs;			\
	op->nfs_argop4_u.opopen.claim.claim = CLAIM_NULL;		\
	op->nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_val \
		= inname;						\
	op->nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_len = \
		strlen(inname);						\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_MKDIR(opcnt, argarray, inname, inattrs) \
do { \
	nfs_argop4 *op = argarray + opcnt; opcnt++;		\
	op->argop = NFS4_OP_CREATE;					\
	op->nfs_argop4_u.opcreate.objtype.type = NF4DIR;		\
	op->nfs_argop4_u.opcreate.objname.utf8string_val = inname;	\
	op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname); \
	op->nfs_argop4_u.opcreate.createattrs = inattrs;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_CREATE(opcnt, arg, inname, nf4typ, inattrs, \
				     specd)				\
do { \
	nfs_argop4 *op = arg + opcnt; opcnt++;				\
	op->argop = NFS4_OP_CREATE;					\
	op->nfs_argop4_u.opcreate.objtype.type = nf4typ;		\
	op->nfs_argop4_u.opcreate.objtype.createtype4_u.devdata = specd; \
	op->nfs_argop4_u.opcreate.objname.utf8string_val = inname;	\
	op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname); \
	op->nfs_argop4_u.opcreate.createattrs = inattrs;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SYMLINK(opcnt, args, inname, incontent, inattrs)\
do { \
	nfs_argop4 *op = args + opcnt; opcnt++;				\
	op->argop = NFS4_OP_CREATE;					\
	op->nfs_argop4_u.opcreate.objtype.type = NF4LNK;		\
	op->nfs_argop4_u.opcreate.objtype.createtype4_u.\
		linkdata.utf8string_val	= incontent;		\
	op->nfs_argop4_u.opcreate.objtype.createtype4_u.\
		linkdata.utf8string_len = strlen(incontent);		\
	op->nfs_argop4_u.opcreate.objname.utf8string_val = inname;	\
	op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname); \
	op->nfs_argop4_u.opcreate.createattrs = inattrs;		\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_LINK(opcnt, argarray, inname) \
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_LINK;					\
	op->nfs_argop4_u.oplink.newname.utf8string_val = inname;	\
	op->nfs_argop4_u.oplink.newname.utf8string_len = strlen(inname); \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_REMOVE(opcnt, argarray, inname) \
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_REMOVE;					\
	op->nfs_argop4_u.opremove.target.utf8string_val = inname;	\
	op->nfs_argop4_u.opremove.target.utf8string_len = strlen(inname); \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_RENAME(opcnt, argarray, inoldname, innewname)  \
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_RENAME;					\
	op->nfs_argop4_u.oprename.oldname.utf8string_val = inoldname;	\
	op->nfs_argop4_u.oprename.oldname.utf8string_len = strlen(inoldname); \
	op->nfs_argop4_u.oprename.newname.utf8string_val = innewname;	\
	op->nfs_argop4_u.oprename.newname.utf8string_len = strlen(innewname); \
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_READLINK(opcnt, argarray) \
do { \
	argarray[opcnt].argop = NFS4_OP_READLINK;			\
	opcnt++;							\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argarray) \
do { \
	argarray[opcnt].argop = NFS4_OP_SAVEFH;				\
	opcnt++;							\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_RESTOREFH(argcompound) \
do { \
	argcompound.argarray.argarray_val[\
		argcompound.argarray.argarray_len].argop		\
		= NFS4_OP_RESTOREFH;					\
	argcompound.argarray.argarray_len += 1;				\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_READ(opcnt, argarray, inoffset, incount,	\
				   __other)				\
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_READ;					\
	op->nfs_argop4_u.opread.stateid.seqid = 0;			\
	memcpy(op->nfs_argop4_u.opread.stateid.other, __other,	\
	       sizeof(op->nfs_argop4_u.opread.stateid.other));		\
	op->nfs_argop4_u.opread.offset = inoffset;			\
	op->nfs_argop4_u.opread.count  = incount;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_READ_STATELESS(opcnt, argarray, inoffset,	\
					     incount)			\
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_READ;					\
	memset(&op->nfs_argop4_u.opread.stateid, 0, sizeof(stateid4));	\
	op->nfs_argop4_u.opread.offset = inoffset;			\
	op->nfs_argop4_u.opread.count  = incount;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_READ_BYPASS(opcnt, argarray, inoffset, incount) \
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_READ;					\
	memset(&op->nfs_argop4_u.opread.stateid, 0xff, sizeof(stateid4)); \
	op->nfs_argop4_u.opread.offset = inoffset;			\
	op->nfs_argop4_u.opread.count  = incount;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_WRITE(opcnt, argarray, inoffset, inbuf,	\
				    inlen, instable, __other)		\
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_WRITE;					\
	op->nfs_argop4_u.opwrite.stable = instable;			\
	op->nfs_argop4_u.opread.stateid.seqid = 0;			\
	memcpy(op->nfs_argop4_u.opwrite.stateid.other, __other,		\
	       sizeof(op->nfs_argop4_u.opwrite.stateid.other));		\
	op->nfs_argop4_u.opwrite.offset = inoffset;			\
	op->nfs_argop4_u.opwrite.data.data_val = inbuf;			\
	op->nfs_argop4_u.opwrite.data.data_len = inlen;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_WRITE_STATELESS(opcnt, argarray, inoffset,\
					      inbuf, inlen, instable)	\
do { \
	nfs_argop4 *op = argarray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_WRITE;					\
	op->nfs_argop4_u.opwrite.stable = instable;			\
	memset(&op->nfs_argop4_u.opwrite.stateid, 0, sizeof(stateid4));	\
	op->nfs_argop4_u.opwrite.offset = inoffset;			\
	op->nfs_argop4_u.opwrite.data.data_val = inbuf;			\
	op->nfs_argop4_u.opwrite.data.data_len = inlen;			\
} while (0)

#define COMPOUNDV4_ARG_ADD_OP_COMMIT(opcnt, argoparray, inoffset, inlen) \
do { \
	nfs_argop4 *op = argoparray+opcnt; opcnt++;			\
	op->argop = NFS4_OP_COMMIT;					\
	op->nfs_argop4_u.opcommit.offset = inoffset;			\
	op->nfs_argop4_u.opcommit.count = inlen;			\
} while (0)

#define COMPOUNDV4_EXECUTE_SIMPLE(pcontext, argcompound, rescompound)   \
	  clnt_call(pcontext->rpc_client, NFSPROC4_COMPOUND,		\
		    (xdrproc_t)xdr_COMPOUND4args, &argcompound,		\
		    (xdrproc_t)xdr_COMPOUND4res,  &rescompound,		\
		    timeout)

#endif				/* _FSAL_NFSV4_MACROS_H */
