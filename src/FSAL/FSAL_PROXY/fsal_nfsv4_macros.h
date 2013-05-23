/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 * \file    fsal_nfsv4_macros.h
 * \author  $Author: deniel $
 * \date    06/05/2007
 * \version $Revision$
 * \brief   Usefull macros to manage NFSv4 call from FSAL_PROXY
 *
 *
 */
#ifndef _FSAL_NFSV4_MACROS_H
#define _FSAL_NFSV4_MACROS_H

#include "ganesha_rpc.h"
#include "nfs4.h"

#include "fsal.h"
#if 0
#include "fsal_internal.h"
#include "fsal_convert.h"
#include "fsal_common.h"
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))
#endif

#define TIMEOUTRPC {2, 0} 

#define PRINT_HANDLE( tag, handle )                                                     \
  do {                                                                                  \
    if(isFullDebug(COMPONENT_FSAL))                                                     \
      {                                                                                 \
        char outstr[1024] ;                                                             \
        snprintHandle(outstr, 1024, handle) ;                                           \
        LogFullDebug(COMPONENT_FSAL, "============> %s : handle=%s\n", tag, outstr ) ;  \
      }                                                                                 \
  } while( 0 )

/* Free a compound */
#define COMPOUNDV4_ARG_FREE \
do {gsh_free(argcompound.argarray_val);} while( 0 )

/* OP specific macros */
#define COMPOUNDV4_ARG_ADD_OP_PUTROOTFH(opcnt, argarray )  \
do {                                                       \
  argarray[opcnt].argop = NFS4_OP_PUTROOTFH ;              \
  opcnt ++;                                                \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_OPEN_CONFIRM( argcompound, __openseqid, __other, __seqid )                                                       \
do {                                                                                                                                           \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_OPEN_CONFIRM ;                                          \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen_confirm.seqid = __seqid ;                           \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen_confirm.open_stateid.seqid = __openseqid ;          \
  memcpy( argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen_confirm.open_stateid.other, __other, 12 ) ; \
  argcompound.argarray.argarray_len += 1 ;                                                                                                     \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_OPEN_NOCREATE( argcompound, __seqid, inclientid, inaccess, inname, __owner_val, __owner_len )             \
do {                                                                                                                                    \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_OPEN ;                                           \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.seqid = __seqid ;                            \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.share_access = OPEN4_SHARE_ACCESS_BOTH ;     \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.share_deny = OPEN4_SHARE_DENY_NONE ;         \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.owner.clientid = inclientid ;                \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.owner.owner.owner_len =  __owner_len  ;      \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.owner.owner.owner_val = __owner_val ;        \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.openhow.opentype = OPEN4_NOCREATE ;          \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.claim.claim = CLAIM_NULL ;                   \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opopen.claim.open_claim4_u.file = inname ;          \
  argcompound.argarray.argarray_len += 1 ;                                                                                              \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_CLOSE(opcnt, argarray, __stateid )             \
do {                                                                         \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                                \
  op->argop = NFS4_OP_CLOSE ;                                                \
  op->nfs_argop4_u.opclose.seqid = __stateid->seqid ;                        \
  op->nfs_argop4_u.opclose.open_stateid.seqid = __stateid->seqid ;           \
  memcpy(op->nfs_argop4_u.opclose.open_stateid.other, __stateid->other, 12); \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_GETATTR( opcnt, argarray, bitmap )             \
do {                                                                         \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                                \
  op->argop = NFS4_OP_GETATTR ;                                              \
  op->nfs_argop4_u.opgetattr.attr_request = bitmap;                          \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_SETATTR(opcnt, argarray, inattr )              \
do {                                                                         \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                                \
  op->argop = NFS4_OP_SETATTR ;                                              \
  memset(&op->nfs_argop4_u.opsetattr.stateid,0,sizeof(stateid4));            \
  op->nfs_argop4_u.opsetattr.obj_attributes = inattr;                        \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_GETFH( opcnt, argarray )               \
do {                                                                 \
  argarray[opcnt].argop = NFS4_OP_GETFH ;                            \
  opcnt ++ ;                                                         \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_PUTFH( opcnt, argarray, nfs4fh )       \
do {                                                                 \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                        \
  op->argop = NFS4_OP_PUTFH ;                                        \
  op->nfs_argop4_u.opputfh.object = nfs4fh ;                         \
} while( 0 )

#define COMPOUNDV4_ARG_ADD_OP_LOOKUP( opcnt, argarray, name )        \
do {                                                                 \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                        \
  op->argop = NFS4_OP_LOOKUP ;                                       \
  op->nfs_argop4_u.oplookup.objname.utf8string_val = (char *)name ;  \
  op->nfs_argop4_u.oplookup.objname.utf8string_len = strlen(name) ;  \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_LOOKUPP(opcnt, argarray )              \
do {                                                                 \
  argarray[opcnt].argop = NFS4_OP_LOOKUPP ;                          \
  opcnt++;                                                           \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_SETCLIENTID( argcompound, inclient, incallback )                                            \
do {                                                                                                                      \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_SETCLIENTID ;                      \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opsetclientid.client = inclient ;     \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opsetclientid.callback = incallback ; \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opsetclientid.callback_ident = 0 ;    \
  argcompound.argarray.argarray_len += 1 ;                                                                                \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_SETCLIENTID_CONFIRM( argcompound, inclientid, inverifier )                                                                                   \
do {                                                                                                                                                                       \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_SETCLIENTID_CONFIRM ;                                                               \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opsetclientid_confirm.clientid = inclientid ;                                          \
  strncpy( argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opsetclientid_confirm.setclientid_confirm, inverifier, NFS4_VERIFIER_SIZE ) ; \
  argcompound.argarray.argarray_len += 1 ;                                                                                                                                 \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_ACCESS( argcompound, inaccessflag )                                                    \
do {                                                                                                                 \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_ACCESS ;                      \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].nfs_argop4_u.opaccess.access = inaccessflag ; \
  argcompound.argarray.argarray_len += 1 ;                                                                           \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_READDIR(opcnt, args, c4, inbitmap)              \
do {                                                                          \
  nfs_argop4 *op = args + opcnt; opcnt++;                                     \
  op->argop = NFS4_OP_READDIR ;                                               \
  op->nfs_argop4_u.opreaddir.cookie = c4;                                     \
  memset(&op->nfs_argop4_u.opreaddir.cookieverf, 0, NFS4_VERIFIER_SIZE);      \
  op->nfs_argop4_u.opreaddir.dircount = 2048 ;                                \
  op->nfs_argop4_u.opreaddir.maxcount = 4096 ;                                \
  op->nfs_argop4_u.opreaddir.attr_request = inbitmap ;                        \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_OPEN_CREATE(opcnt, args, inname, inattrs, inclientid, __owner_val, __owner_len) \
do {                                                                          \
  nfs_argop4 *op = args + opcnt; opcnt++;                                     \
  op->argop = NFS4_OP_OPEN ;                                                  \
  op->nfs_argop4_u.opopen.seqid = 0 ;                                         \
  op->nfs_argop4_u.opopen.share_access = OPEN4_SHARE_ACCESS_BOTH ;            \
  op->nfs_argop4_u.opopen.share_deny = OPEN4_SHARE_DENY_NONE ;                \
  op->nfs_argop4_u.opopen.owner.clientid = inclientid ;                       \
  op->nfs_argop4_u.opopen.owner.owner.owner_len =  __owner_len ;              \
  op->nfs_argop4_u.opopen.owner.owner.owner_val =  __owner_val ;              \
  op->nfs_argop4_u.opopen.openhow.opentype = OPEN4_CREATE ;                   \
  op->nfs_argop4_u.opopen.openhow.openflag4_u.how.mode = GUARDED4 ;           \
  op->nfs_argop4_u.opopen.openhow.openflag4_u.how.createhow4_u.createattrs = inattrs ; \
  op->nfs_argop4_u.opopen.claim.claim = CLAIM_NULL ;                          \
  op->nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_val = inname; \
  op->nfs_argop4_u.opopen.claim.open_claim4_u.file.utf8string_len = strlen(inname); \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_MKDIR(opcnt, argarray, inname, inattrs )      \
do {                                                                        \
  nfs_argop4 *op = argarray + opcnt; opcnt++;                               \
  op->argop = NFS4_OP_CREATE ;                                              \
  op->nfs_argop4_u.opcreate.objtype.type = NF4DIR ;                         \
  op->nfs_argop4_u.opcreate.objname.utf8string_val = inname ;         \
  op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname) ;   \
  op->nfs_argop4_u.opcreate.createattrs = inattrs ;                         \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_CREATE(opcnt, arg, inname, nf4typ, inattrs, specd )      \
do {                                                                        \
  nfs_argop4 *op = arg + opcnt; opcnt++;                                    \
  op->argop = NFS4_OP_CREATE ;                                              \
  op->nfs_argop4_u.opcreate.objtype.type = nf4typ;                          \
  op->nfs_argop4_u.opcreate.objtype.createtype4_u.devdata = specd;          \
  op->nfs_argop4_u.opcreate.objname.utf8string_val = inname ;               \
  op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname) ;       \
  op->nfs_argop4_u.opcreate.createattrs = inattrs ;                         \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_SYMLINK(opcnt, args, inname, incontent, inattrs)\
do {                                                                          \
  nfs_argop4 *op = args + opcnt; opcnt++;                                 \
  op->argop = NFS4_OP_CREATE ;                                                \
  op->nfs_argop4_u.opcreate.objtype.type = NF4LNK ;                           \
  op->nfs_argop4_u.opcreate.objtype.createtype4_u.linkdata.utf8string_val = incontent; \
  op->nfs_argop4_u.opcreate.objtype.createtype4_u.linkdata.utf8string_len = strlen(incontent) ; \
  op->nfs_argop4_u.opcreate.objname.utf8string_val = inname;            \
  op->nfs_argop4_u.opcreate.objname.utf8string_len = strlen(inname); \
  op->nfs_argop4_u.opcreate.createattrs = inattrs ;                           \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_LINK(opcnt, argarray, inname )                 \
do {                                                                         \
  nfs_argop4 *op = argarray+opcnt; opcnt++;                                  \
  op->argop = NFS4_OP_LINK ;                                                 \
  op->nfs_argop4_u.oplink.newname.utf8string_val = inname;             \
  op->nfs_argop4_u.oplink.newname.utf8string_len = strlen(inname);     \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_REMOVE(opcnt, argarray, inname )               \
do {                                                                         \
  nfs_argop4 *op = argarray+opcnt; opcnt++;                                  \
  op->argop = NFS4_OP_REMOVE ;                                               \
  op->nfs_argop4_u.opremove.target.utf8string_val = inname;           \
  op->nfs_argop4_u.opremove.target.utf8string_len = strlen(inname); \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_RENAME(opcnt, argarray, inoldname, innewname)  \
do {                                                                         \
  nfs_argop4 *op = argarray+opcnt; opcnt++;                                  \
  op->argop = NFS4_OP_RENAME ;                                               \
  op->nfs_argop4_u.oprename.oldname.utf8string_val = inoldname ;       \
  op->nfs_argop4_u.oprename.oldname.utf8string_len = strlen(inoldname) ; \
  op->nfs_argop4_u.oprename.newname.utf8string_val = innewname ;       \
  op->nfs_argop4_u.oprename.newname.utf8string_len = strlen(innewname) ; \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_READLINK(opcnt, argarray )                     \
do {                                                                         \
  argarray[opcnt].argop = NFS4_OP_READLINK ;                                 \
  opcnt++;                                                                   \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_SAVEFH(opcnt, argarray )                       \
do {                                                                         \
  argarray[opcnt].argop = NFS4_OP_SAVEFH;                                    \
  opcnt++;                                                                   \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_RESTOREFH( argcompound )                                             \
do {                                                                                               \
  argcompound.argarray.argarray_val[argcompound.argarray.argarray_len].argop = NFS4_OP_RESTOREFH ; \
  argcompound.argarray.argarray_len += 1 ;                                                         \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_READ(opcnt, argarray, inoffset, incount )      \
do {                                                                         \
  nfs_argop4 *op = argarray+opcnt; opcnt++;                                  \
  op->argop = NFS4_OP_READ ;                                                 \
  memset( &op->nfs_argop4_u.opread.stateid, 0, sizeof( stateid4 ) ) ;        \
  op->nfs_argop4_u.opread.offset = inoffset ;                                \
  op->nfs_argop4_u.opread.count  = incount ;                                 \
} while ( 0 )

#define COMPOUNDV4_ARG_ADD_OP_WRITE(opcnt, argarray, inoffset, inbuf, inlen) \
do {                                                                         \
  nfs_argop4 *op = argarray+opcnt; opcnt++;                                  \
  op->argop = NFS4_OP_WRITE ;                                                \
  op->nfs_argop4_u.opwrite.stable= DATA_SYNC4 ;                              \
  memset(&op->nfs_argop4_u.opwrite.stateid, 0, sizeof( stateid4));           \
  op->nfs_argop4_u.opwrite.offset = inoffset ;                               \
  op->nfs_argop4_u.opwrite.data.data_val = inbuf ;                           \
  op->nfs_argop4_u.opwrite.data.data_len = inlen ;                           \
} while ( 0 )

#define COMPOUNDV4_EXECUTE_SIMPLE( pcontext, argcompound, rescompound )   \
   clnt_call( pcontext->rpc_client, NFSPROC4_COMPOUND,                    \
              (xdrproc_t)xdr_COMPOUND4args, (caddr_t)&argcompound,        \
              (xdrproc_t)xdr_COMPOUND4res,  (caddr_t)&rescompound,        \
              timeout )

#endif                          /* _FSAL_NFSV4_MACROS_H */
