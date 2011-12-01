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
 * \file    nfs4_Compound.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/16 16:25:44 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_Compound.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"

typedef struct nfs4_op_desc__
{
  char *name;
  unsigned int val;
  int (*funct) (struct nfs_argop4 *, compound_data_t *, struct nfs_resop4 *);
} nfs4_op_desc_t;

/* This array maps the operation number to the related position in array optab4 */
#ifndef _USE_NFS4_1
const int optab4index[] =
    { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39
};

#else
const int optab4index[] =
    { 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23,
  24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45,
  46,
  47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58
};

#endif

#define POS_ILLEGAL_V40 40
#define POS_ILLEGAL_V41 59

static const nfs4_op_desc_t optab4v0[] = {
  {"OP_ACCESS", NFS4_OP_ACCESS, nfs4_op_access},
  {"OP_CLOSE", NFS4_OP_CLOSE, nfs4_op_close},
  {"OP_COMMIT", NFS4_OP_COMMIT, nfs4_op_commit},
  {"OP_CREATE", NFS4_OP_CREATE, nfs4_op_create},
  {"OP_DELEGPURGE", NFS4_OP_DELEGPURGE, nfs4_op_delegpurge},
  {"OP_DELEGRETURN", NFS4_OP_DELEGRETURN, nfs4_op_delegreturn},
  {"OP_GETATTR", NFS4_OP_GETATTR, nfs4_op_getattr},
  {"OP_GETFH", NFS4_OP_GETFH, nfs4_op_getfh},
  {"OP_LINK", NFS4_OP_LINK, nfs4_op_link},
  {"OP_LOCK", NFS4_OP_LOCK, nfs4_op_lock},
  {"OP_LOCKT", NFS4_OP_LOCKT, nfs4_op_lockt},
  {"OP_LOCKU", NFS4_OP_LOCKU, nfs4_op_locku},
  {"OP_LOOKUP", NFS4_OP_LOOKUP, nfs4_op_lookup},
  {"OP_LOOKUPP", NFS4_OP_LOOKUPP, nfs4_op_lookupp},
  {"OP_NVERIFY", NFS4_OP_NVERIFY, nfs4_op_nverify},
  {"OP_OPEN", NFS4_OP_OPEN, nfs4_op_open},
  {"OP_OPENATTR", NFS4_OP_OPENATTR, nfs4_op_openattr},
  {"OP_OPEN_CONFIRM", NFS4_OP_OPEN_CONFIRM, nfs4_op_open_confirm},
  {"OP_OPEN_DOWNGRADE", NFS4_OP_OPEN_DOWNGRADE, nfs4_op_open_downgrade},
  {"OP_PUTFH", NFS4_OP_PUTFH, nfs4_op_putfh},
  {"OP_PUTPUBFH", NFS4_OP_PUTPUBFH, nfs4_op_putpubfh},
  {"OP_PUTROOTFH", NFS4_OP_PUTROOTFH, nfs4_op_putrootfh},
  {"OP_READ", NFS4_OP_READ, nfs4_op_read},
  {"OP_READDIR", NFS4_OP_READDIR, nfs4_op_readdir},
  {"OP_READLINK", NFS4_OP_READLINK, nfs4_op_readlink},
  {"OP_REMOVE", NFS4_OP_REMOVE, nfs4_op_remove},
  {"OP_RENAME", NFS4_OP_RENAME, nfs4_op_rename},
  {"OP_RENEW", NFS4_OP_RENEW, nfs4_op_renew},
  {"OP_RESTOREFH", NFS4_OP_RESTOREFH, nfs4_op_restorefh},
  {"OP_SAVEFH", NFS4_OP_SAVEFH, nfs4_op_savefh},
  {"OP_SECINFO", NFS4_OP_SECINFO, nfs4_op_secinfo},
  {"OP_SETATTR", NFS4_OP_SETATTR, nfs4_op_setattr},
  {"OP_SETCLIENTID", NFS4_OP_SETCLIENTID, nfs4_op_setclientid},
  {"OP_SETCLIENTID_CONFIRM", NFS4_OP_SETCLIENTID_CONFIRM, nfs4_op_setclientid_confirm},
  {"OP_VERIFY", NFS4_OP_VERIFY, nfs4_op_verify},
  {"OP_WRITE", NFS4_OP_WRITE, nfs4_op_write},
  {"OP_RELEASE_LOCKOWNER", NFS4_OP_RELEASE_LOCKOWNER, nfs4_op_release_lockowner},
  {"OP_ILLEGAL", NFS4_OP_ILLEGAL, nfs4_op_illegal}
};

#ifdef _USE_NFS4_1
static const nfs4_op_desc_t optab4v1[] = {
  {"OP_ACCESS", NFS4_OP_ACCESS, nfs4_op_access},
  {"OP_CLOSE", NFS4_OP_CLOSE, nfs41_op_close},
  {"OP_COMMIT", NFS4_OP_COMMIT, nfs4_op_commit},
  {"OP_CREATE", NFS4_OP_CREATE, nfs4_op_create},
  {"OP_DELEGPURGE", NFS4_OP_DELEGPURGE, nfs4_op_delegpurge},
  {"OP_DELEGRETURN", NFS4_OP_DELEGRETURN, nfs4_op_delegreturn},
  {"OP_GETATTR", NFS4_OP_GETATTR, nfs4_op_getattr},
  {"OP_GETFH", NFS4_OP_GETFH, nfs4_op_getfh},
  {"OP_LINK", NFS4_OP_LINK, nfs4_op_link},
  {"OP_LOCK", NFS4_OP_LOCK, nfs41_op_lock},
  {"OP_LOCKT", NFS4_OP_LOCKT, nfs41_op_lockt},
  {"OP_LOCKU", NFS4_OP_LOCKU, nfs41_op_locku},
  {"OP_LOOKUP", NFS4_OP_LOOKUP, nfs4_op_lookup},
  {"OP_LOOKUPP", NFS4_OP_LOOKUPP, nfs4_op_lookupp},
  {"OP_NVERIFY", NFS4_OP_NVERIFY, nfs4_op_nverify},
  {"OP_OPEN", NFS4_OP_OPEN, nfs41_op_open},
  {"OP_OPENATTR", NFS4_OP_OPENATTR, nfs4_op_openattr},
  {"OP_OPEN_CONFIRM", NFS4_OP_OPEN_CONFIRM, nfs4_op_illegal},   /* OP_OPEN_CONFIRM is deprecated in NFSv4.1 */
  {"OP_OPEN_DOWNGRADE", NFS4_OP_OPEN_DOWNGRADE, nfs4_op_open_downgrade},
  {"OP_PUTFH", NFS4_OP_PUTFH, nfs4_op_putfh},
  {"OP_PUTPUBFH", NFS4_OP_PUTPUBFH, nfs4_op_putpubfh},
  {"OP_PUTROOTFH", NFS4_OP_PUTROOTFH, nfs4_op_putrootfh},
  {"OP_READ", NFS4_OP_READ, nfs41_op_read},
  {"OP_READDIR", NFS4_OP_READDIR, nfs4_op_readdir},
  {"OP_READLINK", NFS4_OP_READLINK, nfs4_op_readlink},
  {"OP_REMOVE", NFS4_OP_REMOVE, nfs4_op_remove},
  {"OP_RENAME", NFS4_OP_RENAME, nfs4_op_rename},
  {"OP_RENEW", NFS4_OP_RENEW, nfs4_op_renew},
  {"OP_RESTOREFH", NFS4_OP_RESTOREFH, nfs4_op_restorefh},
  {"OP_SAVEFH", NFS4_OP_SAVEFH, nfs4_op_savefh},
  {"OP_SECINFO", NFS4_OP_SECINFO, nfs4_op_secinfo},
  {"OP_SETATTR", NFS4_OP_SETATTR, nfs4_op_setattr},
  {"OP_SETCLIENTID", NFS4_OP_SETCLIENTID, nfs4_op_setclientid},
  {"OP_SETCLIENTID_CONFIRM", NFS4_OP_SETCLIENTID_CONFIRM, nfs4_op_setclientid_confirm},
  {"OP_VERIFY", NFS4_OP_VERIFY, nfs4_op_verify},
  {"OP_WRITE", NFS4_OP_WRITE, nfs41_op_write},
  {"OP_RELEASE_LOCKOWNER", NFS4_OP_RELEASE_LOCKOWNER, nfs4_op_release_lockowner},
  {"OP_BACKCHANNEL_CTL", NFS4_OP_BACKCHANNEL_CTL, nfs4_op_illegal},     /* tbd */
  {"OP_BIND_CONN_TO_SESSION", NFS4_OP_BIND_CONN_TO_SESSION, nfs4_op_illegal},   /* tbd */
  {"OP_EXCHANGE_ID", NFS4_OP_EXCHANGE_ID, nfs41_op_exchange_id},
  {"OP_CREATE_SESSION", NFS4_OP_CREATE_SESSION, nfs41_op_create_session},
  {"OP_DESTROY_SESSION", NFS4_OP_DESTROY_SESSION, nfs41_op_destroy_session},
  {"OP_FREE_STATEID", NFS4_OP_FREE_STATEID, nfs41_op_free_stateid},   
  {"OP_GET_DIR_DELEGATION", NFS4_OP_GET_DIR_DELEGATION, nfs4_op_illegal},       /* tbd */
  {"OP_GETDEVICEINFO", NFS4_OP_GETDEVICEINFO, nfs41_op_getdeviceinfo},
  {"OP_GETDEVICELIST", NFS4_OP_GETDEVICELIST, nfs41_op_getdevicelist},
  {"OP_LAYOUTCOMMIT", NFS4_OP_LAYOUTCOMMIT, nfs41_op_layoutcommit},
  {"OP_LAYOUTGET", NFS4_OP_LAYOUTGET, nfs41_op_layoutget},
  {"OP_LAYOUTRETURN", NFS4_OP_LAYOUTRETURN, nfs41_op_layoutreturn},
  {"OP_SECINFO_NO_NAME", NFS4_OP_SECINFO_NO_NAME, nfs4_op_illegal},     /* tbd */
  {"OP_SEQUENCE", NFS4_OP_SEQUENCE, nfs41_op_sequence},
  {"OP_SET_SSV", NFS4_OP_SET_SSV, nfs41_op_set_ssv},
  {"OP_TEST_STATEID", NFS4_OP_TEST_STATEID, nfs41_op_test_stateid}, 
  {"OP_WANT_DELEGATION", NFS4_OP_WANT_DELEGATION, nfs4_op_illegal},     /* tbd */
  {"OP_DESTROY_CLIENTID", NFS4_OP_DESTROY_CLIENTID, nfs4_op_illegal},   /* tbd */
  {"OP_RECLAIM_COMPLETE", NFS4_OP_RECLAIM_COMPLETE, nfs41_op_reclaim_complete},
  {"OP_ILLEGAL", NFS4_OP_ILLEGAL, nfs4_op_illegal}
};
#endif                          /* _USE_NFS4_1 */

#ifdef _USE_NFS4_1
nfs4_op_desc_t *optabvers[] =
    { (nfs4_op_desc_t *) optab4v0, (nfs4_op_desc_t *) optab4v1 };
#else
nfs4_op_desc_t *optabvers[] = { (nfs4_op_desc_t *) optab4v0 };
#endif

/**
 * nfs4_COMPOUND: The NFS PROC4 COMPOUND
 *
 * Implements the NFS PROC4 COMPOUND.
 * This routine processes the content of the nfsv4 operation list and composes the result.
 * On this aspect it is a little similar to a dispatch routine.
 * Operation and functions necessary to process them are defined in array optab4 .
 *
 *
 *  @param parg        [IN]  generic nfs arguments
 *  @param pexportlist [IN]  the full export list
 *  @param pcontex     [IN]  context for the FSAL (unused but kept for nfs functions prototype homogeneity)
 *  @param pclient     [INOUT] client resource for request management
 *  @param ht          [INOUT] cache inode hash table
 *  @param preq        [IN]  RPC svc request
 *  @param pres        [OUT] generic nfs reply
 *
 *  @see   nfs4_op_<*> functions
 *  @see   nfs4_GetPseudoFs
 *
 */

int nfs4_Compound(nfs_arg_t * parg /* IN     */ ,
                  exportlist_t * pexport /* IN     */ ,
                  fsal_op_context_t * pcontext /* IN     */ ,
                  cache_inode_client_t * pclient /* INOUT  */ ,
                  hash_table_t * ht /* INOUT */ ,
                  struct svc_req *preq /* IN     */ ,
                  nfs_res_t * pres /* OUT    */ )
{
  unsigned int i = 0;
  int status = NFS4_OK;
  struct nfs_resop4 res;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_Compound";
  compound_data_t data;
  int opindex;
  #define TAGLEN 64
  char tagstr[TAGLEN + 1 + 5];

  /* A "local" #define to avoid typo with nfs (too) long structure names */
#define COMPOUND4_ARRAY parg->arg_compound4.argarray
#define COMPOUND4_MINOR parg->arg_compound4.minorversion

#ifdef _USE_NFS4_1
  if(COMPOUND4_MINOR > 1)
#else
  if(COMPOUND4_MINOR != 0)
#endif
    {
      LogCrit(COMPONENT_NFS_V4,
              "Bad Minor Version %d",
              COMPOUND4_MINOR);

      pres->res_compound4.status = NFS4ERR_MINOR_VERS_MISMATCH;
      pres->res_compound4.resarray.resarray_len = 0;
      return NFS_REQ_OK;
    }

  /* Check for empty COMPOUND request */
  if(COMPOUND4_ARRAY.argarray_len == 0)
    {
      LogMajor(COMPONENT_NFS_V4,
               "An empty COMPOUND (no operation in it) was received");

      pres->res_compound4.status = NFS4_OK;
      pres->res_compound4.resarray.resarray_len = 0;
      return NFS_REQ_OK;
    }

  /* Check if this export supports NFSv4 */
  if( ( pexport->options & EXPORT_OPTION_NFSV4 ) == 0 )
   {
      LogMajor( COMPONENT_NFS_V4,
                "The export(id=%u) does not support NFSv4... rejecting it", pexport->id ) ;
      pres->res_compound4.status = NFS4ERR_PERM ;
      pres->res_compound4.resarray.resarray_val[0].nfs_resop4_u.opaccess.status = NFS4ERR_PERM ;
      return NFS_REQ_OK ;
   }

  /* Check for too long request */
  if(COMPOUND4_ARRAY.argarray_len > 30)
    {
      LogMajor(COMPONENT_NFS_V4,
               "A COMPOUND with too many operations (%d) was received",
               COMPOUND4_ARRAY.argarray_len);

      pres->res_compound4.status = NFS4ERR_RESOURCE;
      pres->res_compound4.resarray.resarray_len = 0;
      return NFS_REQ_OK;
    }

  /* Initialisation of the compound request internal's data */
  memset(&data, 0, sizeof(data));

  /* Minor version related stuff */
  data.minorversion = COMPOUND4_MINOR;
  /** @todo BUGAZOMEU: Reminder: Stats on NFSv4 operations are to be set here */

  data.pfullexportlist = pexport;       /* Full export list is provided in input */
  data.pcontext = pcontext;     /* Get the fsal credentials from the worker thread */
  data.pseudofs = nfs4_GetPseudoFs();
  data.reqp = preq;
  data.ht = ht;
  data.pclient = pclient;

  strcpy(data.MntPath, "/");

  /* Building the client credential field */
  if(nfs_rpc_req2client_cred(preq, &(data.credential)) == -1)
    return NFS_REQ_DROP;        /* Malformed credential */

  /* Keeping the same tag as in the arguments */
  memcpy(&(pres->res_compound4.tag), &(parg->arg_compound4.tag),
         sizeof(parg->arg_compound4.tag));

  if(utf8dup(&(pres->res_compound4.tag), &(parg->arg_compound4.tag)) == -1)
    {
      LogCrit(COMPONENT_NFS_V4, "Unable to duplicate tag into response");
      return NFS_REQ_DROP;
    }

  /* Allocating the reply nfs_resop4 */
  if((pres->res_compound4.resarray.resarray_val =
      (struct nfs_resop4 *)Mem_Alloc((COMPOUND4_ARRAY.argarray_len) *
                                     sizeof(struct nfs_resop4))) == NULL)
    {
      return NFS_REQ_DROP;
    }

  if(isDebug(COMPONENT_NFS_V4) && pres->res_compound4.tag.utf8string_len > 0)
    {
      sprintf(tagstr, " TAG=");
      utf82str(tagstr+5, TAGLEN, &(pres->res_compound4.tag));
    }
  else
    {
      tagstr[0] = '\0';
    }

  /* Managing the operation list */
  LogDebug(COMPONENT_NFS_V4,
           "COMPOUND: There are %d operations%s",
           COMPOUND4_ARRAY.argarray_len, tagstr);

#ifdef _USE_NFS4_1
  /* Manage error NFS4ERR_NOT_ONLY_OP */
  if(COMPOUND4_ARRAY.argarray_len > 1)
    {
      /* If not prepended ny OP4_SEQUENCE, OP4_EXCHANGE_ID should be the only request in the compound
       * see 18.35.3. and test EID8 for details */
      if(optabvers[1][optab4index[COMPOUND4_ARRAY.argarray_val[0].argop]].val ==
         NFS4_OP_EXCHANGE_ID)
        {
          status = NFS4ERR_NOT_ONLY_OP;
          pres->res_compound4.resarray.resarray_val[0].nfs_resop4_u.opexchange_id.
              eir_status = status;
          pres->res_compound4.status = status;

          return NFS_REQ_OK;
        }
    }
#endif

  pres->res_compound4.resarray.resarray_len = COMPOUND4_ARRAY.argarray_len;
  for(i = 0; i < COMPOUND4_ARRAY.argarray_len; i++)
    {
      /* Use optab4index to reference the operation */
#ifdef _USE_NFS4_1
      data.oppos = i;           /* Useful to check if OP_SEQUENCE is used as the first operation */

      if(COMPOUND4_MINOR == 1)
        {
          if(data.psession != NULL)
            {
              if(data.psession->fore_channel_attrs.ca_maxoperations == i)
                {
                  status = NFS4ERR_TOO_MANY_OPS;
                  pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess.
                      status = status;
                  pres->res_compound4.resarray.resarray_val[i].resop =
                      COMPOUND4_ARRAY.argarray_val[i].argop;
                  pres->res_compound4.status = status;
                  break;        /* stop loop */
                }
            }
        }

      /* if( COMPOUND4_MINOR == 1 ) */
      if((COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RELEASE_LOCKOWNER
          && COMPOUND4_MINOR == 0)
         || (COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RECLAIM_COMPLETE
             && COMPOUND4_MINOR == 1))
#else
      if(COMPOUND4_ARRAY.argarray_val[i].argop <= NFS4_OP_RELEASE_LOCKOWNER)
#endif
        opindex = optab4index[COMPOUND4_ARRAY.argarray_val[i].argop];
      else
       {
         /* Set optindex to op_illegal */
         opindex = (COMPOUND4_MINOR==0)?optab4index[POS_ILLEGAL_V40]:optab4index[POS_ILLEGAL_V41];  
         LogMajor( COMPONENT_NFS_V4, "Client is using Illegal operation #%u", COMPOUND4_ARRAY.argarray_val[i].argop ) ;
       }

      LogDebug(COMPONENT_NFS_V4,
               "Request %d is %d = %s, entry %d in the op array%s",
               i,
               optabvers[COMPOUND4_MINOR][opindex].val,
               optabvers[COMPOUND4_MINOR][opindex].name,
               opindex,
               tagstr);

      memset(&res, 0, sizeof(res));
      status = (optabvers[COMPOUND4_MINOR][opindex].funct) (&(COMPOUND4_ARRAY.argarray_val[i]),
                                                            &data,
                                                            &res);

      memcpy(&(pres->res_compound4.resarray.resarray_val[i]), &res, sizeof(res));

      LogCompoundFH(&data);

      /* All the operation, like NFS4_OP_ACESS, have a first replyied field called .status */
      pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess.status = status;

      if(status != NFS4_OK)
        {
          /* An error occured, we do not manage the other requests in the COMPOUND, this may be a regular behaviour */
          LogDebug(COMPONENT_NFS_V4,
                   "Status of %s in position %d = %s%s",
                   optabvers[COMPOUND4_MINOR][opindex].name,
                   i,
                   nfsstat4_to_str(status),
                   tagstr);

          pres->res_compound4.resarray.resarray_len = i + 1;

          break;
        }
#ifdef _USE_NFS4_1
      /* Check Req size */

      /* NFS_V4.1 specific stuff */
      if(COMPOUND4_MINOR == 1)
        {
          if(i == 0)            /* OP_SEQUENCE is always the first operation within the request */
            {
              if((optabvers[1][optab4index[COMPOUND4_ARRAY.argarray_val[0].argop]].val ==
                  NFS4_OP_SEQUENCE)
                 || (optabvers[1][optab4index[COMPOUND4_ARRAY.argarray_val[0].argop]].val
                     == NFS4_OP_CREATE_SESSION))
                {
                  /* Manage sessions's DRC : replay previously cached request */
                  if(data.use_drc == TRUE)
                    {
                      /* Replay cache */
                      memcpy((char *)pres, data.pcached_res,
                             (COMPOUND4_ARRAY.argarray_len) * sizeof(struct nfs_resop4));
                      status = ((COMPOUND4res *) data.pcached_res)->status;
                      break;    /* Exit the for loop */
                    }
                }
            }
        }
#endif
    }                           /* for */

  /* Complete the reply, in particular, tell where you stopped if unsuccessfull COMPOUD */
  pres->res_compound4.status = status;

#ifdef _USE_NFS4_1
  /* Manage session's DRC : keep NFS4.1 replay for later use */
  if(COMPOUND4_MINOR == 1)
    {
      if(data.pcached_res != NULL)      /* Pointer has been set by nfs41_op_sequence and points to cached zone */
        {
          memcpy(data.pcached_res, (char *)pres,
                 (COMPOUND4_ARRAY.argarray_len) * sizeof(struct nfs_resop4));
        }
    }
#endif

  if(status != NFS4_OK)
    LogDebug(COMPONENT_NFS_V4,
             "End status = %s lastindex = %d%s",
             nfsstat4_to_str(status), i, tagstr);

  compound_data_Free(&data);

  return NFS_REQ_OK;
}                               /* nfs4_Compound */

/**
 *
 * nfs4_Compound_FreeOne: Mem_Free the result for one NFS4_OP
 *
 * @param resp pointer to be Mem_Freed
 *
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void nfs4_Compound_FreeOne(nfs_resop4 * pres)
{
  /* LogFullDebug(COMPONENT_NFS_V4,
                  "nfs4_Compound_Free sur op=%s",
                  optabvers[COMPOUND4_MINOR][optab4index[pres->resop]].name);
  */
  switch (pres->resop)
    {
      case NFS4_OP_ACCESS:
        nfs4_op_access_Free(&(pres->nfs_resop4_u.opaccess));
        break;

      case NFS4_OP_CLOSE:
        nfs4_op_close_Free(&(pres->nfs_resop4_u.opclose));
        break;

      case NFS4_OP_COMMIT:
        nfs4_op_commit_Free(&(pres->nfs_resop4_u.opcommit));
        break;

      case NFS4_OP_CREATE:
        nfs4_op_create_Free(&(pres->nfs_resop4_u.opcreate));
        break;

      case NFS4_OP_DELEGPURGE:
        nfs4_op_delegpurge_Free(&(pres->nfs_resop4_u.opdelegpurge));
        break;

      case NFS4_OP_DELEGRETURN:
        nfs4_op_delegreturn_Free(&(pres->nfs_resop4_u.opdelegreturn));
        break;

      case NFS4_OP_GETATTR:
        nfs4_op_getattr_Free(&(pres->nfs_resop4_u.opgetattr));
        break;

      case NFS4_OP_GETFH:
        nfs4_op_getfh_Free(&(pres->nfs_resop4_u.opgetfh));
        break;

      case NFS4_OP_LINK:
        nfs4_op_link_Free(&(pres->nfs_resop4_u.oplink));
        break;

      case NFS4_OP_LOCK:
        nfs4_op_lock_Free(&(pres->nfs_resop4_u.oplock));
        break;

      case NFS4_OP_LOCKT:
        nfs4_op_lockt_Free(&(pres->nfs_resop4_u.oplockt));
        break;

      case NFS4_OP_LOCKU:
        nfs4_op_locku_Free(&(pres->nfs_resop4_u.oplocku));
        break;

      case NFS4_OP_LOOKUP:
        nfs4_op_lookup_Free(&(pres->nfs_resop4_u.oplookup));
        break;

      case NFS4_OP_LOOKUPP:
        nfs4_op_lookupp_Free(&(pres->nfs_resop4_u.oplookupp));
        break;

      case NFS4_OP_NVERIFY:
        nfs4_op_nverify_Free(&(pres->nfs_resop4_u.opnverify));
        break;

      case NFS4_OP_OPEN:
        nfs4_op_open_Free(&(pres->nfs_resop4_u.opopen));
        break;

      case NFS4_OP_OPENATTR:
        nfs4_op_openattr_Free(&(pres->nfs_resop4_u.opopenattr));
        break;

      case NFS4_OP_OPEN_CONFIRM:
        nfs4_op_open_confirm_Free(&(pres->nfs_resop4_u.opopen_confirm));
        break;

      case NFS4_OP_OPEN_DOWNGRADE:
        nfs4_op_open_downgrade_Free(&(pres->nfs_resop4_u.opopen_downgrade));
        break;

      case NFS4_OP_PUTFH:
        nfs4_op_putfh_Free(&(pres->nfs_resop4_u.opputfh));
        break;

      case NFS4_OP_PUTPUBFH:
        nfs4_op_putpubfh_Free(&(pres->nfs_resop4_u.opputpubfh));
        break;

      case NFS4_OP_PUTROOTFH:
        nfs4_op_putrootfh_Free(&(pres->nfs_resop4_u.opputrootfh));
        break;

      case NFS4_OP_READ:
        nfs4_op_read_Free(&(pres->nfs_resop4_u.opread));
        break;

      case NFS4_OP_READDIR:
        nfs4_op_readdir_Free(&(pres->nfs_resop4_u.opreaddir));
        break;

      case NFS4_OP_READLINK:
      	nfs4_op_readlink_Free(&(pres->nfs_resop4_u.opreadlink));
        break;

      case NFS4_OP_REMOVE:
        nfs4_op_remove_Free(&(pres->nfs_resop4_u.opremove));
        break;

      case NFS4_OP_RENAME:
        nfs4_op_rename_Free(&(pres->nfs_resop4_u.oprename));
        break;

      case NFS4_OP_RENEW:
        nfs4_op_renew_Free(&(pres->nfs_resop4_u.oprenew));
        break;

      case NFS4_OP_RESTOREFH:
        nfs4_op_restorefh_Free(&(pres->nfs_resop4_u.oprestorefh));
        break;

      case NFS4_OP_SAVEFH:
        nfs4_op_savefh_Free(&(pres->nfs_resop4_u.opsavefh));
        break;

      case NFS4_OP_SECINFO:
        nfs4_op_secinfo_Free(&(pres->nfs_resop4_u.opsecinfo));
        break;

      case NFS4_OP_SETATTR:
        nfs4_op_setattr_Free(&(pres->nfs_resop4_u.opsetattr));
        break;

      case NFS4_OP_SETCLIENTID:
        nfs4_op_setclientid_Free(&(pres->nfs_resop4_u.opsetclientid));
        break;

      case NFS4_OP_SETCLIENTID_CONFIRM:
        nfs4_op_setclientid_confirm_Free(&(pres->nfs_resop4_u.opsetclientid_confirm));
        break;

      case NFS4_OP_VERIFY:
        nfs4_op_verify_Free(&(pres->nfs_resop4_u.opverify));
        break;

      case NFS4_OP_WRITE:
        nfs4_op_write_Free(&(pres->nfs_resop4_u.opwrite));
        break;

      case NFS4_OP_RELEASE_LOCKOWNER:
        nfs4_op_release_lockowner_Free(&(pres->nfs_resop4_u.oprelease_lockowner));
        break;

#ifdef _USE_NFS4_1
      case NFS4_OP_EXCHANGE_ID:
        nfs41_op_exchange_id_Free(&(pres->nfs_resop4_u.opexchange_id));
        break;

      case NFS4_OP_CREATE_SESSION:
        nfs41_op_create_session_Free(&(pres->nfs_resop4_u.opcreate_session));
        break;

      case NFS4_OP_SEQUENCE:
        nfs41_op_sequence_Free(&(pres->nfs_resop4_u.opsequence));
        break;

      case NFS4_OP_GETDEVICEINFO:
        nfs41_op_getdeviceinfo_Free(&(pres->nfs_resop4_u.opgetdeviceinfo));
        break;

      case NFS4_OP_GETDEVICELIST:
        nfs41_op_getdevicelist_Free(&(pres->nfs_resop4_u.opgetdevicelist));
        break;

      case NFS4_OP_TEST_STATEID:
        nfs41_op_test_stateid_Free(&(pres->nfs_resop4_u.optest_stateid));
        break;

      case NFS4_OP_FREE_STATEID:
        nfs41_op_free_stateid_Free(&(pres->nfs_resop4_u.opfree_stateid));
        break;

      case NFS4_OP_BACKCHANNEL_CTL:
      case NFS4_OP_BIND_CONN_TO_SESSION:
      case NFS4_OP_DESTROY_SESSION:
      case NFS4_OP_GET_DIR_DELEGATION:
      case NFS4_OP_LAYOUTCOMMIT:
      case NFS4_OP_LAYOUTGET:
      case NFS4_OP_LAYOUTRETURN:
      case NFS4_OP_SECINFO_NO_NAME:
      case NFS4_OP_SET_SSV:
      case NFS4_OP_WANT_DELEGATION:
      case NFS4_OP_DESTROY_CLIENTID:
      case NFS4_OP_RECLAIM_COMPLETE:
        nfs41_op_reclaim_complete_Free(&(pres->nfs_resop4_u.opreclaim_complete));
        break;
#endif

      case NFS4_OP_ILLEGAL:
        nfs4_op_illegal_Free(&(pres->nfs_resop4_u.opillegal));
        break;
    }                       /* switch */
}

/**
 *
 * nfs4_Compound_Free: Mem_Free the result for NFS4PROC_COMPOUND
 *
 * Mem_Free the result for NFS4PROC_COMPOUND.
 *
 * @param resp pointer to be Mem_Freed
 *
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void nfs4_Compound_Free(nfs_res_t * pres)
{
  unsigned int i = 0;

  LogFullDebug(COMPONENT_NFS_V4,
               "nfs4_Compound_Free de %p (resarraylen : %i)",
               pres,
               pres->res_compound4.resarray.resarray_len);

  for(i = 0; i < pres->res_compound4.resarray.resarray_len; i++)
    nfs4_Compound_FreeOne(&pres->res_compound4.resarray.resarray_val[i]);

  Mem_Free((char *)pres->res_compound4.resarray.resarray_val);
  free_utf8(&pres->res_compound4.tag);

  return;
}                               /* nfs4_Compound_Free */

/**
 *
 * compound_data_Free: Mem_Frees the compound data structure.
 *
 * Mem_Frees the compound data structure..
 *
 * @param data pointer to be Mem_Freed
 *
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void compound_data_Free(compound_data_t * data)
{
  if(data->currentFH.nfs_fh4_val != NULL)
    Mem_Free((char *)data->currentFH.nfs_fh4_val);

  if(data->rootFH.nfs_fh4_val != NULL)
    Mem_Free((char *)data->rootFH.nfs_fh4_val);

  if(data->publicFH.nfs_fh4_val != NULL)
    Mem_Free((char *)data->publicFH.nfs_fh4_val);

  if(data->savedFH.nfs_fh4_val != NULL)
    Mem_Free((char *)data->savedFH.nfs_fh4_val);

  if(data->mounted_on_FH.nfs_fh4_val != NULL)
    Mem_Free((char *)data->mounted_on_FH.nfs_fh4_val);

}                               /* compound_data_Free */

/**
 *
 * nfs4_Compound_CopyResOne: Copy the result for one NFS4_OP
 *
 */
void nfs4_Compound_CopyResOne(nfs_resop4 * pres_dst, nfs_resop4 * pres_src)
{
  /* Copy base data structure */
  memcpy(pres_dst, pres_src, sizeof(*pres_dst));

  /* Do deep copy where necessary */
  switch (pres_src->resop)
    {
      case NFS4_OP_ACCESS:
        break;

      case NFS4_OP_CLOSE:
        nfs4_op_close_CopyRes(&(pres_dst->nfs_resop4_u.opclose),
                              &(pres_src->nfs_resop4_u.opclose));
        return;

      case NFS4_OP_COMMIT:
      case NFS4_OP_CREATE:
      case NFS4_OP_DELEGPURGE:
      case NFS4_OP_DELEGRETURN:
      case NFS4_OP_GETATTR:
      case NFS4_OP_GETFH:
      case NFS4_OP_LINK:
        break;

      case NFS4_OP_LOCK:
        nfs4_op_lock_CopyRes(&(pres_dst->nfs_resop4_u.oplock),
                             &(pres_src->nfs_resop4_u.oplock));
        return;

      case NFS4_OP_LOCKT:
        break;

      case NFS4_OP_LOCKU:
        nfs4_op_locku_CopyRes(&(pres_dst->nfs_resop4_u.oplocku),
                              &(pres_src->nfs_resop4_u.oplocku));
        return;

      case NFS4_OP_LOOKUP:
      case NFS4_OP_LOOKUPP:
      case NFS4_OP_NVERIFY:
        break;

      case NFS4_OP_OPEN:
        nfs4_op_open_CopyRes(&(pres_dst->nfs_resop4_u.opopen),
                             &(pres_src->nfs_resop4_u.opopen));
        return;

      case NFS4_OP_OPENATTR:
        break;

      case NFS4_OP_OPEN_CONFIRM:
        nfs4_op_open_confirm_CopyRes(&(pres_dst->nfs_resop4_u.opopen_confirm),
                                     &(pres_src->nfs_resop4_u.opopen_confirm));
        return;

      case NFS4_OP_OPEN_DOWNGRADE:
        nfs4_op_open_downgrade_CopyRes(&(pres_dst->nfs_resop4_u.opopen_downgrade),
                                       &(pres_src->nfs_resop4_u.opopen_downgrade));
        return;

      case NFS4_OP_PUTFH:
      case NFS4_OP_PUTPUBFH:
      case NFS4_OP_PUTROOTFH:
      case NFS4_OP_READ:
      case NFS4_OP_READDIR:
      case NFS4_OP_READLINK:
      case NFS4_OP_REMOVE:
      case NFS4_OP_RENAME:
      case NFS4_OP_RENEW:
      case NFS4_OP_RESTOREFH:
      case NFS4_OP_SAVEFH:
      case NFS4_OP_SECINFO:
      case NFS4_OP_SETATTR:
      case NFS4_OP_SETCLIENTID:
      case NFS4_OP_SETCLIENTID_CONFIRM:
      case NFS4_OP_VERIFY:
      case NFS4_OP_WRITE:
      case NFS4_OP_RELEASE_LOCKOWNER:
        break;

#ifdef _USE_NFS4_1
      case NFS4_OP_EXCHANGE_ID:
      case NFS4_OP_CREATE_SESSION:
      case NFS4_OP_SEQUENCE:
      case NFS4_OP_GETDEVICEINFO:
      case NFS4_OP_GETDEVICELIST:
      case NFS4_OP_BACKCHANNEL_CTL:
      case NFS4_OP_BIND_CONN_TO_SESSION:
      case NFS4_OP_DESTROY_SESSION:
      case NFS4_OP_FREE_STATEID:
      case NFS4_OP_GET_DIR_DELEGATION:
      case NFS4_OP_LAYOUTCOMMIT:
      case NFS4_OP_LAYOUTGET:
      case NFS4_OP_LAYOUTRETURN:
      case NFS4_OP_SECINFO_NO_NAME:
      case NFS4_OP_SET_SSV:
      case NFS4_OP_TEST_STATEID:
      case NFS4_OP_WANT_DELEGATION:
      case NFS4_OP_DESTROY_CLIENTID:
      case NFS4_OP_RECLAIM_COMPLETE:
        break;
#endif

      case NFS4_OP_ILLEGAL:
        break;
    }                       /* switch */

  LogFatal(COMPONENT_NFS_V4,
           "nfs4_Compound_CopyResOne not implemented for %d",
           pres_src->resop);
}

/**
 *
 * nfs4_Compound_CopyRes: Copy the result for NFS4PROC_COMPOUND
 *
 * Copy the result for NFS4PROC_COMPOUND.
 *
 * @param resp pointer to be Mem_Freed
 *
 * @return nothing (void function).
 *
 * @see nfs4_op_getfh
 *
 */
void nfs4_Compound_CopyRes(nfs_res_t * pres_dst, nfs_res_t * pres_src)
{
  unsigned int i = 0;

  LogFullDebug(COMPONENT_NFS_V4,
               "nfs4_Compound_CopyRes of %p to %p (resarraylen : %i)",
               pres_src, pres_dst,
               pres_src->res_compound4.resarray.resarray_len);

  for(i = 0; i < pres_src->res_compound4.resarray.resarray_len; i++)
    nfs4_Compound_CopyResOne(&pres_dst->res_compound4.resarray.resarray_val[i],
                             &pres_src->res_compound4.resarray.resarray_val[i]);
}

/**
 *
 *  nfs4_op_stat_update: updates the NFSv4 operations specific statistics for a COMPOUND4 requests (either v4.0 or v4.1).
 *
 *  Updates the NFSv4 operations specific statistics for a COMPOUND4 requests (either v4.0 or v4.1).
 *
 *  @param parg argument for the COMPOUND4 request
 *  @param pres result for the COMPOUND4 request
 *  @param pstat_req pointer to the worker's structure for NFSv4 stats
 *
 * @return -1 if failed 0 otherwise
 *
 */

int nfs4_op_stat_update(nfs_arg_t * parg /* IN     */ ,
                        nfs_res_t * pres /* IN    */ ,
                        nfs_request_stat_t * pstat_req /* OUT */ )
{
  int i = 0;

  switch (COMPOUND4_MINOR)
    {
    case 0:
      for(i = 0; i < pres->res_compound4.resarray.resarray_len; i++)
        {
          pstat_req->nb_nfs40_op += 1;
          pstat_req->stat_op_nfs40[pres->res_compound4.resarray.resarray_val[i].resop].
              total += 1;

          /* All operations's reply structures start with their status, whatever the name of this field */
          if(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess.status ==
             NFS4_OK)
            pstat_req->stat_op_nfs40[pres->res_compound4.resarray.resarray_val[i].resop].
                success += 1;
          else
            pstat_req->stat_op_nfs40[pres->res_compound4.resarray.resarray_val[i].resop].
                failed += 1;
        }
      break;

    case 1:
      for(i = 0; i < pres->res_compound4.resarray.resarray_len; i++)
        {
          pstat_req->nb_nfs41_op += 1;
          pstat_req->stat_op_nfs41[pres->res_compound4.resarray.resarray_val[i].resop].
              total += 1;

          /* All operations's reply structures start with their status, whatever the name of this field */
          if(pres->res_compound4.resarray.resarray_val[i].nfs_resop4_u.opaccess.status ==
             NFS4_OK)
            pstat_req->stat_op_nfs41[pres->res_compound4.resarray.resarray_val[i].resop].
                success += 1;
          else
            pstat_req->stat_op_nfs41[pres->res_compound4.resarray.resarray_val[i].resop].
                failed += 1;
        }

      break;

    default:
      /* Bad parameter */
      return -1;
    }
  return 0;
}                               /* nfs4_op_stat_update */
