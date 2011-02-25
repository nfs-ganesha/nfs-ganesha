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
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/include/nfs_proto_functions.h,v 1.31 2006/01/05 15:15:01 deniel Exp $
 *
 * $Log: nfs_proto_functions.h,v $
 * Revision 1.31  2006/01/05 15:15:01  deniel
 * Bug fixed in pseudo fs in front of a solaris client
 *
 * Revision 1.29  2005/10/12 11:31:32  leibovic
 * Split nfs_Lookup_free function.
 *
 * Revision 1.28  2005/10/10 12:37:15  leibovic
 * Splitting mnt_Mnt_Free in 2 function mnt1_Mnt_Free and mnt3_Mnt_Free.
 *
 * Revision 1.27  2005/10/06 09:28:34  deniel
 * Added dispatch behaviour management
 *
 * Revision 1.26  2005/10/03 12:52:08  deniel
 * NFSv4 OP Mem_Free functions were added
 *
 * Revision 1.25  2005/09/30 12:25:11  deniel
 * Added NFSv2/NFSv3 Free functions (for memory management)
 *
 * Revision 1.24  2005/08/08 08:41:36  deniel
 * NFSv4 Ok
 *
 * Revision 1.23  2005/08/05 09:26:13  deniel
 * Changed supported attr
 *
 * Revision 1.22  2005/08/01 13:28:35  deniel
 * Finer management of export lists
 *
 * Revision 1.21  2005/07/21 09:18:51  deniel
 * Structure of the file handles was redefined
 *
 * Revision 1.20  2005/07/20 12:56:59  deniel
 * Reorganisation of the source files
 *
 * Revision 1.19  2005/07/20 11:50:04  deniel
 * Mount protocol V1/V3 support almost complete
 *
 * Revision 1.18  2005/07/18 14:13:21  deniel
 * Currently integrating the layers together
 *
 * Revision 1.17  2005/07/11 15:30:43  deniel
 * Mount ok nfsv4 on udp and tcp with kernel 2.6.11 as client
 *
 * Revision 1.16  2005/07/05 09:20:23  deniel
 * Temporary mod on RPCSECGSS to avoid GSSAPI conflicts
 * Integration of the layers in the actual nfsd
 *
 * Revision 1.15  2005/03/02 09:23:04  leibovic
 * Addinf mount result.
 *
 * Revision 1.14  2005/01/26 15:52:04  leibovic
 * Removing multiply defined prototype for mnt_export.
 *
 * Revision 1.13  2005/01/24 16:25:15  deniel
 * mnt_* functions added to nfs_proto_functions.h
 *
 * Revision 1.12  2005/01/21 13:48:51  leibovic
 * Adding Mnt_export prototype.
 *
 * Revision 1.11  2005/01/18 08:29:04  leibovic
 * adding result value for mnt_Export.
 *
 * Revision 1.10  2005/01/17 13:29:41  deniel
 * Mod for gssprinc
 *
 * Revision 1.9  2005/01/14 14:28:02  leibovic
 * Adding mnt_null prototype.
 *
 * Revision 1.8  2005/01/14 13:44:13  deniel
 * separated functions for PROC NULL in NFS and MOUNT protocol
 *
 * Revision 1.7  2005/01/13 14:31:46  deniel
 * Adding stuff to exports structures
 *
 * Revision 1.6  2004/11/17 16:05:36  deniel
 * Pseudo fs is available on AIX 5.1
 * Mount is possible via nfsv4 putrootfh
 * Some memory allocation pb in XDR on Solaris
 *
 * Revision 1.5  2004/11/16 14:10:43  deniel
 * Headers files all moved here
 *
 * Revision 1.2  2004/11/16 10:06:44  deniel
 * Before trying the first mount from a CITI client ON A LINUX 2.6 Box.
 *
 * Revision 1.1  2004/11/15 16:46:20  deniel
 * Integration of pseudo fs
 *
 *
 */

#ifndef _NFS_PROTO_FUNCTIONS_H
#define _NFS_PROTO_FUNCTIONS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "Rpc.h"
#include "LRU_List.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"

#include "err_LRU_List.h"
#include "err_HashTable.h"

#define  NFS4_ATTRVALS_BUFFLEN  1024

#define NFS4_MY_MALLOC( p, l )                                                                                        \
do                                                                                                                    \
{                                                                                                                     \
  DisplayLogLevel( NIV_FULL_DEBUG, "Allocation de %d a l'adresse %p, file=%s, line =%d", l, p, __FILE__, __LINE__ ) ; \
  if( (  p = (char *)Mem_Alloc(  l ) ) == NULL )                                                              \
    {                                                                                                                 \
      DisplayLogLevel( NIV_MAJOR, "Malloc error, p=%p l=%d returning NFS4ERR_STALE", p, l ) ;                         \
      return NFS4ERR_STALE ;                                                                                          \
    }                                                                                                                 \
  memset( (char *)p, 0, l ) ;                                                                                         \
} while( 0 )

/* ------------------------------ Typedefs and structs----------------------- */

typedef union nfs_arg__
{
  fhandle2 arg_getattr2;
  SETATTR2args arg_setattr2;
  diropargs2 arg_lookup2;
  fhandle2 arg_readlink2;
  READ2args arg_read2;
  WRITE2args arg_write2;
  CREATE2args arg_create2;
  diropargs2 arg_remove2;
  RENAME2args arg_rename2;
  LINK2args arg_link2;
  SYMLINK2args arg_symlink2;
  CREATE2args arg_mkdir2;
  diropargs2 arg_rmdir2;
  READDIR2args arg_readdir2;
  fhandle2 arg_statfs2;
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

  /* mnt protocol arguments */
  dirpath arg_mnt;

} nfs_arg_t;

typedef union nfs_res__
{
  ATTR2res res_attr2;
  DIROP2res res_dirop2;
  READLINK2res res_readlink2;
  READ2res res_read2;
  nfsstat2 res_stat2;
  READDIR2res res_readdir2;
  STATFS2res res_statfs2;
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

  /* mount protocol returned values */
  fhstatus2 res_mnt1;
  exports res_mntexport;
  mountres3 res_mnt3;
  mountlist res_dump;

  char toto[1024];
} nfs_res_t;

/* flags related to the behaviour of the requests (to be stored in the dispatch behaviour field)  */
#define NOTHING_SPECIAL 0x0000  /* Nothing to be done for this kind of request              */
#define MAKES_WRITE     0x0001  /* The function modifyes the FSAL (not permitted for RO FS) */
#define NEEDS_CRED      0x0002  /* A credential is needed for this operation                */
#define CAN_BE_DUP      0x0004  /* Handling of dup request can be done for this request     */

typedef int (*nfs_protocol_function_t) (nfs_arg_t *,
                                        exportlist_t *,
                                        fsal_cred_t *,
                                        cache_inode_client_t *,
                                        hash_table_t *, struct Svc_req *, nfs_res_t *);

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

/**
 * @defgroup MNTprocs    Mount protocol functions.
 * 
 * @{
 */
int mnt_Null(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int mnt_Mnt(nfs_arg_t * parg /* IN  */ ,
            exportlist_t * pexport /* IN  */ ,
            fsal_cred_t * pcred /* IN  */ ,
            cache_inode_client_t * pclient /* IN  */ ,
            hash_table_t * ht /* INOUT */ ,
            struct Svc_req *preq /* IN  */ ,
            nfs_res_t * pres /* OUT */ );

int mnt_Dump(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int mnt_Umnt(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int mnt_UmntAll(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int mnt_Export(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

/* @}
 * -- End of MNT protocol functions. --
 */

/**
 * @defgroup NFSprocs    NFS protocols functions.
 * 
 * @{
 */

int nfs_Null(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int nfs_Getattr(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs_Setattr(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs2_Root(nfs_arg_t * parg /* IN  */ ,
              exportlist_t * pexport /* IN  */ ,
              fsal_cred_t * pcred /* IN  */ ,
              cache_inode_client_t * pclient /* IN  */ ,
              hash_table_t * ht /* INOUT */ ,
              struct Svc_req *preq /* IN  */ ,
              nfs_res_t * pres /* OUT */ );

int nfs_Lookup(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int nfs_Readlink(nfs_arg_t * parg /* IN  */ ,
                 exportlist_t * pexport /* IN  */ ,
                 fsal_cred_t * pcred /* IN  */ ,
                 cache_inode_client_t * pclient /* IN  */ ,
                 hash_table_t * ht /* INOUT */ ,
                 struct Svc_req *preq /* IN  */ ,
                 nfs_res_t * pres /* OUT */ );

int nfs_Read(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int nfs2_Writecache(nfs_arg_t * parg /* IN  */ ,
                    exportlist_t * pexport /* IN  */ ,
                    fsal_cred_t * pcred /* IN  */ ,
                    cache_inode_client_t * pclient /* IN  */ ,
                    hash_table_t * ht /* INOUT */ ,
                    struct Svc_req *preq /* IN  */ ,
                    nfs_res_t * pres /* OUT */ );

int nfs_Write(nfs_arg_t * parg /* IN  */ ,
              exportlist_t * pexport /* IN  */ ,
              fsal_cred_t * pcred /* IN  */ ,
              cache_inode_client_t * pclient /* IN  */ ,
              hash_table_t * ht /* INOUT */ ,
              struct Svc_req *preq /* IN  */ ,
              nfs_res_t * pres /* OUT */ );

int nfs_Create(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int nfs_Remove(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int nfs_Rename(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int nfs_Link(nfs_arg_t * parg /* IN  */ ,
             exportlist_t * pexport /* IN  */ ,
             fsal_cred_t * pcred /* IN  */ ,
             cache_inode_client_t * pclient /* IN  */ ,
             hash_table_t * ht /* INOUT */ ,
             struct Svc_req *preq /* IN  */ ,
             nfs_res_t * pres /* OUT */ );

int nfs_Symlink(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs_Mkdir(nfs_arg_t * parg /* IN  */ ,
              exportlist_t * pexport /* IN  */ ,
              fsal_cred_t * pcred /* IN  */ ,
              cache_inode_client_t * pclient /* IN  */ ,
              hash_table_t * ht /* INOUT */ ,
              struct Svc_req *preq /* IN  */ ,
              nfs_res_t * pres /* OUT */ );

int nfs_Rmdir(nfs_arg_t * parg /* IN  */ ,
              exportlist_t * pexport /* IN  */ ,
              fsal_cred_t * pcred /* IN  */ ,
              cache_inode_client_t * pclient /* IN  */ ,
              hash_table_t * ht /* INOUT */ ,
              struct Svc_req *preq /* IN  */ ,
              nfs_res_t * pres /* OUT */ );

int nfs_Readdir(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs_Fsstat(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int nfs3_Access(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs3_Readdirplus(nfs_arg_t * parg /* IN  */ ,
                     exportlist_t * pexport /* IN  */ ,
                     fsal_cred_t * pcred /* IN  */ ,
                     cache_inode_client_t * pclient /* IN  */ ,
                     hash_table_t * ht /* INOUT */ ,
                     struct Svc_req *preq /* IN  */ ,
                     nfs_res_t * pres /* OUT */ );

int nfs3_Fsinfo(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs3_Pathconf(nfs_arg_t * parg /* IN  */ ,
                  exportlist_t * pexport /* IN  */ ,
                  fsal_cred_t * pcred /* IN  */ ,
                  cache_inode_client_t * pclient /* IN  */ ,
                  hash_table_t * ht /* INOUT */ ,
                  struct Svc_req *preq /* IN  */ ,
                  nfs_res_t * pres /* OUT */ );

int nfs3_Commit(nfs_arg_t * parg /* IN  */ ,
                exportlist_t * pexport /* IN  */ ,
                fsal_cred_t * pcred /* IN  */ ,
                cache_inode_client_t * pclient /* IN  */ ,
                hash_table_t * ht /* INOUT */ ,
                struct Svc_req *preq /* IN  */ ,
                nfs_res_t * pres /* OUT */ );

int nfs3_Mknod(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

/* Functions needed for nfs v4 */

int nfs4_Compound(nfs_arg_t * parg /* IN  */ ,
                  exportlist_t * pexport /* IN  */ ,
                  fsal_cred_t * pcred /* IN  */ ,
                  cache_inode_client_t * pclient /* IN  */ ,
                  hash_table_t * ht /* INOUT */ ,
                  struct Svc_req *preq /* IN  */ ,
                  nfs_res_t * pres /* OUT */ );

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

/* Available operation on pseudo fs */
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

/* @}
 * -- End of NFS protocols functions. --
 */

/* Two dummy functions, just a step in the implementation process. These functions will
 * be removed when all NFSv2/v3/v4 functions will be available */
int dummy_func(nfs_arg_t * parg /* IN  */ ,
               exportlist_t * pexport /* IN  */ ,
               fsal_cred_t * pcred /* IN  */ ,
               cache_inode_client_t * pclient /* IN  */ ,
               hash_table_t * ht /* INOUT */ ,
               struct Svc_req *preq /* IN  */ ,
               nfs_res_t * pres /* OUT */ );

int dummy_op(struct nfs_argop4 *op,     /* [IN] NFS4 OP arguments */
             compound_data_t * data,    /* [IN] current data for the compound request */
             struct nfs_resop4 *resp);  /* [OUT] NFS4 OP results */

/*
 * Definition of an array for the characteristics of each GETATTR sub-operations
 */

typedef struct fattr4_dent
{
  char *name;                   /* The name of the operation  */
  unsigned int val;             /* The rank for the operation */
  unsigned int supported;       /* Is this action supported ? */
  unsigned size_fattr4;         /* The size of the dedicated attr subtype */
} fattr4_dent_t;

static const fattr4_dent_t fattr4tab[] = {
  "FATTR4_SUPPORTED_ATTRS", 0, 1, sizeof(fattr4_supported_attrs),
  "FATTR4_TYPE", 1, 1, sizeof(fattr4_type),
  "FATTR4_FH_EXPIRE_TYPE", 2, 1, sizeof(fattr4_fh_expire_type),
  "FATTR4_CHANGE", 3, 1, sizeof(fattr4_change),
  "FATTR4_SIZE", 4, 1, sizeof(fattr4_size),
  "FATTR4_LINK_SUPPORT", 5, 1, sizeof(fattr4_link_support),
  "FATTR4_SYMLINK_SUPPORT", 6, 1, sizeof(fattr4_symlink_support),
  "FATTR4_NAMED_ATTR", 7, 0, sizeof(fattr4_named_attr),
  "FATTR4_FSID", 8, 1, sizeof(fattr4_fsid),
  "FATTR4_UNIQUE_HANDLES", 9, 1, sizeof(fattr4_unique_handles),
  "FATTR4_LEASE_TIME", 10, 1, sizeof(fattr4_lease_time),
  "FATTR4_RDATTR_ERROR", 11, 0, sizeof(fattr4_rdattr_error),
  "FATTR4_ACL", 12, 0, sizeof(fattr4_acl),
  "FATTR4_ACLSUPPORT", 13, 0, sizeof(fattr4_aclsupport),
  "FATTR4_ARCHIVE", 14, 1, sizeof(fattr4_archive),
  "FATTR4_CANSETTIME", 15, 1, sizeof(fattr4_cansettime),
  "FATTR4_CASE_INSENSITIVE", 16, 1, sizeof(fattr4_case_insensitive),
  "FATTR4_CASE_PRESERVING", 17, 1, sizeof(fattr4_case_preserving),
  "FATTR4_CHOWN_RESTRICTED", 18, 1, sizeof(fattr4_chown_restricted),
  "FATTR4_FILEHANDLE", 19, 1, sizeof(fattr4_filehandle),
  "FATTR4_FILEID", 20, 1, sizeof(fattr4_fileid),
  "FATTR4_FILES_AVAIL", 21, 1, sizeof(fattr4_files_avail),
  "FATTR4_FILES_FREE", 22, 1, sizeof(fattr4_files_free),
  "FATTR4_FILES_TOTAL", 23, 1, sizeof(fattr4_files_total),
  "FATTR4_FS_LOCATIONS", 24, 0, sizeof(fattr4_fs_locations),
  "FATTR4_HIDDEN", 25, 1, sizeof(fattr4_hidden),
  "FATTR4_HOMOGENEOUS", 26, 1, sizeof(fattr4_homogeneous),
  "FATTR4_MAXFILESIZE", 27, 1, sizeof(fattr4_maxfilesize),
  "FATTR4_MAXLINK", 28, 1, sizeof(fattr4_maxlink),
  "FATTR4_MAXNAME", 29, 1, sizeof(fattr4_maxname),
  "FATTR4_MAXREAD", 30, 1, sizeof(fattr4_maxread),
  "FATTR4_MAXWRITE", 31, 1, sizeof(fattr4_maxwrite),
  "FATTR4_MIMETYPE", 32, 0, sizeof(fattr4_mimetype),
  "FATTR4_MODE", 33, 1, sizeof(fattr4_mode),
  "FATTR4_NO_TRUNC", 34, 1, sizeof(fattr4_no_trunc),
  "FATTR4_NUMLINKS", 35, 1, sizeof(fattr4_numlinks),
  "FATTR4_OWNER", 36, 1, sizeof(fattr4_owner),
  "FATTR4_OWNER_GROUP", 37, 1, sizeof(fattr4_owner_group),
  "FATTR4_QUOTA_AVAIL_HARD", 38, 0, sizeof(fattr4_quota_avail_hard),
  "FATTR4_QUOTA_AVAIL_SOFT", 39, 0, sizeof(fattr4_quota_avail_soft),
  "FATTR4_QUOTA_USED", 40, 0, sizeof(fattr4_quota_used),
  "FATTR4_RAWDEV", 41, 0, sizeof(fattr4_rawdev),        /* BUGAZOMEU : to be updated with FSAL attrs */
  "FATTR4_SPACE_AVAIL", 42, 1, sizeof(fattr4_space_avail),
  "FATTR4_SPACE_FREE", 43, 1, sizeof(fattr4_space_used),
  "FATTR4_SPACE_TOTAL", 44, 1, sizeof(fattr4_space_total),
  "FATTR4_SPACE_USED", 45, 1, sizeof(fattr4_space_used),
  "FATTR4_SYSTEM", 46, 1, sizeof(fattr4_system),
  "FATTR4_TIME_ACCESS", 47, 1, 12,      /*sizeof( fattr4_time_access )  not aligned on 32 bits */
  "FATTR4_TIME_ACCESS_SET", 48, 0, sizeof(fattr4_time_access_set),
  "FATTR4_TIME_BACKUP", 49, 0, 12,      /* sizeof( fattr4_time_backup ) not aligned on 32 bits */
  "FATTR4_TIME_CREATE", 50, 0, 12,      /* sizeof( fattr4_time_create ) not aligned on 32 bits */
  "FATTR4_TIME_DELTA", 51, 1, 12,       /*  sizeof( fattr4_time_delta ) not aligned on 32 bits */
  "FATTR4_TIME_METADATA", 52, 1, 12,    /* sizeof( fattr4_time_metadata ) not aligned on 32 bits */
  "FATTR4_TIME_MODIFY", 53, 1, 12,      /* sizeof( fattr4_time_modify ) not aligned on 32 bits */
  "FATTR4_TIME_MODIFY_SET", 54, 0, sizeof(fattr4_time_modify_set),
  "FATTR4_MOUNTED_ON_FILEID", 55, 1, sizeof(fattr4_mounted_on_fileid)
};

/* BUGAZOMEU: Some definitions to be removed. FSAL parameters to be used instead */
#define NFS4_LEASE_LIFETIME 120
#define FSINFO_MAX_FILESIZE  0xFFFFFFFFFFFFFFFFll
#define MAX_HARD_LINK_VALUE           (0xffff)
#define NFS4_PSEUDOFS_MAX_READ_SIZE  1024
#define NFS4_PSEUDOFS_MAX_WRITE_SIZE 1024
#define NFS4_ROOT_UID 0
#define NFS_MAXPATHLEN MAXPATHLEN
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
void nfs2_Root_Free(nfs_res_t * pres);
void nfs2_Writecache_Free(nfs_res_t * pres);
void nfs2_Readdir_Free(nfs_res_t * resp);
void nfs3_Read_Free(nfs_res_t * resp);
void nfs2_Readlink_Free(nfs_res_t * resp);
void nfs4_Compound_Free(nfs_res_t * pres);

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
void nfs4_op_openattr_FREE(OPENATTR4res * resp);
void nfs4_op_putfh_Free(PUTFH4res * resp);
void nfs4_op_putpubfh_Free(PUTPUBFH4res * resp);
void nfs4_op_putpubfh_Free(PUTPUBFH4res * resp);
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

void compound_data_Free(compound_data_t * data);

/* Pseudo FS functions */
int nfs4_ExportToPseudoFS(exportlist_t * pexportlist);
pseudofs_t *nfs4_GetPseudoFs(void);

int nfs4_SetCompoundExport(compound_data_t * data);
int nfs4_MakeCred(compound_data_t * data);

int nfs4_fsal_attr_To_Fattr(fsal_attrib_list_t * pattr, fattr4 * Fattr,
                            compound_data_t * data, bitmap4 * Bitmap);
int nfs4_Fattr_To_fsal_attr(fsal_attrib_list_t * pattr, fattr4 * Fattr,
                            compound_data_t * data, bitmap4 * Bitmap);

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
 *  nfs4_bitmap4_to_list  - convert an attribute's bitmap to a list of attributes
 *  nfs4_list_to_bitmap4  - convert a list of attributes to an attributes's bitmap
 */

int utf82str(char *str, int size, utf8string * utf8str);
int str2utf8(char *str, utf8string * utf8str);

int uid2utf8(uid_t uid, utf8string * utf8str);
int utf82uid(utf8string * utf8str, uid_t * Uid);

int uid2str(uid_t uid, char *str);
int str2uid(char *str, uid_t * Uid);

int gid2str(gid_t gid, char *str);
int str2gid(char *str, gid_t * Gid);

int gid2utf8(gid_t gid, utf8string * utf8str);
int utf82gid(utf8string * utf8str, gid_t * Gid);

void nfs4_stringid_split(char *buff, char *uidname, char *domainname);

seqid4 nfs4_NextSeqId(seqid4 seqid);

/* Attributes conversion */
int nfs2_Sattr_To_FSALattr(fsal_attrib_list_t * pFSAL_attr,     /* Out: file attributes */
                           sattr2 * pFattr);    /* In: file attributes  */

int nfs2_FSALattr_To_Fattr(fsal_attrib_list_t * pFSAL_attr,     /* In: file attributes  */
                           fattr2 * pFattr);    /* Out: file attributes */

int nfs3_FSALattr_To_Fattr(fsal_attrib_list_t * pFSAL_attr,     /* In: file attributes  */
                           fattr3 * pFattr);    /* Out: file attributes */

int nfs3_Sattr_To_FSALattr(fsal_attrib_list_t * pFSAL_attr,     /* Out: file attributes */
                           sattr3 * pFattr);    /* In: file attributes  */

int nfs4_FSAL_attr_To_Fattr(fsal_attrib_list_t * pFSAL_attr,    /* In: file attributes    */
                            fattr4 * pFattr,    /* Out: file attributes   */
                            compound_data_t * data,     /* InOut: Compound data   */
                            nfs_fh4 * objFH,    /* In: object file handle */
                            bitmap4 * bitmap);  /* Out: attributes bitmap */

int nfs4_PseudoToFattr(pseudofs_entry_t * psfsp,
                       fattr4 * Fattr,
                       compound_data_t * data, nfs_fh4 * objFH, bitmap4 * Bitmap);

int nfs4_Fattr_To_FSAL_attr(fsal_attrib_list_t * pFSAL_attr,    /* Out: File attributes  */
                            fattr4 * pFattr);   /* In: File attributes   */

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                /* time_how4          * mtime_set, *//* Out: How to set mtime */
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        /* time_how4          * atimen_set ) ; *//* Out: How to set atime */

void nfs4_list_to_bitmap4(bitmap4 * b, uint_t * plen, uint32_t * pval);
void nfs4_bitmap4_to_list(bitmap4 * b, uint_t * plen, uint32_t * pval);

/* Error conversion routines */
nfsstat4 nfs4_Errno(cache_inode_status_t error);
nfsstat3 nfs3_Errno(cache_inode_status_t error);
nfsstat2 nfs2_Errno(cache_inode_status_t error);
int nfs4_AllocateFH(nfs_fh4 * fh);
