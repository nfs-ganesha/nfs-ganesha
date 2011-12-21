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
 * \file    nfs_proto_tools.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/31 10:06:00 $
 * \version $Revision: 1.48 $
 * \brief   A set of functions used to managed NFS.
 *
 * nfs_proto_tools.c -  A set of functions used to managed NFS.
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
#include <pwd.h>
#include <grp.h>
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
#include "nfs_tools.h"
#include "nfs_file_handle.h"
#include "nfs_proto_tools.h"
#include "nfs4_acls.h"

#ifdef _USE_NFS4_ACL
/* Define mapping of NFS4 who name and type. */
static struct {
  char *string;
  int   stringlen;
  int type;
} whostr_2_type_map[] = {
  {
    .string    = "OWNER@",
    .stringlen = sizeof("OWNER@") - 1,
    .type      = FSAL_ACE_SPECIAL_OWNER,
  },
  {
    .string    = "GROUP@",
    .stringlen = sizeof("GROUP@") - 1,
    .type      = FSAL_ACE_SPECIAL_GROUP,
  },
  {
    .string    = "EVERYONE@",
    .stringlen = sizeof("EVERYONE@") - 1,
    .type      = FSAL_ACE_SPECIAL_EVERYONE,
  },
};
#endif                          /* _USE_NFS4_ACL */

/**
 *
 * nfs_FhandleToStr: Converts a file handle to a string representation.
 * 
 * Converts a file handle to a string representation.
 *
 * @param rq_vers  [IN]    version of the NFS protocol to be used 
 * @param pfh2     [IN]    NFSv2 file handle or NULL 
 * @param pfh3     [IN]    NFSv3 file handle or NULL 
 * @param pfh4     [IN]    NFSv4 file handle or NULL 
 * @param str      [OUT]   string version of handle
 *
 */
void nfs_FhandleToStr(u_long     rq_vers,
                      fhandle2  *pfh2,
                      nfs_fh3   *pfh3,
                      nfs_fh4   *pfh4,
                      char      *str)
{

  switch (rq_vers)
    {
    case NFS_V4:
      sprint_fhandle4(str, pfh4);
      break;

    case NFS_V3:
      sprint_fhandle3(str, pfh3);
      break;

    case NFS_V2:
      sprint_fhandle2(str, pfh2);
      break;
    }
}                               /* nfs_FhandleToStr */

/**
 *
 * nfs_FhandleToCache: Gets a cache entry using a file handle (v2/3/4) as input.
 * 
 * Gets a cache entry using a file handle (v2/3/4) as input.
 *
 * @param rq_vers  [IN]    version of the NFS protocol to be used 
 * @param pfh2     [IN]    NFSv2 file handle or NULL 
 * @param pfh3     [IN]    NFSv3 file handle or NULL 
 * @param pfh4     [IN]    NFSv4 file handle or NULL 
 * @param pstatus2 [OUT]   pointer to NFSv2 status or NULL
 * @param pstatus3 [OUT]   pointer to NFSv3 status or NULL
 * @param pstatus4 [OUT]   pointer to NFSv4 status or NULL
 * @param pattr    [OUT]   FSAL attributes related to this cache entry
 * @param pcontext    [IN]    client's FSAL credentials
 * @param pclient  [IN]    client's ressources to be used for accessing the Cache Inode
 * @param ht       [INOUT] Hash Table used to address the Cache Inode 
 * @param prc      [OUT]   internal status for the request (NFS_REQ_DROP or NFS_REQ_OK)
 *
 * @return a pointer to the related pentry if successful, NULL is returned in case of a failure.
 *
 */
cache_entry_t *nfs_FhandleToCache(u_long rq_vers,
                                  fhandle2 * pfh2,
                                  nfs_fh3 * pfh3,
                                  nfs_fh4 * pfh4,
                                  nfsstat2 * pstatus2,
                                  nfsstat3 * pstatus3,
                                  nfsstat4 * pstatus4,
                                  fsal_attrib_list_t * pattr,
                                  fsal_op_context_t * pcontext,
                                  cache_inode_client_t * pclient,
                                  hash_table_t * ht, int *prc)
{
  cache_inode_fsal_data_t fsal_data;
  cache_inode_status_t cache_status;
  cache_entry_t *pentry = NULL;
  fsal_attrib_list_t attr;
  short exportid;

  /* Default behaviour */
  *prc = NFS_REQ_OK;

  memset( (char *)&fsal_data, 0, sizeof( fsal_data ) ) ;
  switch (rq_vers)
    {
    case NFS_V4:
      if(!nfs4_FhandleToFSAL(pfh4, &fsal_data.handle, pcontext))
        {
          *prc = NFS_REQ_DROP;
          *pstatus4 = NFS4ERR_BADHANDLE;
          return NULL;
        }
      exportid = nfs4_FhandleToExportId(pfh4);
      break;

    case NFS_V3:
      if(!nfs3_FhandleToFSAL(pfh3, &fsal_data.handle, pcontext))
        {
          *prc = NFS_REQ_DROP;
          *pstatus3 = NFS3ERR_BADHANDLE;
          return NULL;
        }
      exportid = nfs3_FhandleToExportId(pfh3);
      break;

    case NFS_V2:
      if(!nfs2_FhandleToFSAL(pfh2, &fsal_data.handle, pcontext))
        {
          *prc = NFS_REQ_DROP;
          *pstatus2 = NFSERR_STALE;
          return NULL;
        }
      exportid = nfs2_FhandleToExportId(pfh2);
      break;
    }
  fsal_data.cookie = DIR_START;

  print_buff(COMPONENT_FILEHANDLE, (char *)&fsal_data.handle, sizeof(fsal_data.handle));

  if((pentry = cache_inode_get(&fsal_data,
                               CACHE_INODE_JOKER_POLICY,
                               &attr, ht, pclient, pcontext, &cache_status)) == NULL)
    {
      switch (rq_vers)
        {
        case NFS_V4:
          *pstatus4 = NFS4ERR_STALE;
          break;

        case NFS_V3:
          *pstatus3 = NFS3ERR_STALE;
          break;

        case NFS_V2:
          *pstatus2 = NFSERR_STALE;
          break;
        }
      *prc = NFS_REQ_OK;
      return NULL;
    }

  if(pattr != NULL)
    *pattr = attr;

  return pentry;
}                               /* nfs_FhandleToCache */

/**
 *
 * nfs_SetPostOpAttr: Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 * 
 * @param pexport    [IN]  the related export entry
 * @param pfsal_attr [IN]  FSAL attributes
 * @param pattr      [OUT] NFSv3 PostOp structure attributes.
 *
 * @return 0 in all cases (making it a void function maybe a good idea)
 *
 */
int nfs_SetPostOpAttr(fsal_op_context_t * pcontext,
                      exportlist_t * pexport,
                      cache_entry_t * pentry,
                      fsal_attrib_list_t * pfsal_attr, post_op_attr * presult)
{
  if(pentry == NULL)
    {
      presult->attributes_follow = FALSE;
      return 0;
    }

  if(pfsal_attr == NULL)
    {
      presult->attributes_follow = FALSE;
      return 0;
    }

  if(nfs3_FSALattr_To_Fattr(pexport, pfsal_attr, &(presult->post_op_attr_u.attributes))
     == 0)
    presult->attributes_follow = FALSE;
  else
    presult->attributes_follow = TRUE;

  return 0;
}                               /* nfs_SetPostOpAttr */

/**
 *
 * nfs_SetPreOpAttr: Converts FSAL Attributes to NFSv3 PreOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PreOp Attributes structure.
 * 
 * @param pfsal_attr [IN]  FSAL attributes.
 * @param pattr      [OUT] NFSv3 PreOp structure attributes.
 *
 * @return nothing (void function)
 *
 */
void nfs_SetPreOpAttr(fsal_attrib_list_t * pfsal_attr, pre_op_attr * pattr)
{
  if(pfsal_attr == NULL)
    {
      pattr->attributes_follow = FALSE;
    }
  else
    {
      pattr->pre_op_attr_u.attributes.size = pfsal_attr->filesize;

      pattr->pre_op_attr_u.attributes.mtime.seconds = pfsal_attr->mtime.seconds;
      pattr->pre_op_attr_u.attributes.mtime.nseconds = pfsal_attr->mtime.nseconds;

      pattr->pre_op_attr_u.attributes.ctime.seconds = pfsal_attr->ctime.seconds;
      pattr->pre_op_attr_u.attributes.ctime.nseconds = pfsal_attr->ctime.nseconds;

      pattr->attributes_follow = TRUE;
    }
}                               /* nfs_SetPreOpAttr */

/**
 * 
 * nfs_SetWccData: Sets NFSv3 Weak Cache Coherency structure.
 *
 * Sets NFSv3 Weak Cache Coherency structure.
 *
 * @param pcontext        [IN]  credentials
 * @param pentry       [IN]  related pentry
 * @param pbefore_attr [IN]  the attributes before the operation.
 * @param pafter_attr  [IN]  the attributes after the operation
 * @param pwcc_data    [OUT] the Weak Cache Coherency structure 
 *
 * @return nothing (void function).
 *
 */
void nfs_SetWccData(fsal_op_context_t * pcontext,
                    exportlist_t * pexport,
                    cache_entry_t * pentry,
                    fsal_attrib_list_t * pbefore_attr,
                    fsal_attrib_list_t * pafter_attr, wcc_data * pwcc_data)
{
  /* Build directory pre operation attributes */
  nfs_SetPreOpAttr(pbefore_attr, &(pwcc_data->before));

  /* Build directory post operation attributes */
  nfs_SetPostOpAttr(pcontext, pexport, pentry, pafter_attr, &(pwcc_data->after));

  return;
}                               /* nfs_SetWccData */

/**
 *
 * nfs_RetryableError: Indicates if an error is retryable or not.
 *
 * Indicates if an error is retryable or not.
 *
 * @param cache_status [IN] input Cache Inode Status value, to be tested.
 *
 * @return TRUE if retryable, FALSE otherwise.
 *
 * @todo: Not implemented for NOW BUGAZEOMEU 
 *
 */
int nfs_RetryableError(cache_inode_status_t cache_status)
{
  switch (cache_status)
    {
    case CACHE_INODE_IO_ERROR:
      if(nfs_param.core_param.drop_io_errors)
        {
          /* Drop the request */
          return TRUE;
        }
      else
        {
          /* Propagate error to the client */
          return FALSE;
        }
      break;

    case CACHE_INODE_INVALID_ARGUMENT:
      if(nfs_param.core_param.drop_inval_errors)
        {
          /* Drop the request */
          return TRUE;
        }
      else
        {
          /* Propagate error to the client */
          return FALSE;
        }
      break;

    case CACHE_INODE_FSAL_DELAY:
      if(nfs_param.core_param.drop_delay_errors)
        {
          /* Drop the request */
          return TRUE;
        }
      else
        {
          /* Propagate error to the client */
          return FALSE;
        }
      break;

    case CACHE_INODE_SUCCESS:
      LogCrit(COMPONENT_NFSPROTO,
              "Possible implementation error: CACHE_INODE_SUCCESS managed as an error");
      return FALSE;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_INSERT_ERROR:
      /* Internal error, should be dropped and retryed */
      return TRUE;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
    case CACHE_INODE_BAD_TYPE:
    case CACHE_INODE_ENTRY_EXISTS:
    case CACHE_INODE_DIR_NOT_EMPTY:
    case CACHE_INODE_NOT_FOUND:
    case CACHE_INODE_FSAL_EACCESS:
    case CACHE_INODE_IS_A_DIRECTORY:
    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_NO_SPACE_LEFT:
    case CACHE_INODE_CACHE_CONTENT_ERROR:
    case CACHE_INODE_CACHE_CONTENT_EXISTS:
    case CACHE_INODE_CACHE_CONTENT_EMPTY:
    case CACHE_INODE_READ_ONLY_FS:
    case CACHE_INODE_FSAL_ESTALE:
    case CACHE_INODE_FSAL_ERR_SEC:
    case CACHE_INODE_QUOTA_EXCEEDED:
    case CACHE_INODE_NOT_SUPPORTED:
    case CACHE_INODE_NAME_TOO_LONG:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
    case CACHE_INODE_BAD_COOKIE:
    case CACHE_INODE_FILE_BIG:
      /* Non retryable error, return error to client */
      return FALSE;
      break;
    }

  /* Should never reach this */
  LogDebug(COMPONENT_NFSPROTO,
           "cache_inode_status=%u not managed properly in nfs_RetryableError, line %u should never be reached",
           cache_status, __LINE__);
  return FALSE;
}

void nfs_SetFailedStatus(fsal_op_context_t * pcontext,
                         exportlist_t * pexport,
                         int version,
                         cache_inode_status_t status,
                         nfsstat2 * pstatus2,
                         nfsstat3 * pstatus3,
                         cache_entry_t * pentry0,
                         post_op_attr * ppost_op_attr,
                         cache_entry_t * pentry1,
                         fsal_attrib_list_t * ppre_vattr1,
                         wcc_data * pwcc_data1,
                         cache_entry_t * pentry2,
                         fsal_attrib_list_t * ppre_vattr2, wcc_data * pwcc_data2)
{
  switch (version)
    {
    case NFS_V2:
      if(status != CACHE_INODE_SUCCESS) /* Should not use success to address a failed status */
        *pstatus2 = nfs2_Errno(status);
      break;

    case NFS_V3:
      if(status != CACHE_INODE_SUCCESS) /* Should not use success to address a failed status */
        *pstatus3 = nfs3_Errno(status);

      if(ppost_op_attr != NULL)
        nfs_SetPostOpAttr(pcontext, pexport, pentry0, NULL, ppost_op_attr);

      if(pwcc_data1 != NULL)
        nfs_SetWccData(pcontext, pexport, pentry1, ppre_vattr1, NULL, pwcc_data1);

      if(pwcc_data2 != NULL)
        nfs_SetWccData(pcontext, pexport, pentry2, ppre_vattr2, NULL, pwcc_data2);
      break;

    }
}

#ifdef _USE_NFS4_ACL
/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_special_user(int who, char *attrvalsBuffer,
                                        u_int *LastOffset)
{
  int rc = 0;
  int i;
  u_int utf8len = 0;
  u_int deltalen = 0;

  for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++)
    {
      if (whostr_2_type_map[i].type == who)
        {
          if(whostr_2_type_map[i].stringlen % 4 == 0)
            deltalen = 0;
          else
            deltalen = 4 - whostr_2_type_map[i].stringlen % 4;

          utf8len = htonl(whostr_2_type_map[i].stringlen + deltalen);
          memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
          *LastOffset += sizeof(int);

          memcpy((char *)(attrvalsBuffer + *LastOffset), whostr_2_type_map[i].string,
                 whostr_2_type_map[i].stringlen);
          *LastOffset += whostr_2_type_map[i].stringlen;

          /* Pad with zero to keep xdr alignement */
          if(deltalen != 0)
            memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
          *LastOffset += deltalen;

          /* Found a matched one. */
          rc = 1;
          break;
        }
    }

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_group_name(fsal_gid_t gid, char *attrvalsBuffer,
                                      u_int *LastOffset)
{
  int rc = 0;
  char name[MAXNAMLEN];
  u_int utf8len = 0;
  u_int stringlen = 0;
  u_int deltalen = 0;

  rc = gid2name(name, &gid);
  LogFullDebug(COMPONENT_NFS_V4,
               "encode gid2name = %s, strlen = %llu",
               name, (long long unsigned int)strlen(name));
  if(rc == 0)  /* Failure. */
    {
      /* Encode gid itself without @. */
      sprintf(name, "%u", gid);
    }

  stringlen = strlen(name);
  if(stringlen % 4 == 0)
    deltalen = 0;
  else
    deltalen = 4 - (stringlen % 4);

  utf8len = htonl(stringlen + deltalen);
  memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
  *LastOffset += sizeof(int);

  memcpy((char *)(attrvalsBuffer + *LastOffset), name, stringlen);
  *LastOffset += stringlen;

  /* Pad with zero to keep xdr alignement */
  if(deltalen != 0)
    memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
  *LastOffset += deltalen;

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl_user_name(int whotype, fsal_uid_t uid,
                                     char *attrvalsBuffer, u_int *LastOffset)
{
  int rc = 0;
  char name[MAXNAMLEN];
  u_int utf8len = 0;
  u_int stringlen = 0;
  u_int deltalen = 0;

  /* Encode special user first. */
  if (whotype != FSAL_ACE_NORMAL_WHO)
    {
      rc = nfs4_encode_acl_special_user(uid, attrvalsBuffer, LastOffset);
      if(rc == 1)  /* Success. */
        return rc;
    }

  /* Encode normal user or previous user we failed to encode as special user. */
  rc = uid2name(name, &uid);
  LogFullDebug(COMPONENT_NFS_V4,
               "econde uid2name = %s, strlen = %llu",
               name, (long long unsigned int)strlen(name));
  if(rc == 0)  /* Failure. */
    {
      /* Encode uid itself without @. */
      sprintf(name, "%u", uid);
    }

  stringlen = strlen(name);
  if(stringlen % 4 == 0)
    deltalen = 0;
  else
    deltalen = 4 - (stringlen % 4);

  utf8len = htonl(stringlen + deltalen);
  memcpy((char *)(attrvalsBuffer + *LastOffset), &utf8len, sizeof(int));
  *LastOffset += sizeof(int);

  memcpy((char *)(attrvalsBuffer + *LastOffset), name, stringlen);
  *LastOffset += stringlen;

  /* Pad with zero to keep xdr alignement */
  if(deltalen != 0)
    memset((char *)(attrvalsBuffer + *LastOffset), 0, deltalen);
  *LastOffset += deltalen;

  return rc;
}

/* Following idmapper function conventions, return 1 if successful, 0 otherwise. */
static int nfs4_encode_acl(fsal_attrib_list_t * pattr, char *attrvalsBuffer, u_int *LastOffset)
{
  int rc = 0;
  uint32_t naces, type, flag, access_mask, whotype;
  fsal_ace_t *pace;

  if(pattr->acl)
    {
      LogFullDebug(COMPONENT_NFS_V4,
                   "GATTR: Number of ACEs = %u",
                   pattr->acl->naces);

      /* Encode number of ACEs. */
      naces = htonl(pattr->acl->naces);
      memcpy((char *)(attrvalsBuffer + *LastOffset), &naces, sizeof(uint32_t));
      *LastOffset += sizeof(uint32_t);

      /* Encode ACEs. */
      for(pace = pattr->acl->aces; pace < pattr->acl->aces + pattr->acl->naces; pace++)
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "GATTR: type=0X%x, flag=0X%x, perm=0X%x",
                       pace->type, pace->flag, pace->perm);

          type = htonl(pace->type);
          flag = htonl(pace->flag);
          access_mask = htonl(pace->perm);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &type, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &flag, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          memcpy((char *)(attrvalsBuffer + *LastOffset), &access_mask, sizeof(uint32_t));
          *LastOffset += sizeof(uint32_t);

          if(IS_FSAL_ACE_GROUP_ID(*pace))  /* Encode group name. */
            {
              rc = nfs4_encode_acl_group_name(pace->who.gid, attrvalsBuffer, LastOffset);
            }
          else
            {
              if(!IS_FSAL_ACE_SPECIAL_ID(*pace))
                {
                  whotype = FSAL_ACE_NORMAL_WHO;
                }
              else
                whotype = pace->who.uid;

              /* Encode special or normal user name. */
              rc = nfs4_encode_acl_user_name(whotype, pace->who.uid, attrvalsBuffer, LastOffset);
            }

          LogFullDebug(COMPONENT_NFS_V4,
                       "GATTR: special = %u, %s = %u",
                       IS_FSAL_ACE_SPECIAL_ID(*pace),
                       IS_FSAL_ACE_GROUP_ID(*pace) ? "gid" : "uid",
                       IS_FSAL_ACE_GROUP_ID(*pace) ? pace->who.gid : pace->who.uid);

        }
    }
  else
    {
      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_encode_acl: no acl available");

      fattr4_acl acl;
      acl.fattr4_acl_len = htonl(0);
      memcpy((char *)(attrvalsBuffer + *LastOffset), &acl, sizeof(fattr4_acl));
      *LastOffset += fattr4tab[FATTR4_ACL].size_fattr4;
    }

  return rc;
}
#endif                          /* _USE_NFS4_ACL */

/**
 *
 * nfs4_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * Converts FSAL Attributes to NFSv4 Fattr buffer.
 *
 * @param pexport [IN]  the related export entry.
 * @param pattr   [IN]  pointer to FSAL attributes.
 * @param Fattr   [OUT] NFSv4 Fattr buffer
 * @param data    [IN]  NFSv4 compoud request's data.
 * @param Bitmap  [OUT] NFSv4 attributes bitmap to the Fattr buffer.
 * 
 * @return -1 if failed, 0 if successful.
 *
 */

int nfs4_FSALattr_To_Fattr(exportlist_t * pexport,
                           fsal_attrib_list_t * pattr,
                           fattr4 * Fattr,
                           compound_data_t * data, nfs_fh4 * objFH, bitmap4 * Bitmap)
{
  fattr4_type file_type;
  fattr4_link_support link_support;
  fattr4_symlink_support symlink_support;
  fattr4_fh_expire_type expire_type;
  fattr4_named_attr named_attr;
  fattr4_unique_handles unique_handles;
  fattr4_archive archive;
  fattr4_cansettime cansettime;
  fattr4_case_insensitive case_insensitive;
  fattr4_case_preserving case_preserving;
  fattr4_chown_restricted chown_restricted;
  fattr4_hidden hidden;
  fattr4_mode file_mode;
  fattr4_no_trunc no_trunc;
  fattr4_numlinks file_numlinks;
  fattr4_rawdev rawdev;
  fattr4_system system;
  fattr4_size file_size;
  fattr4_space_used file_space_used;
  fattr4_fsid fsid;
  fattr4_time_access time_access;
  fattr4_time_modify time_modify;
  fattr4_time_metadata time_metadata;
  fattr4_time_delta time_delta;
  fattr4_change file_change;
  fattr4_fileid file_id;
  fattr4_owner file_owner;
  fattr4_owner_group file_owner_group;
  fattr4_space_avail space_avail;
  fattr4_space_free space_free;
  fattr4_space_total space_total;
  fattr4_files_avail files_avail;
  fattr4_files_free files_free;
  fattr4_files_total files_total;
  fattr4_lease_time lease_time;
  fattr4_time_backup time_backup;
  fattr4_time_create time_create;
  fattr4_maxfilesize max_filesize;
  fattr4_supported_attrs supported_attrs;
  fattr4_maxread maxread;
  fattr4_maxwrite maxwrite;
  fattr4_maxname maxname;
  fattr4_maxlink maxlink;
  fattr4_homogeneous homogeneous;
  fattr4_aclsupport aclsupport;
#ifndef _USE_NFS4_ACL
  fattr4_acl acl;
#endif
  fattr4_rdattr_error rdattr_error;
  fattr4_quota_avail_hard quota_avail_hard;
  fattr4_quota_avail_soft quota_avail_soft;
  fattr4_quota_used quota_used;
  fattr4_time_modify_set __attribute__ ((__unused__)) time_modify_set;
  fattr4_time_access_set __attribute__ ((__unused__)) time_access_set;
#ifdef _USE_NFS4_1
  fattr4_fs_layout_types layout_types;
  layouttype4 layouts[1];
#endif

  u_int tmp_int;
  char tmp_buff[1024];

  uint32_t attribute_to_set = 0;

  u_int fhandle_len = 0;
  uint32_t supported_attrs_len;
  uint32_t supported_attrs_val;
  u_int LastOffset;
  u_int len = 0, off = 0;       /* Use for XDR alignment */
  int op_attr_success = 0;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_FSALattr_To_Fattr";

#ifdef _USE_NFS4_1
  unsigned int attrvalslist_supported[FATTR4_FS_CHARSET_CAP];
  uint32_t attrmasklist[FATTR4_FS_CHARSET_CAP]; /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
  uint32_t attrvalslist[FATTR4_FS_CHARSET_CAP]; /* List cannot be longer than FATTR4_FS_CHARSET_CAP */
#else
  unsigned int attrvalslist_supported[FATTR4_MOUNTED_ON_FILEID];
  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrvalslist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
#endif
  uint32_t attrmasklen = 0;
  char attrvalsBuffer[ATTRVALS_BUFFLEN];

  uint_t i = 0;
  uint_t j = 0;
  uint_t k = 0;
  uint_t c = 0;

  cache_inode_status_t cache_status;

  int statfscalled = 0;
  fsal_staticfsinfo_t * pstaticinfo = NULL ;
  fsal_dynamicfsinfo_t dynamicinfo;

  if( data != NULL ) /* data can be NULL if called from FSAL_PROXY operating as a client */
    pstaticinfo = data->pcontext->export_context->fe_static_fs_info;

#ifdef _USE_NFS4_ACL
  int rc;
#endif

  /* basic init */
  memset(attrvalsBuffer, 0, NFS4_ATTRVALS_BUFFLEN);
#ifdef _USE_NFS4_1
  memset((uint32_t *) attrmasklist, 0, FATTR4_FS_CHARSET_CAP * sizeof(uint32_t));
  memset((uint32_t *) attrvalslist, 0, FATTR4_FS_CHARSET_CAP * sizeof(uint32_t));
#else
  memset((uint32_t *) attrmasklist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(uint32_t));
  memset((uint32_t *) attrvalslist, 0, FATTR4_MOUNTED_ON_FILEID * sizeof(uint32_t));
#endif

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(Bitmap, &attrmasklen, attrmasklist);

  /* Once the bitmap has been converted to a list of attribute, manage each attribute */
  Fattr->attr_vals.attrlist4_len = 0;
  LastOffset = 0;
  j = 0;

  for(i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

#ifdef _USE_NFS4_1
      if(attrmasklist[i] > FATTR4_FS_CHARSET_CAP)
#else
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
#endif
        {
          /* Erroneous value... skip */
          continue;
        }
      LogFullDebug(COMPONENT_NFS_V4,
                   "Flag for Operation (Regular) = %d|%d is ON,  name  = %s  reply_size = %d",
                   attrmasklist[i],
                   fattr4tab[attribute_to_set].val,
                   fattr4tab[attribute_to_set].name,
                   fattr4tab[attribute_to_set].size_fattr4);

      op_attr_success = 0;

      /* compute the new size for the fattr4 reply */
      /* This space is to be filled below in the big switch/case statement */
      switch (attribute_to_set)
        {
        case FATTR4_SUPPORTED_ATTRS:
          /* The supported attributes have field ',supported' set in tab fattr4tab, I will proceed in 2 pass 
           * 1st: compute the number of supported attributes
           * 2nd: allocate the replyed bitmap and fill it
           *
           * I do not set a #define to keep the number of supported attributes because I want this parameter
           * to be a consequence of fattr4tab and avoid incoherency */

          /* How many supported attributes ? Compute the result in variable named c */
          c = 0;
#ifdef _USE_NFS4_1
          for(k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_FS_CHARSET_CAP; k++)
#else
          for(k = FATTR4_SUPPORTED_ATTRS; k <= FATTR4_MOUNTED_ON_FILEID; k++)
#endif
            {
              if(fattr4tab[k].supported)
                {
                  attrvalslist_supported[c++] = k;
                }
            }

          /* Let set the reply bitmap */
#ifdef _USE_NFS4_1
          if((supported_attrs.bitmap4_val =
              (uint32_t *) Mem_Alloc_Label(3 * sizeof(uint32_t),
                                           "FSALattr_To_Fattr:supported_bitmap")) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 3 * sizeof(uint32_t));
#else
          if((supported_attrs.bitmap4_val =
              (uint32_t *) Mem_Alloc(2 * sizeof(uint32_t))) == NULL)
            return -1;
          memset(supported_attrs.bitmap4_val, 0, 2 * sizeof(uint32_t));
#endif

          nfs4_list_to_bitmap4(&supported_attrs, &c, attrvalslist_supported);

          LogFullDebug(COMPONENT_NFS_V4,
                       "Fattr (regular) supported_attrs(len)=%u -> %u|%u",
                       supported_attrs.bitmap4_len, supported_attrs.bitmap4_val[0],
                       supported_attrs.bitmap4_val[1]);

          /* This kind of operation is always a success */
          op_attr_success = 1;

          /* we store the index */
          supported_attrs_len = htonl(supported_attrs.bitmap4_len);
          memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_len,
                 sizeof(uint32_t));
          LastOffset += sizeof(uint32_t);

          /* And then the data */
          for(k = 0; k < supported_attrs.bitmap4_len; k++)
            {
              supported_attrs_val = htonl(supported_attrs.bitmap4_val[k]);
              memcpy((char *)(attrvalsBuffer + LastOffset), &supported_attrs_val,
                     sizeof(uint32_t));
              LastOffset += sizeof(uint32_t);
            }

          Mem_Free((char *)supported_attrs.bitmap4_val);
          break;

        case FATTR4_TYPE:
          switch (pattr->type)
            {
            case FSAL_TYPE_FILE:
            case FSAL_TYPE_XATTR:
              file_type = htonl(NF4REG);        /* Regular file */
              break;

            case FSAL_TYPE_DIR:
              file_type = htonl(NF4DIR);        /* Directory */
              break;

            case FSAL_TYPE_BLK:
              file_type = htonl(NF4BLK);        /* Special File - block device */
              break;

            case FSAL_TYPE_CHR:
              file_type = htonl(NF4CHR);        /* Special File - character device */
              break;

            case FSAL_TYPE_LNK:
              file_type = htonl(NF4LNK);        /* Symbolic Link */
              break;

            case FSAL_TYPE_SOCK:
              file_type = htonl(NF4SOCK);       /* Special File - socket */
              break;

            case FSAL_TYPE_FIFO:
              file_type = htonl(NF4FIFO);       /* Special File - fifo */
              break;

            case FSAL_TYPE_JUNCTION:
              /* For wanting of a better solution */
              file_type = 0;
              op_attr_success = 0;      /* This was no success */
              break;
            }                   /* switch( pattr->type ) */

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_type, sizeof(fattr4_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FH_EXPIRE_TYPE:
          /* For the moment, we handle only the persistent filehandle */
          if(nfs_param.nfsv4_param.fh_expire == TRUE)
            expire_type = htonl(FH4_VOLATILE_ANY);
          else
            expire_type = htonl(FH4_PERSISTENT);
          memcpy((char *)(attrvalsBuffer + LastOffset), &expire_type,
                 sizeof(expire_type));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHANGE:
          /* a value that change when the object change. I use the file's mtime */
          memset(&file_change, 0, sizeof(changeid4));
          file_change = nfs_htonl64((changeid4) pattr->change);

          memcpy((char *)(attrvalsBuffer + LastOffset), &file_change,
                 sizeof(fattr4_change));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SIZE:
          file_size = nfs_htonl64((fattr4_size) pattr->filesize);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_size, sizeof(fattr4_size));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LINK_SUPPORT:
          /* HPSS NameSpace support hard link */
          link_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &link_support,
                 sizeof(fattr4_link_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYMLINK_SUPPORT:
          /* HPSS NameSpace support symbolic link */
          symlink_support = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &symlink_support,
                 sizeof(fattr4_symlink_support));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NAMED_ATTR:
          /* For this version of the binary, named attributes is not supported */
          named_attr = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &named_attr,
                 sizeof(fattr4_named_attr));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FSID:
          /* The file system id (taken from the configuration file) */
          fsid.major = nfs_htonl64((uint64_t) pexport->filesystem_id.major);
          fsid.minor = nfs_htonl64((uint64_t) pexport->filesystem_id.minor);

          /* If object is a directory attached to a referral, then a different fsid is to be returned
           * to tell the client that a different fs is being crossed */
          if(nfs4_Is_Fh_Referral(objFH))
            {
              fsid.major = ~(nfs_htonl64((uint64_t) pexport->filesystem_id.major));
              fsid.minor = ~(nfs_htonl64((uint64_t) pexport->filesystem_id.minor));
            }

          memcpy((char *)(attrvalsBuffer + LastOffset), &fsid, sizeof(fattr4_fsid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_UNIQUE_HANDLES:
          /* Filehandles are unique */
          unique_handles = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &unique_handles,
                 sizeof(fattr4_unique_handles));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_LEASE_TIME:
          /* lease_time = htonl( (fattr4_lease_time)pstaticinfo->lease_time.seconds ) ; */
          lease_time = htonl(nfs_param.nfsv4_param.lease_lifetime);
          memcpy((char *)(attrvalsBuffer + LastOffset), &lease_time,
                 sizeof(fattr4_lease_time));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RDATTR_ERROR:
          rdattr_error = htonl(NFS4_OK);        /* By default, READDIR call may use a different value */
          memcpy((char *)(attrvalsBuffer + LastOffset), &rdattr_error,
                 sizeof(fattr4_rdattr_error));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ACL:
#ifdef _USE_NFS4_ACL
          rc = nfs4_encode_acl(pattr, attrvalsBuffer, &LastOffset);
          if(rc == 0)  /* uid/gid mapping to a string failure */
            LogEvent(COMPONENT_NFS_V4, "Failed to map uid/gid to a string.");
#else
          memset(&acl, 0, sizeof(acl));
          memcpy((char *)(attrvalsBuffer + LastOffset), &acl, sizeof(fattr4_acl));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
#endif
          op_attr_success = 1;
          break;

        case FATTR4_ACLSUPPORT:
#ifdef _USE_NFS4_ACL
          aclsupport = htonl(ACL4_SUPPORT_ALLOW_ACL | ACL4_SUPPORT_DENY_ACL);
#else
          aclsupport = htonl(0);
#endif
          memcpy((char *)(attrvalsBuffer + LastOffset), &aclsupport,
                 sizeof(fattr4_aclsupport));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_ARCHIVE:
          /* Archive flag is not supported */
          archive = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &archive, sizeof(fattr4_archive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CANSETTIME:
          /* The time can be set on files */
          cansettime = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &cansettime,
                 sizeof(fattr4_cansettime));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_INSENSITIVE:
          case_insensitive = htonl(pstaticinfo->case_insensitive);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_insensitive,
                 sizeof(fattr4_case_insensitive));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CASE_PRESERVING:
          case_preserving = htonl(pstaticinfo->case_preserving);
          memcpy((char *)(attrvalsBuffer + LastOffset), &case_preserving,
                 sizeof(fattr4_case_preserving));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_CHOWN_RESTRICTED:
          /* chown is restricted to root */
          chown_restricted = htonl(pstaticinfo->chown_restricted);
          memcpy((char *)(attrvalsBuffer + LastOffset), &chown_restricted,
                 sizeof(fattr4_chown_restricted));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILEHANDLE:
          /* Return the file handle */
          fhandle_len = htonl(objFH->nfs_fh4_len);
          memcpy((char *)(attrvalsBuffer + LastOffset), &fhandle_len, sizeof(u_int));
          LastOffset += sizeof(u_int);

          memcpy((char *)(attrvalsBuffer + LastOffset),
                 objFH->nfs_fh4_val, objFH->nfs_fh4_len);
          LastOffset += objFH->nfs_fh4_len;

          /* XDR's special stuff for 32-bit alignment */
          len = objFH->nfs_fh4_len;
          off = 0;
          while((len + off) % 4 != 0)
            {
              char c = '\0';

              off += 1;
              memset((char *)(attrvalsBuffer + LastOffset), (int)c, 1);
              LastOffset += 1;
            }

          op_attr_success = 1;
          break;

        case FATTR4_FILEID:
          /* The analog to the inode number. RFC3530 says "a number uniquely identifying the file within the filesystem" 
           * I use hpss_GetObjId to extract this information from the Name Server's handle */
          file_id = nfs_htonl64(pattr->fileid);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_AVAIL:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          files_avail = nfs_htonl64((fattr4_files_avail) dynamicinfo.avail_files);
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_avail,
                 sizeof(fattr4_files_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_FREE:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          files_free = nfs_htonl64((fattr4_files_avail) dynamicinfo.free_files);
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_free,
                 sizeof(fattr4_files_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FILES_TOTAL:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          files_total = nfs_htonl64((fattr4_files_avail) dynamicinfo.total_files);
          memcpy((char *)(attrvalsBuffer + LastOffset), &files_total,
                 sizeof(fattr4_files_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_FS_LOCATIONS:
          if(data->current_entry->internal_md.type != DIRECTORY)
            {
              op_attr_success = 0;
              break;
            }

          if(!nfs4_referral_str_To_Fattr_fs_location
             (data->current_entry->object.dir.referral, tmp_buff, &tmp_int))
            {
              op_attr_success = 0;
              break;
            }

          memcpy((char *)(attrvalsBuffer + LastOffset), tmp_buff, tmp_int);
          LastOffset += tmp_int;
          op_attr_success = 1;
          break;

        case FATTR4_HIDDEN:
          /* There are no hidden file in HPSS */
          hidden = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &hidden, sizeof(fattr4_hidden));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_HOMOGENEOUS:
          /* Unix semantic is homogeneous (all objects have the same kind of attributes) */
          homogeneous = htonl(TRUE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &homogeneous,
                 sizeof(fattr4_homogeneous));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXFILESIZE:
          max_filesize = nfs_htonl64((fattr4_maxfilesize) FSINFO_MAX_FILESIZE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &max_filesize,
                 sizeof(fattr4_maxfilesize));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXLINK:
          maxlink = htonl(pstaticinfo->maxlink);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxlink, sizeof(fattr4_maxlink));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXNAME:
          maxname = htonl((fattr4_maxname) pstaticinfo->maxnamelen);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxname, sizeof(fattr4_maxname));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXREAD:
          maxread = nfs_htonl64((fattr4_maxread) pstaticinfo->maxread);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxread, sizeof(fattr4_maxread));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MAXWRITE:
          maxwrite = nfs_htonl64((fattr4_maxwrite) pstaticinfo->maxwrite);
          memcpy((char *)(attrvalsBuffer + LastOffset), &maxwrite,
                 sizeof(fattr4_maxwrite));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_MIMETYPE:
          memset((char *)(attrvalsBuffer + LastOffset), 0,
                 sizeof(fattr4_mimetype));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;  /* No supported for the moment */
          break;

        case FATTR4_MODE:
          /* file_mode = (fattr4_mode) htonl(fsal2unix_mode(pattr->mode)) ; */
          file_mode = htonl((fattr4_mode) fsal2unix_mode(pattr->mode));
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_mode, sizeof(fattr4_mode));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NO_TRUNC:
          /* File's names are not truncated, an error is returned is name is too long */
          no_trunc = htonl(pstaticinfo->no_trunc);
          memcpy((char *)(attrvalsBuffer + LastOffset), &no_trunc,
                 sizeof(fattr4_no_trunc));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_NUMLINKS:
          /* Reply the number of links found in vattr structure */
          file_numlinks = htonl((fattr4_numlinks) pattr->numlinks);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_numlinks,
                 sizeof(fattr4_numlinks));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_OWNER:
          /* Return the uid as a human readable utf8 string */
          if(uid2utf8(pattr->owner, &file_owner) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner.utf8string_len % 4;

              utf8len = htonl(file_owner.utf8string_len + deltalen);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner.utf8string_val, file_owner.utf8string_len);
              LastOffset += file_owner.utf8string_len;

              /* Free what was allocated by uid2utf8 */
              Mem_Free((char *)file_owner.utf8string_val);

              /* Pad with zero to keep xdr alignement */
              if(deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;

            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_OWNER_GROUP:
          /* Return the gid as a human-readable utf8 string */
          if(gid2utf8(pattr->group, &file_owner_group) == 0)
            {
              u_int utf8len = 0;
              u_int deltalen = 0;

              /* Take care of 32 bits alignment */
              if(file_owner_group.utf8string_len % 4 == 0)
                deltalen = 0;
              else
                deltalen = 4 - file_owner_group.utf8string_len % 4;

              utf8len = htonl(file_owner_group.utf8string_len + deltalen);
              memcpy((char *)(attrvalsBuffer + LastOffset), &utf8len, sizeof(u_int));
              LastOffset += sizeof(u_int);

              memcpy((char *)(attrvalsBuffer + LastOffset),
                     file_owner_group.utf8string_val, file_owner_group.utf8string_len);
              LastOffset += file_owner_group.utf8string_len;

              /* Free what was used for utf8 conversion */
              Mem_Free((char *)file_owner_group.utf8string_val);

              /* Pad with zero to keep xdr alignement */
              if(deltalen != 0)
                memset((char *)(attrvalsBuffer + LastOffset), 0, deltalen);
              LastOffset += deltalen;

              op_attr_success = 1;

            }
          else
            op_attr_success = 0;
          break;

        case FATTR4_QUOTA_AVAIL_HARD:
          quota_avail_hard = nfs_htonl64((fattr4_quota_avail_hard) NFS_V4_MAX_QUOTA_HARD);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_hard,
                 sizeof(fattr4_quota_avail_hard));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_AVAIL_SOFT:
          quota_avail_soft = nfs_htonl64((fattr4_quota_avail_soft) NFS_V4_MAX_QUOTA_SOFT);    /** @todo: not the right answer, actual quotas should be implemented */
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_avail_soft,
                 sizeof(fattr4_quota_avail_soft));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_QUOTA_USED:
          quota_used = nfs_htonl64((fattr4_quota_used) pattr->filesize);
          memcpy((char *)(attrvalsBuffer + LastOffset), &quota_used,
                 sizeof(fattr4_quota_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_RAWDEV:
          rawdev.specdata1 = htonl(pattr->rawdev.major);
          rawdev.specdata2 = htonl(pattr->rawdev.minor);
          memcpy((char *)(attrvalsBuffer + LastOffset), &rawdev, sizeof(fattr4_rawdev));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_AVAIL:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          space_avail = nfs_htonl64((fattr4_space_avail) dynamicinfo.avail_bytes);
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_avail,
                 sizeof(fattr4_space_avail));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_FREE:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          space_free = nfs_htonl64((fattr4_space_free) dynamicinfo.free_bytes);
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_free,
                 sizeof(fattr4_space_free));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_TOTAL:
          if(!statfscalled)
            {
              if((cache_status = cache_inode_statfs(data->current_entry,
                                                    &dynamicinfo,
                                                    data->pcontext,
                                                    &cache_status)) !=
                 CACHE_INODE_SUCCESS)
                {
                  op_attr_success = 0;
                  break;
                }
              else
                statfscalled = 1;
            }
          space_total = nfs_htonl64((fattr4_space_total) dynamicinfo.total_bytes);
          memcpy((char *)(attrvalsBuffer + LastOffset), &space_total,
                 sizeof(fattr4_space_total));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SPACE_USED:
          /* the number of bytes on the filesystem used by the object, which is slightly different 
           * from the file's size (there can be hole in the file) */
          file_space_used = nfs_htonl64((fattr4_space_used) pattr->spaceused);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_space_used,
                 sizeof(fattr4_space_used));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_SYSTEM:
          /* This is not a windows system File-System with respect to the regarding API */
          system = htonl(FALSE);
          memcpy((char *)(attrvalsBuffer + LastOffset), &system, sizeof(fattr4_system));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS:
          /* This will contain the object's time os last access, the 'atime' in the Unix semantic */
          memset(&(time_access.seconds), 0, sizeof(int64_t));
          time_access.seconds = nfs_htonl64((int64_t) pattr->atime.seconds);
          time_access.nseconds = htonl((uint32_t) pattr->atime.nseconds);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_access,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_ACCESS_SET:
#ifdef _USE_PROXY
          op_attr_success = 0;  /* should never be used here outside FSAL_PROXY */
#else
          time_access_set.set_it = htonl(SET_TO_CLIENT_TIME4);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_access_set.set_it, sizeof(time_how4));
          LastOffset += sizeof(time_how4);

          time_access_set.settime4_u.time.seconds =
              nfs_htonl64((int64_t) pattr->mtime.seconds);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_access_set.settime4_u.time.seconds, sizeof(int64_t));
          LastOffset += sizeof(int64_t);

          time_access_set.settime4_u.time.nseconds = htonl(pattr->mtime.nseconds);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_access_set.settime4_u.time.nseconds, sizeof(uint32_t));
          LastOffset += sizeof(uint32_t);

          op_attr_success = 1;
#endif
          break;

        case FATTR4_TIME_BACKUP:
          /* No time backup, return unix's beginning of time */
          time_backup.seconds = nfs_htonl64(0LL);
          time_backup.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_backup,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_CREATE:
          /* No time create, return unix's beginning of time */
          time_create.seconds = nfs_htonl64(0LL);
          time_create.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_create,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_DELTA:
          /* According to RFC3530, this is "the smallest usefull server time granularity", I set this to 1s */
          time_delta.seconds = nfs_htonl64(1LL);
          time_delta.nseconds = htonl(0);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_delta,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_METADATA:
          /* The time for the last metadata operation, the ctime in the unix's semantic */
          memset(&(time_metadata.seconds), 0, sizeof(int64_t));
          time_metadata.seconds = nfs_htonl64((int64_t) pattr->ctime.seconds);
          time_metadata.nseconds = htonl(pattr->ctime.nseconds);
          memcpy((char *)(attrvalsBuffer + LastOffset), &time_metadata,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY:
          /* The time for the last modify operation, the mtime in the unix's semantic */
          memset(&(time_modify.seconds), 0, sizeof(int64_t));
          time_modify.seconds = nfs_htonl64((int64_t) pattr->mtime.seconds);
          time_modify.nseconds = htonl(pattr->mtime.nseconds);

          memcpy((char *)(attrvalsBuffer + LastOffset), &time_modify,
                 fattr4tab[attribute_to_set].size_fattr4);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

        case FATTR4_TIME_MODIFY_SET:
#ifndef _USE_PROXY
          op_attr_success = 0;  /* should never be used here outside FSAL_PROXY */
#else
          time_modify_set.set_it = htonl(SET_TO_CLIENT_TIME4);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_modify_set.set_it, sizeof(time_how4));
          LastOffset += sizeof(time_how4);

          time_modify_set.settime4_u.time.seconds =
              nfs_htonl64((int64_t) pattr->mtime.seconds);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_modify_set.settime4_u.time.seconds, sizeof(int64_t));
          LastOffset += sizeof(int64_t);

          time_modify_set.settime4_u.time.nseconds = htonl(pattr->mtime.nseconds);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &time_modify_set.settime4_u.time.nseconds, sizeof(uint32_t));
          LastOffset += sizeof(uint32_t);

          op_attr_success = 1;
#endif
          break;

        case FATTR4_MOUNTED_ON_FILEID:
          file_id = nfs_htonl64(pattr->fileid);
          memcpy((char *)(attrvalsBuffer + LastOffset), &file_id, sizeof(fattr4_fileid));
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          op_attr_success = 1;
          break;

#ifdef _USE_NFS4_1
        case FATTR4_FS_LAYOUT_TYPES:
          layout_types.fattr4_fs_layout_types_len = htonl(1);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 &layout_types.fattr4_fs_layout_types_len, sizeof(u_int));
          LastOffset += sizeof(u_int);

          layout_types.fattr4_fs_layout_types_val = layouts;
          layouts[0] = htonl(LAYOUT4_NFSV4_1_FILES);
          memcpy((char *)(attrvalsBuffer + LastOffset),
                 layout_types.fattr4_fs_layout_types_val, sizeof(layouttype4));
          LastOffset += sizeof(layouttype4);

          op_attr_success = 1;
          break;
#endif

        default:
          LogFullDebug(COMPONENT_NFS_V4,
                       " unsupported value for attributes bitmap = %u", attribute_to_set);

          op_attr_success = 0;
          break;
        }                       /* switch( attribute_to_set ) */

      /* Increase the Offset for the next operation if this was a success */
      if(op_attr_success)
        {
          /* Set the returned bitmask */
          attrvalslist[j] = attribute_to_set;
          j += 1;

          /* Be carefull not to get out of attrvalsBuffer */
          if(LastOffset > ATTRVALS_BUFFLEN)
            return -1;
        }

    }                           /* for i */

  /* Set the bitmap for result */
  if((Fattr->attrmask.bitmap4_val = (uint32_t *) Mem_Alloc_Label(2 * sizeof(uint32_t),
                                                                 "FSALattr_To_Fattr:bitmap")) == NULL)
    return -1;
  memset((char *)Fattr->attrmask.bitmap4_val, 0, 2 * sizeof(uint32_t));

  nfs4_list_to_bitmap4(&(Fattr->attrmask), &j, attrvalslist);

  /* Set the attrlist4 */
  Fattr->attr_vals.attrlist4_len = LastOffset;
  if(LastOffset != 0)           /* No need to allocate an empty buffer */
    {
      if((Fattr->attr_vals.attrlist4_val =
          Mem_Alloc_Label(Fattr->attr_vals.attrlist4_len,
                          "FSALattr_To_Fattr:attrvals")) == NULL)
        return -1;
      memset((char *)Fattr->attr_vals.attrlist4_val, 0, Fattr->attr_vals.attrlist4_len);
      memcpy(Fattr->attr_vals.attrlist4_val, attrvalsBuffer,
             Fattr->attr_vals.attrlist4_len);
    }
  /* LastOffset contains the length of the attrvalsBuffer usefull data */

  return 0;
}                               /* nfs4_FSALattr_To_Fattr */

/**
 *
 * nfs3_Sattr_To_FSALattr: Converts NFSv3 Sattr to FSAL Attributes.
 *
 * Converts NFSv3 Sattr to FSAL Attributes.
 *
 * @param pFSAL_attr  [OUT]  computed FSAL attributes.
 * @param psattr      [IN]   NFSv3 sattr to be set.
 * 
 * @return 0 if failed, 1 if successful.
 *
 */
int nfs3_Sattr_To_FSALattr(fsal_attrib_list_t * pFSAL_attr,     /* Out: file attributes */
                           sattr3 * psattr)     /* In: file attributes  */
{
  struct timeval t;

  if(pFSAL_attr == NULL || psattr == NULL)
    return 0;

  pFSAL_attr->asked_attributes = 0;

  if(psattr->mode.set_it == TRUE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: mode = %o",
                   psattr->mode.set_mode3_u.mode);
      pFSAL_attr->mode = unix2fsal_mode(psattr->mode.set_mode3_u.mode);
      pFSAL_attr->asked_attributes |= FSAL_ATTR_MODE;
    }

  if(psattr->uid.set_it == TRUE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: uid = %d",
                   psattr->uid.set_uid3_u.uid);
      pFSAL_attr->owner = psattr->uid.set_uid3_u.uid;
      pFSAL_attr->asked_attributes |= FSAL_ATTR_OWNER;
    }

  if(psattr->gid.set_it == TRUE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: gid = %d",
                   psattr->gid.set_gid3_u.gid);
      pFSAL_attr->group = psattr->gid.set_gid3_u.gid;
      pFSAL_attr->asked_attributes |= FSAL_ATTR_GROUP;
    }

  if(psattr->size.set_it == TRUE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: size = %lld",
                   psattr->size.set_size3_u.size);
      pFSAL_attr->filesize = (fsal_size_t) psattr->size.set_size3_u.size;
      pFSAL_attr->spaceused = (fsal_size_t) psattr->size.set_size3_u.size;
      /* Both FSAL_ATTR_SIZE and FSAL_ATTR_SPACEUSED are to be managed */
      pFSAL_attr->asked_attributes |= FSAL_ATTR_SIZE;
      pFSAL_attr->asked_attributes |= FSAL_ATTR_SPACEUSED;
    }

  if(psattr->atime.set_it != DONT_CHANGE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: set=%d atime = %d,%d",
                   psattr->atime.set_it, psattr->atime.set_atime_u.atime.seconds,
                   psattr->atime.set_atime_u.atime.nseconds);
      if(psattr->atime.set_it == SET_TO_CLIENT_TIME)
        {
          pFSAL_attr->atime.seconds = psattr->atime.set_atime_u.atime.seconds;
          pFSAL_attr->atime.nseconds = psattr->atime.set_atime_u.atime.nseconds;
        }
      else
        {
          /* Use the server's current time */
          gettimeofday(&t, NULL);

          pFSAL_attr->atime.seconds = t.tv_sec;
          pFSAL_attr->atime.nseconds = t.tv_usec * 1000;
        }
      pFSAL_attr->asked_attributes |= FSAL_ATTR_ATIME;
    }

  if(psattr->mtime.set_it != DONT_CHANGE)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_Sattr_To_FSALattr: set=%d mtime = %d,%d",
                   psattr->atime.set_it, psattr->mtime.set_mtime_u.mtime.seconds,
                   psattr->mtime.set_mtime_u.mtime.nseconds);
      if(psattr->mtime.set_it == SET_TO_CLIENT_TIME)
        {
          pFSAL_attr->mtime.seconds = psattr->mtime.set_mtime_u.mtime.seconds;
          pFSAL_attr->mtime.nseconds = psattr->mtime.set_mtime_u.mtime.nseconds;
        }
      else
        {
          /* Use the server's current time */
          gettimeofday(&t, NULL);
          pFSAL_attr->mtime.seconds = t.tv_sec;
          pFSAL_attr->mtime.nseconds = t.tv_usec * 1000;
        }
      pFSAL_attr->asked_attributes |= FSAL_ATTR_MTIME;
    }

  return 1;
}                               /* nfs3_Sattr_To_FSALattr */

/**
 * 
 * nfs2_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv2 attributes.
 * 
 * Converts FSAL Attributes to NFSv2 attributes.
 *
 * @param pexport   [IN]  the related export entry.
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv2 attributes. 
 * 
 * @return 1 if successful, 0 otherwise. 
 *
 */
int nfs2_FSALattr_To_Fattr(exportlist_t * pexport,      /* In: the related export entry */
                           fsal_attrib_list_t * pFSAL_attr,     /* In: file attributes  */
                           fattr2 * pFattr)     /* Out: file attributes */
{
  /* Badly formed arguments */
  if(pFSAL_attr == NULL || pFattr == NULL)
    return 0;

  /* @todo BUGAZOMEU: sanity check on attribute mask (does the FSAL support the attributes required to support NFSv2 ? */

  /* initialize mode */
  pFattr->mode = 0;

  switch (pFSAL_attr->type)
    {
    case FSAL_TYPE_FILE:
      pFattr->type = NFREG;
      pFattr->mode = NFS2_MODE_NFREG;
      break;

    case FSAL_TYPE_DIR:
      pFattr->type = NFDIR;
      pFattr->mode = NFS2_MODE_NFDIR;
      break;

    case FSAL_TYPE_BLK:
      pFattr->type = NFBLK;
      pFattr->mode = NFS2_MODE_NFBLK;
      break;

    case FSAL_TYPE_CHR:
      pFattr->type = NFCHR;
      pFattr->mode = NFS2_MODE_NFCHR;
      break;

    case FSAL_TYPE_FIFO:
      pFattr->type = NFFIFO;
      /** @todo mode mask ? */
      break;

    case FSAL_TYPE_LNK:
      pFattr->type = NFLNK;
      pFattr->mode = NFS2_MODE_NFLNK;
      break;

    case FSAL_TYPE_SOCK:
      pFattr->type = NFSOCK;
      /** @todo mode mask ? */
      break;

    case FSAL_TYPE_XATTR:
    case FSAL_TYPE_JUNCTION:
      pFattr->type = NFBAD;
    }

  pFattr->mode |= fsal2unix_mode(pFSAL_attr->mode);
  pFattr->nlink = pFSAL_attr->numlinks;
  pFattr->uid = pFSAL_attr->owner;
  pFattr->gid = pFSAL_attr->group;

  /* in NFSv2, it only keeps fsid.major, casted into an into an int32 */
  pFattr->fsid = (u_int) (pexport->filesystem_id.major & 0xFFFFFFFFLL);

  LogFullDebug(COMPONENT_NFSPROTO,
               "nfs2_FSALattr_To_Fattr: fsid.major = %#llX (%llu), fsid.minor = %#llX (%llu), nfs2_fsid = %#X (%u)",
               pexport->filesystem_id.major, pexport->filesystem_id.major,
               pexport->filesystem_id.minor, pexport->filesystem_id.minor, pFattr->fsid,
               pFattr->fsid);

  if(pFSAL_attr->filesize > NFS2_MAX_FILESIZE)
    pFattr->size = NFS2_MAX_FILESIZE;
  else
    pFattr->size = pFSAL_attr->filesize;

  pFattr->blocksize = DEV_BSIZE;

  pFattr->blocks = pFattr->size >> 9;   /* dividing by 512 */
  if(pFattr->size % DEV_BSIZE != 0)
    pFattr->blocks += 1;

  if(pFSAL_attr->type == FSAL_TYPE_CHR || pFSAL_attr->type == FSAL_TYPE_BLK)
    pFattr->rdev = pFSAL_attr->rawdev.major;
  else
    pFattr->rdev = 0;

  pFattr->atime.seconds = pFSAL_attr->atime.seconds;
  pFattr->atime.useconds = pFSAL_attr->atime.nseconds / 1000;
  pFattr->mtime.seconds = pFSAL_attr->mtime.seconds;
  pFattr->mtime.useconds = pFSAL_attr->mtime.nseconds / 1000;
  pFattr->ctime.seconds = pFSAL_attr->ctime.seconds;
  pFattr->ctime.useconds = pFSAL_attr->ctime.nseconds / 1000;
  pFattr->fileid = pFSAL_attr->fileid;

  return 1;
}                               /*  nfs2_FSALattr_To_Fattr */

/**
 * 
 * nfs4_SetCompoundExport
 * 
 * This routine fills in the pexport field in the compound data.
 *
 * @param pfh [OUT] pointer to compound data to be used. 
 * 
 * @return NFS4_OK if successfull. Possible errors are NFS4ERR_BADHANDLE and NFS4ERR_WRONGSEC.
 *
 */

int nfs4_SetCompoundExport(compound_data_t * data)
{
  short exportid;

  /* This routine is not related to pseudo fs file handle, do not handle them */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return NFS4_OK;

  /* Get the export id */
  if((exportid = nfs4_FhandleToExportId(&(data->currentFH))) == 0)
    return NFS4ERR_BADHANDLE;

  if((data->pexport = nfs_Get_export_by_id(data->pfullexportlist, exportid)) == NULL)
    return NFS4ERR_BADHANDLE;

  if((data->pexport->options & EXPORT_OPTION_NFSV4) == 0)
    return NFS4ERR_ACCESS;

  if(nfs4_MakeCred(data) != NFS4_OK)
    return NFS4ERR_WRONGSEC;

  return NFS4_OK;
}                               /* nfs4_SetCompoundExport */

/**
 * 
 * nfs4_FhandleToExId
 * 
 * This routine extracts the export id from the filehandle
 *
 * @param fh4p  [IN]  pointer to file handle to be used.
 * @param ExIdp [OUT] pointer to buffer in which found export id will be stored. 
 * 
 * @return TRUE is successful, FALSE otherwise. 
 *
 */
int nfs4_FhandleToExId(nfs_fh4 * fh4p, unsigned short *ExIdp)
{
  file_handle_v4_t *pfhandle4;

  /* Map the filehandle to the correct structure */
  pfhandle4 = (file_handle_v4_t *) (fh4p->nfs_fh4_val);

  /* The function should not be used on a pseudo fhandle */
  if(pfhandle4->pseudofs_flag == TRUE)
    return FALSE;

  *ExIdp = pfhandle4->exportid;
  return TRUE;
}                               /* nfs4_FhandleToExId */

/**** Glue related functions ****/

/**
 *
 * nfs4_stringid_split: Splits a domain stamped name in two different parts.
 * 
 *  Splits a domain stamped name in two different parts.
 *
 * @param buff [IN] the input string 
 * @param uidname [OUT] the extracted uid name
 * @param domainname [OUT] the extracted fomain name
 *
 * @return nothing (void function) 
 * 
 */
void nfs4_stringid_split(char *buff, char *uidname, char *domainname)
{
  char *c = NULL;
  unsigned int i = 0;

  for(c = buff, i = 0; *c != '\0'; c++, i++)
    if(*c == '@')
      break;

  strncpy(uidname, buff, i);
  uidname[i] = '\0';
  strcpy(domainname, c);

  LogFullDebug(COMPONENT_NFS_V4,
               "buff = #%s#    uid = #%s#   domain = #%s#",
               buff, uidname, domainname);
}                               /* nfs4_stringid_split */

/**
 *
 * free_utf8: Free's a utf8str that was created by utf8dup
 *
 * @param utf8str [IN]  UTF8 string to be freed
 *
 */
void free_utf8(utf8string * utf8str)
{
  if(utf8str != NULL)
    {
      if(utf8str->utf8string_val != NULL)
        Mem_Free(utf8str->utf8string_val);
      utf8str->utf8string_val = 0;
      utf8str->utf8string_len = 0;
    }
}

/**
 *
 * utf8dup: Makes a copy of a utf8str.
 *
 * @param newstr  [OUT] copied UTF8 string
 * @param oldstr  [IN]  input UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
int utf8dup(utf8string * newstr, utf8string * oldstr)
{
  if(newstr == NULL)
    return -1;

  newstr->utf8string_len = oldstr->utf8string_len;
  newstr->utf8string_val = NULL;

  if(oldstr->utf8string_len == 0 || oldstr->utf8string_val == NULL)
    return 0;

  newstr->utf8string_val = (char *)Mem_Alloc_Label(oldstr->utf8string_len,
                                                   "utf82str");
  if(newstr->utf8string_val == NULL)
    return -1;

  strncpy(newstr->utf8string_val, oldstr->utf8string_val, oldstr->utf8string_len);

  return 0;
}                               /* uft82str */

/**
 *
 * utf82str: converts a UTF8 string buffer into a string descriptor.
 *
 * Converts a UTF8 string buffer into a string descriptor.
 *
 * @param str     [OUT] computed output string
 * @param utf8str [IN]  input UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
int utf82str(char *str, int size, utf8string * utf8str)
{
  int copy;

  if(str == NULL)
    return -1;

  if(utf8str == NULL || utf8str->utf8string_len == 0)
    {
      str[0] = '\0';
      return -1;
    }

  if(utf8str->utf8string_len >= size)
    copy = size - 1;
  else
    copy = utf8str->utf8string_len;

  strncpy(str, utf8str->utf8string_val, copy);
  str[copy] = '\0';

  if(copy < utf8str->utf8string_len)
    return -1;

  return 0;
}                               /* uft82str */

/**
 *
 * str2utf8: converts a string buffer into a UTF8 string descriptor.
 *
 * Converts a string buffer into a UTF8 string descriptor.
 *
 * @param str     [IN]  input string
 * @param utf8str [OUT] computed UTF8 string
 *
 * @return -1 if failed, 0 if successful.
 *
 */
int str2utf8(char *str, utf8string * utf8str)
{
  uint_t len;
  char buff[MAXNAMLEN];

  /* The uft8 will probably be sent over XDR, for this reason, its size MUST be a multiple of 32 bits = 4 bytes */
  strcpy(buff, str);
  len = strlen(buff);

  /* BUGAZOMEU: TO BE DONE: use STUFF ALLOCATOR here */
  if(utf8str->utf8string_val == NULL)
    return -1;

  utf8str->utf8string_len = len;
  memcpy(utf8str->utf8string_val, buff, utf8str->utf8string_len);
  return 0;
}                               /* str2utf8 */

/**
 * 
 * nfs4_NextSeqId: compute the next nfsv4 sequence id.
 *
 * Compute the next nfsv4 sequence id.
 *
 * @param seqid [IN] previous sequence number.
 * 
 * @return the requested sequence number.
 *
 */

seqid4 nfs4_NextSeqId(seqid4 seqid)
{
  return ((seqid + 1) % 0xFFFFFFFF);
}                               /* nfs4_NextSeqId */

/**
 *
 * nfs_bitmap4_to_list: convert an attribute's bitmap to a list of attributes.
 *
 * Convert an attribute's bitmap to a list of attributes.
 *
 * @param b     [IN] bitmap to convert.
 * @param plen  [OUT] list's length.
 * @param plval [OUT] list's values.
 *
 * @return nothing (void function)
 *
 */

/*
 * bitmap is usually 2 x uint32_t which makes a uint64_t 
 *
 * Structure of the bitmap is as follow
 *
 *                  0         1
 *    +-------+---------+----------+-
 *    | count | 31 .. 0 | 63 .. 32 | 
 *    +-------+---------+----------+-
 *
 * One bit is set for every possible attributes. The bits are packed together in a uint32_T (XDR alignment reason probably)
 * As said in the RFC3530, the n-th bit is with the uint32_t #(n/32), and its position with the uint32_t is n % 32
 * Example
 *     1st bit = FATTR4_TYPE            = 1
 *     2nd bit = FATTR4_LINK_SUPPORT    = 5
 *     3rd bit = FATTR4_SYMLINK_SUPPORT = 6
 *
 *     Juste one uint32_t is necessay: 2**1 + 2**5 + 2**6 = 2 + 32 + 64 = 98
 *   +---+----+
 *   | 1 | 98 |
 *   +---+----+
 *
 * Other Example
 *
 *     1st bit = FATTR4_LINK_SUPPORT    = 5
 *     2nd bit = FATTR4_SYMLINK_SUPPORT = 6
 *     3rd bit = FATTR4_MODE            = 33
 *     4th bit = FATTR4_OWNER           = 36
 *
 *     Two uint32_t will be necessary there:
 *            #1 = 2**5 + 2**6 = 32 + 64 = 96
 #            #2 = 2**(33-32) + 2**(36-32) = 2**1 + 2**4 = 2 + 16 = 18 
 *   +---+----+----+
 *   | 2 | 98 | 18 |
 *   +---+----+----+
 *
 */

void nfs4_bitmap4_to_list(bitmap4 * b, uint_t * plen, uint32_t * pval)
{
  uint_t i = 0;
  uint_t val = 0;
  uint_t index = 0;
  uint_t offset = 0;
  if(b->bitmap4_len > 0)
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u Val = %u|%u",
                 b->bitmap4_len, b->bitmap4_val[0], b->bitmap4_val[1]);
  else
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u ... ", b->bitmap4_len);

  for(offset = 0; offset < b->bitmap4_len; offset++)
    {
      for(i = 0; i < 32; i++)
        {
          val = 1 << i;         /* Compute 2**i */
          if(b->bitmap4_val[offset] & val)
            pval[index++] = i + 32 * offset;
        }
    }
  *plen = index;

}                               /* nfs4_bitmap4_to_list */

/**
 * 
 * nfs4_list_to_bitmap4: convert a list of attributes to an attributes's bitmap.
 * 
 * Convert a list of attributes to an attributes's bitmap.
 *
 * @param b [OUT] computed bitmap
 * @param plen [IN] list's length 
 * @param pval [IN] list's array
 *
 * @return nothing (void function).
 *
 */

/* bitmap is usually 2 x uint32_t which makes a uint64_t 
 * bitmap4_len is the number of uint32_t required to keep the bitmap value 
 *
 * Structure of the bitmap is as follow
 *
 *                  0         1
 *    +-------+---------+----------+-
 *    | count | 31 .. 0 | 63 .. 32 | 
 *    +-------+---------+----------+-
 *
 * One bit is set for every possible attributes. The bits are packed together in a uint32_T (XDR alignment reason probably)
 * As said in the RFC3530, the n-th bit is with the uint32_t #(n/32), and its position with the uint32_t is n % 32
 * Example
 *     1st bit = FATTR4_TYPE            = 1
 *     2nd bit = FATTR4_LINK_SUPPORT    = 5
 *     3rd bit = FATTR4_SYMLINK_SUPPORT = 6
 *
 *     Juste one uint32_t is necessay: 2**1 + 2**5 + 2**6 = 2 + 32 + 64 = 98
 *   +---+----+
 *   | 1 | 98 |
 *   +---+----+
 *
 * Other Example
 *
 *     1st bit = FATTR4_LINK_SUPPORT    = 5
 *     2nd bit = FATTR4_SYMLINK_SUPPORT = 6
 *     3rd bit = FATTR4_MODE            = 33
 *     4th bit = FATTR4_OWNER           = 36
 *
 *     Two uint32_t will be necessary there:
 *            #1 = 2**5 + 2**6 = 32 + 64 = 96
 #            #2 = 2**(33-32) + 2**(36-32) = 2**1 + 2**4 = 2 + 16 = 18 
 *   +---+----+----+
 *   | 2 | 98 | 18 |
 *   +---+----+----+
 *
 */

/* This function converts a list of attributes to a bitmap4 structure */
void nfs4_list_to_bitmap4(bitmap4 * b, uint_t * plen, uint32_t * pval)
{
  uint_t i;
  uint_t intpos = 0;
  uint_t bitpos = 0;
  uint_t val = 0;
  /* Both uint32 int the bitmap MUST be allocated */
  b->bitmap4_val[0] = 0;
  b->bitmap4_val[1] = 0;

  b->bitmap4_len = 1;
  for(i = 0; i < *plen; i++)
    {
      intpos = pval[i] / 32;
      bitpos = pval[i] % 32;
      val = 1 << bitpos;
      b->bitmap4_val[intpos] |= val;

      if(intpos != 0)
        b->bitmap4_len = 2;
    }
  LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u   Val = %u|%u",
               b->bitmap4_len,
               b->bitmap4_len >= 1 ? b->bitmap4_val[0] : 0,
               b->bitmap4_len >= 2 ? b->bitmap4_val[1] : 0);
}                               /* nfs4_list_to_bitmap4 */

/* 
 * Conversion of attributes 
*/

/**
 * 
 * nfs3_FSALattr_To_Fattr: Converts FSAL Attributes to NFSv3 attributes.
 * 
 * Converts FSAL Attributes to NFSv3 attributes.
 *
 * @param pexport   [IN]  the related export entry
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv3 attributes. 
 * 
 * @return 1 if successful, 0 otherwise. 
 *
 */
int nfs3_FSALattr_To_Fattr(exportlist_t * pexport,      /* In: the related export entry */
                           fsal_attrib_list_t * FSAL_attr,      /* In: file attributes */
                           fattr3 * Fattr)      /* Out: file attributes */
{
  if(FSAL_attr == NULL || Fattr == NULL)
    {
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_FSALattr_To_Fattr: FSAL_attr=%p, Fattr=%p",
                   FSAL_attr, Fattr);
      return 0;
    }

  switch (FSAL_attr->type)
    {
    case FSAL_TYPE_FIFO:
      Fattr->type = NF3FIFO;
      break;

    case FSAL_TYPE_CHR:
      Fattr->type = NF3CHR;
      break;

    case FSAL_TYPE_DIR:
      Fattr->type = NF3DIR;
      break;

    case FSAL_TYPE_BLK:
      Fattr->type = NF3BLK;
      break;

    case FSAL_TYPE_FILE:
    case FSAL_TYPE_XATTR:
      Fattr->type = NF3REG;
      break;

    case FSAL_TYPE_LNK:
      Fattr->type = NF3LNK;
      break;

    case FSAL_TYPE_SOCK:
      Fattr->type = NF3SOCK;
      break;

    case FSAL_TYPE_JUNCTION:
      /* Should not occur */
      LogFullDebug(COMPONENT_NFSPROTO,
                   "nfs3_FSALattr_To_Fattr: FSAL_attr->type = %d",
                   FSAL_attr->type);
      Fattr->type = 0;
      return 0;
    }

  Fattr->mode = fsal2unix_mode(FSAL_attr->mode);
  Fattr->nlink = FSAL_attr->numlinks;
  Fattr->uid = FSAL_attr->owner;
  Fattr->gid = FSAL_attr->group;
  Fattr->size = FSAL_attr->filesize;
  Fattr->used = FSAL_attr->spaceused;

  if(FSAL_attr->type == FSAL_TYPE_CHR || FSAL_attr->type == FSAL_TYPE_BLK)
    {
      Fattr->rdev.specdata1 = FSAL_attr->rawdev.major;
      Fattr->rdev.specdata2 = FSAL_attr->rawdev.minor;
    }
  else
    {
      Fattr->rdev.specdata1 = 0;
      Fattr->rdev.specdata2 = 0;
    }

  /* in NFSv3, we only keeps fsid.major, casted into an nfs_uint64 */
  Fattr->fsid = (nfs3_uint64) pexport->filesystem_id.major;

  LogFullDebug(COMPONENT_NFSPROTO,
               "nfs3_FSALattr_To_Fattr: fsid.major = %#llX (%llu), fsid.minor = %#llX (%llu), nfs3_fsid = %#llX (%llu)",
               pexport->filesystem_id.major, pexport->filesystem_id.major,
               pexport->filesystem_id.minor, pexport->filesystem_id.minor, Fattr->fsid,
               Fattr->fsid);

  Fattr->fileid = FSAL_attr->fileid;
  Fattr->atime.seconds = FSAL_attr->atime.seconds;
  Fattr->atime.nseconds = FSAL_attr->atime.nseconds;
  Fattr->mtime.seconds = FSAL_attr->mtime.seconds;
  Fattr->mtime.nseconds = FSAL_attr->mtime.nseconds;
  Fattr->ctime.seconds = FSAL_attr->ctime.seconds;
  Fattr->ctime.nseconds = FSAL_attr->ctime.nseconds;

  return 1;
}                               /* nfs3_FSALattr_To_Fattr */

/**
 * 
 * nfs2_Sattr_To_FSALattr: Converts NFSv2 Set Attributes to FSAL attributes.
 * 
 * Converts NFSv2 Set Attributes to FSAL attributes.
 *
 * @param FSAL_attr [IN]  pointer to FSAL attributes.
 * @param Fattr     [OUT] pointer to NFSv2 set attributes. 
 * 
 * @return 1 if successful, 0 otherwise. 
 *
 */
int nfs2_Sattr_To_FSALattr(fsal_attrib_list_t * pFSAL_attr,     /* Out: file attributes */
                           sattr2 * Fattr)      /* In: file attributes */
{
  struct timeval t;

  FSAL_CLEAR_MASK(pFSAL_attr->asked_attributes);

  if(Fattr->mode != (unsigned int)-1)
    {
      pFSAL_attr->mode = unix2fsal_mode(Fattr->mode);
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_MODE);
    }

  if(Fattr->uid != (unsigned int)-1)
    {
      pFSAL_attr->owner = Fattr->uid;
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_OWNER);
    }

  if(Fattr->gid != (unsigned int)-1)
    {
      pFSAL_attr->group = Fattr->gid;
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_GROUP);
    }

  if(Fattr->size != (unsigned int)-1)
    {
      /* Both FSAL_ATTR_SIZE and FSAL_ATTR_SPACEUSED are to be managed */
      pFSAL_attr->filesize = (fsal_size_t) Fattr->size;
      pFSAL_attr->spaceused = (fsal_size_t) Fattr->size;
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_SIZE);
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_SPACEUSED);
    }

  /* if mtime.useconds == 1 millions,
   * this means we must set atime and mtime
   * to server time (NFS Illustrated p. 98)
   */
  if(Fattr->mtime.useconds == 1000000)
    {
      gettimeofday(&t, NULL);

      pFSAL_attr->atime.seconds = pFSAL_attr->mtime.seconds = t.tv_sec;
      pFSAL_attr->atime.nseconds = pFSAL_attr->mtime.nseconds = t.tv_usec * 1000;
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_ATIME);
      FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_MTIME);
    }
  else
    {
      /* set atime to client */

      if(Fattr->atime.seconds != (unsigned int)-1)
        {
          pFSAL_attr->atime.seconds = Fattr->atime.seconds;

          if(Fattr->atime.seconds != (unsigned int)-1)
            pFSAL_attr->atime.nseconds = Fattr->atime.useconds * 1000;
          else
            pFSAL_attr->atime.nseconds = 0;     /* ignored */

          FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_ATIME);
        }

      /* set mtime to client */

      if(Fattr->mtime.seconds != (unsigned int)-1)
        {
          pFSAL_attr->mtime.seconds = Fattr->mtime.seconds;

          if(Fattr->mtime.seconds != (unsigned int)-1)
            pFSAL_attr->mtime.nseconds = Fattr->mtime.useconds * 1000;
          else
            pFSAL_attr->mtime.nseconds = 0;     /* ignored */

          FSAL_SET_MASK(pFSAL_attr->asked_attributes, FSAL_ATTR_MTIME);
        }
    }

  return 1;
}                               /* nfs2_Sattr_To_FSALattr */

/**
 *
 * nfs4_Fattr_Check_Access: checks if attributes have READ or WRITE access.
 *
 * Checks if attributes have READ or WRITE access.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 * @param access     [IN] access to be checked, either FATTR4_ATTR_READ or FATTR4_ATTR_WRITE
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Check_Access(fattr4 * Fattr, int access)
{
  unsigned int i = 0;

  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;

  /* Parameter sanity check */
  if(Fattr == NULL)
    return 0;

  if(access != FATTR4_ATTR_READ && access != FATTR4_ATTR_WRITE)
    return 0;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(&(Fattr->attrmask), &attrmasklen, attrmasklist);

  for(i = 0; i < attrmasklen; i++)
    {
#ifdef _USE_NFS4_1
      if(attrmasklist[i] > FATTR4_FS_CHARSET_CAP)
#else
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
#endif
        {
          /* Erroneous value... skip */
          continue;
        }

      if(((int)fattr4tab[attrmasklist[i]].access & access) != access)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Check_Access */

/**
 *
 * nfs4_Fattr_Check_Access_Bitmap: checks if attributes bitmaps have READ or WRITE access.
 *
 * Checks if attributes have READ or WRITE access.
 *
 * @param pbitmap    [IN] pointer to NFSv4 attributes.
 * @param access     [IN] access to be checked, either FATTR4_ATTR_READ or FATTR4_ATTR_WRITE
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Check_Access_Bitmap(bitmap4 * pbitmap, int access)
{
  unsigned int i = 0;

  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;

  /* Parameter sanity check */
  if(pbitmap == NULL)
    return 0;

  if(access != FATTR4_ATTR_READ && access != FATTR4_ATTR_WRITE)
    return 0;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(pbitmap, &attrmasklen, attrmasklist);

  for(i = 0; i < attrmasklen; i++)
    {
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
        {
          /* Erroneous value... skip */
          continue;
        }

      if(((int)fattr4tab[attrmasklist[i]].access & access) != access)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Check_Access */

/**
 *
 * nfs4_bitmap4_Remove_Unsupported: removes unsupported attributes from bitmap4
 *
 * Removes unsupported attributes from bitmap4
 *
 * @param pbitmap    [IN] pointer to NFSv4 attributes's bitmap.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */
int nfs4_bitmap4_Remove_Unsupported(bitmap4 * pbitmap )
{
  uint_t i = 0;
  uint_t val = 0;
  uint_t offset = 0;

  uint32_t bitmap_val[2] ;
  bitmap4 bout ;

  bout.bitmap4_val = bitmap_val ;
  bout.bitmap4_len = pbitmap->bitmap4_len  ;

  if(pbitmap->bitmap4_len > 0)
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u Val = %u|%u",
                 pbitmap->bitmap4_len, pbitmap->bitmap4_val[0],
                 pbitmap->bitmap4_val[1]);
  else
    LogFullDebug(COMPONENT_NFS_V4, "Bitmap: Len = %u ... ",
                 pbitmap->bitmap4_len);

  bout.bitmap4_val[0] = 0 ;
  bout.bitmap4_val[1] = 0 ;

  for(offset = 0; offset < pbitmap->bitmap4_len; offset++)
    {
      for(i = 0; i < 32; i++)
        {
          val = 1 << i;         /* Compute 2**i */
          if(pbitmap->bitmap4_val[offset] & val)
           {
             if( fattr4tab[i+32*offset].supported ) /* keep only supported stuff */
               bout.bitmap4_val[offset] |= val ; 
           }
        }
    }

  pbitmap->bitmap4_val[0] = bout.bitmap4_val[0] ;  
  if( pbitmap->bitmap4_len > 1 )
     pbitmap->bitmap4_val[1] = bout.bitmap4_val[1] ;  

  return 1 ;
}                               /* nfs4_Fattr_Bitmap_Remove_Unsupported */


/**
 *
 * nfs4_Fattr_Supported: Checks if an attribute is supported.
 *
 * Checks if an attribute is supported.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Supported(fattr4 * Fattr)
{
  unsigned int i = 0;

  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;

  /* Parameter sanity check */
  if(Fattr == NULL)
    return 0;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(&(Fattr->attrmask), &attrmasklen, attrmasklist);

  for(i = 0; i < attrmasklen; i++)
    {

#ifndef _USE_NFS4_1
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
        {
          /* Erroneous value... skip */
          continue;
        }
#endif

      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_Fattr_Supported  ==============> %s supported flag=%u | ",
                   fattr4tab[attrmasklist[i]].name, fattr4tab[attrmasklist[i]].supported);

      if(!fattr4tab[attrmasklist[i]].supported)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Supported */

/**
 *
 * nfs4_Fattr_Supported: Checks if an attribute is supported.
 *
 * Checks if an attribute is supported.
 *
 * @param Fattr      [IN] pointer to NFSv4 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */

int nfs4_Fattr_Supported_Bitmap(bitmap4 * pbitmap)
{
  unsigned int i = 0;

  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;

  /* Parameter sanity check */
  if(pbitmap == NULL)
    return 0;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(pbitmap, &attrmasklen, attrmasklist);

  for(i = 0; i < attrmasklen; i++)
    {

#ifndef _USE_NFS4_1
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
        {
          /* Erroneous value... skip */
          continue;
        }
#endif
      
      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_Fattr_Supported  ==============> %s supported flag=%u",
                   fattr4tab[attrmasklist[i]].name,
                   fattr4tab[attrmasklist[i]].supported);
      if(!fattr4tab[attrmasklist[i]].supported)
        return 0;
    }

  return 1;
}                               /* nfs4_Fattr_Supported */

/**
 *
 * nfs4_Fattr_cmp: compares 2 fattr4 buffers.
 *
 * Compares 2 fattr4 buffers.
 *
 * @param Fattr1      [IN] pointer to NFSv4 attributes.
 * @param Fattr2      [IN] pointer to NFSv4 attributes.
 *
 * @return TRUE if attributes are the same, FALSE otherwise, but -1 if RDATTR_ERROR is set
 *
 */
int nfs4_Fattr_cmp(fattr4 * Fattr1, fattr4 * Fattr2)
{
  uint32_t attrmasklist1[FATTR4_MOUNTED_ON_FILEID];     /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen1 = 0;
  u_int LastOffset;
  uint32_t attrmasklist2[FATTR4_MOUNTED_ON_FILEID];     /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen2 = 0;
  uint32_t i;
  uint32_t k;
  unsigned int cmp = 0;
  u_int len = 0;
  uint32_t attribute_to_set = 0;

  if(Fattr1 == NULL)
    return FALSE;

  if(Fattr2 == NULL)
    return FALSE;

  if(Fattr1->attrmask.bitmap4_len != Fattr2->attrmask.bitmap4_len)      /* different mask */
    return FALSE;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(&(Fattr1->attrmask), &attrmasklen1, attrmasklist1);
  nfs4_bitmap4_to_list(&(Fattr2->attrmask), &attrmasklen2, attrmasklist2);

  /* Should not occur, bu this is a sanity check */
  if(attrmasklen1 != attrmasklen2)
    return FALSE;

  for(i = 0; i < attrmasklen1; i++)
    {
      if(attrmasklist1[i] != attrmasklist2[i])
        return 0;

      if(attrmasklist1[i] == FATTR4_RDATTR_ERROR)
        return -1;

      if(attrmasklist2[i] == FATTR4_RDATTR_ERROR)
        return -1;
    }

  cmp = 0;
  LastOffset = 0;
  len = 0;

  for(i = 0; i < attrmasklen1; i++)
    {
      attribute_to_set = attrmasklist1[i];

      LogFullDebug(COMPONENT_NFS_V4,
                   "nfs4_Fattr_cmp ==============> %s",
                   fattr4tab[attribute_to_set].name);

      switch (attribute_to_set)
        {
        case FATTR4_SUPPORTED_ATTRS:
          memcpy(&len, (char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          cmp +=
              memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                     (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                     sizeof(u_int));

          len = htonl(len);
          LastOffset += sizeof(u_int);

          for(k = 0; k < len; k++)
            {
              cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                            (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                            sizeof(uint32_t));
              LastOffset += sizeof(uint32_t);
            }

          break;

        case FATTR4_FILEHANDLE:
        case FATTR4_OWNER:
        case FATTR4_OWNER_GROUP:
          memcpy(&len, (char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          len = ntohl(len);     /* xdr marshalling on fattr4 */
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                        sizeof(u_int));
          LastOffset += sizeof(u_int);
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset), len);
          break;

        case FATTR4_TYPE:
        case FATTR4_FH_EXPIRE_TYPE:
        case FATTR4_CHANGE:
        case FATTR4_SIZE:
        case FATTR4_LINK_SUPPORT:
        case FATTR4_SYMLINK_SUPPORT:
        case FATTR4_NAMED_ATTR:
        case FATTR4_FSID:
        case FATTR4_UNIQUE_HANDLES:
        case FATTR4_LEASE_TIME:
        case FATTR4_RDATTR_ERROR:
        case FATTR4_ACL:
        case FATTR4_ACLSUPPORT:
        case FATTR4_ARCHIVE:
        case FATTR4_CANSETTIME:
        case FATTR4_CASE_INSENSITIVE:
        case FATTR4_CASE_PRESERVING:
        case FATTR4_CHOWN_RESTRICTED:
        case FATTR4_FILEID:
        case FATTR4_FILES_AVAIL:
        case FATTR4_FILES_FREE:
        case FATTR4_FILES_TOTAL:
        case FATTR4_FS_LOCATIONS:
        case FATTR4_HIDDEN:
        case FATTR4_HOMOGENEOUS:
        case FATTR4_MAXFILESIZE:
        case FATTR4_MAXLINK:
        case FATTR4_MAXNAME:
        case FATTR4_MAXREAD:
        case FATTR4_MAXWRITE:
        case FATTR4_MIMETYPE:
        case FATTR4_MODE:
        case FATTR4_NO_TRUNC:
        case FATTR4_NUMLINKS:
        case FATTR4_QUOTA_AVAIL_HARD:
        case FATTR4_QUOTA_AVAIL_SOFT:
        case FATTR4_QUOTA_USED:
        case FATTR4_RAWDEV:
        case FATTR4_SPACE_AVAIL:
        case FATTR4_SPACE_FREE:
        case FATTR4_SPACE_TOTAL:
        case FATTR4_SPACE_USED:
        case FATTR4_SYSTEM:
        case FATTR4_TIME_ACCESS:
        case FATTR4_TIME_ACCESS_SET:
        case FATTR4_TIME_BACKUP:
        case FATTR4_TIME_CREATE:
        case FATTR4_TIME_DELTA:
        case FATTR4_TIME_METADATA:
        case FATTR4_TIME_MODIFY:
        case FATTR4_TIME_MODIFY_SET:
        case FATTR4_MOUNTED_ON_FILEID:
          cmp += memcmp((char *)(Fattr1->attr_vals.attrlist4_val + LastOffset),
                        (char *)(Fattr2->attr_vals.attrlist4_val + LastOffset),
                        fattr4tab[attribute_to_set].size_fattr4);
          break;

        default:
          return 0;
          break;
        }
    }
  if(cmp == 0)
    return TRUE;
  else
    return FALSE;
}

#ifdef _USE_NFS4_ACL
static int nfs4_decode_acl_special_user(utf8string *utf8str, int *who)
{
  int i;

  for (i = 0; i < FSAL_ACE_SPECIAL_EVERYONE; i++)
    {
      if(strncmp(utf8str->utf8string_val, whostr_2_type_map[i].string, utf8str->utf8string_len) == 0)
        {
          *who = whostr_2_type_map[i].type;
          return 0;
        }
    }

  return -1;
}

static int nfs4_decode_acl(fsal_attrib_list_t * pFSAL_attr, fattr4 * Fattr, u_int *LastOffset)
{
  fsal_acl_status_t status;
  fsal_acl_data_t acldata;
  fsal_ace_t *pace;
  fsal_acl_t *pacl;
  int len;
  char buffer[MAXNAMLEN];
  utf8string utf8buffer;
  int who;

  /* Decode number of ACEs. */
  memcpy(&(acldata.naces), (char*)(Fattr->attr_vals.attrlist4_val + *LastOffset), sizeof(u_int));
  acldata.naces = ntohl(acldata.naces);
  LogFullDebug(COMPONENT_NFS_V4,
               "SATTR: Number of ACEs = %u",
               acldata.naces);
  *LastOffset += sizeof(u_int);

  /* Allocate memory for ACEs. */
  acldata.aces = (fsal_ace_t *)nfs4_ace_alloc(acldata.naces);
  if(acldata.aces == NULL)
    {
      LogCrit(COMPONENT_NFS_V4,
              "SATTR: Failed to allocate ACEs");
      return NFS4ERR_SERVERFAULT;
    }
  else
    memset(acldata.aces, 0, acldata.naces * sizeof(fsal_ace_t));

  /* Decode ACEs. */
  for(pace = acldata.aces; pace < acldata.aces + acldata.naces; pace++)
    {
      memcpy(&(pace->type), (char*)(Fattr->attr_vals.attrlist4_val + *LastOffset), sizeof(fsal_acetype_t));
      pace->type = ntohl(pace->type);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE type = 0x%x",
                   pace->type);
      *LastOffset += sizeof(fsal_acetype_t);

      memcpy(&(pace->flag), (char*)(Fattr->attr_vals.attrlist4_val + *LastOffset), sizeof(fsal_aceflag_t));
      pace->flag = ntohl(pace->flag);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE flag = 0x%x",
                   pace->flag);
      *LastOffset += sizeof(fsal_aceflag_t);

      memcpy(&(pace->perm), (char*)(Fattr->attr_vals.attrlist4_val + *LastOffset), sizeof(fsal_aceperm_t));
      pace->perm = ntohl(pace->perm);
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: ACE perm = 0x%x",
                   pace->perm);
      *LastOffset += sizeof(fsal_aceperm_t);

      /* Find out who type */

      /* Convert name to uid or gid */
      memcpy(&len, (char *)(Fattr->attr_vals.attrlist4_val + *LastOffset), sizeof(u_int));
      len = ntohl(len);        /* xdr marshalling on fattr4 */
      *LastOffset += sizeof(u_int);

      memcpy(buffer, (char *)(Fattr->attr_vals.attrlist4_val + *LastOffset), len);
      buffer[len] = '\0';

      /* Do not forget that xdr_opaque are aligned on 32bit long words */
      while((len % 4) != 0)
        len += 1;

      *LastOffset += len;

      /* Decode users. */
      LogFullDebug(COMPONENT_NFS_V4,
                   "SATTR: owner = %s, len = %d, type = %s",
                   buffer, len,
                   GET_FSAL_ACE_WHO_TYPE(*pace));

      utf8buffer.utf8string_val = buffer;
      utf8buffer.utf8string_len = strlen(buffer);

      if(nfs4_decode_acl_special_user(&utf8buffer, &who) == 0)  /* Decode special user. */
        {
          /* Clear group flag for special users */
          pace->flag &= ~(FSAL_ACE_FLAG_GROUP_ID);
          pace->iflag |= FSAL_ACE_IFLAG_SPECIAL_ID;
          pace->who.uid = who;
          LogFullDebug(COMPONENT_NFS_V4,
                       "SATTR: ACE special who.uid = 0x%x",
                       pace->who.uid);
        }
      else
        {
          if(pace->flag == FSAL_ACE_FLAG_GROUP_ID)  /* Decode group. */
            {
              utf82gid(&utf8buffer, &(pace->who.gid));
              LogFullDebug(COMPONENT_NFS_V4,
                           "SATTR: ACE who.gid = 0x%x",
                           pace->who.gid);
            }
          else  /* Decode user. */
            {
              utf82uid(&utf8buffer, &(pace->who.uid));
              LogFullDebug(COMPONENT_NFS_V4,
                           "SATTR: ACE who.uid = 0x%x",
                           pace->who.uid);
            }
        }

      /* Check if we can map a name string to uid or gid. If we can't, do cleanup
       * and bubble up NFS4ERR_BADOWNER. */
      if((pace->flag == FSAL_ACE_FLAG_GROUP_ID ? pace->who.gid : pace->who.uid) == -1)
        {
          LogFullDebug(COMPONENT_NFS_V4,
                       "SATTR: bad owner");
          nfs4_ace_free(acldata.aces);
          return NFS4ERR_BADOWNER;
        }
    }

  pacl = nfs4_acl_new_entry(&acldata, &status);
  if(pacl == NULL)
    {
      LogCrit(COMPONENT_NFS_V4,
              "SATTR: Failed to create a new entry for ACL");
      return NFS4ERR_SERVERFAULT;
    }
  else
     LogFullDebug(COMPONENT_NFS_V4,
                  "SATTR: Successfully created a new entry for ACL, status = %u",
                  status);

  /* Set new ACL */
  pFSAL_attr->acl = pacl;
  LogFullDebug(COMPONENT_NFS_V4,
               "SATTR: new acl = %p",
               pacl);

  return NFS4_OK;
}
#endif                          /* _USE_NFS4_ACL */

/**
 * 
 * nfs4_attrmap_To_FSAL_attrmask: Converts NFSv4 attribute bitmap to
 * FSAL attribute mask.
 * 
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * @param attrmap  [IN]   pointer to NFSv4 attribute bitmap. 
 * @param attrmask [OUT]  pointer to FSAL attribute mask.
 * 
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */
int nfs4_attrmap_to_FSAL_attrmask(bitmap4 attrmap, fsal_attrib_mask_t* attrmask)
{
  unsigned int offset = 0;
  unsigned int i = 0;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_FattrToSattr";

  for(offset = 0; offset < attrmap.bitmap4_len; offset++)
    {
      for(i = 0; i < 32; i++)
        {
          if(attrmap.bitmap4_val[offset] & (1 << i)) {
            uint32_t val = i + 32 * offset;
            switch (val)
              {
              case FATTR4_TYPE:
                *attrmask |= FSAL_ATTR_TYPE;
                break;
              case FATTR4_FILEID:
                *attrmask |= FSAL_ATTR_FILEID;
                break;
              case FATTR4_FSID:
                *attrmask |= FSAL_ATTR_FSID;
                break;
              case FATTR4_NUMLINKS:
                *attrmask |= FSAL_ATTR_NUMLINKS;
                break;
              case FATTR4_SIZE:
                *attrmask |= FSAL_ATTR_SIZE;
                break;
              case FATTR4_MODE:
                *attrmask |= FSAL_ATTR_MODE;
                break;
              case FATTR4_OWNER:
                *attrmask |= FSAL_ATTR_OWNER;
                break;
              case FATTR4_OWNER_GROUP:
                *attrmask |= FSAL_ATTR_GROUP;
                break;
              case FATTR4_CHANGE:
                *attrmask |= FSAL_ATTR_CHGTIME;
                break;
              case FATTR4_RAWDEV:
                *attrmask |= FSAL_ATTR_RAWDEV;
                break;
              case FATTR4_SPACE_USED:
                *attrmask |= FSAL_ATTR_SPACEUSED;
                break;
              case FATTR4_TIME_ACCESS:
                *attrmask |= FSAL_ATTR_ATIME;
                break;
              case FATTR4_TIME_METADATA:
                *attrmask |= FSAL_ATTR_CTIME;
                break;
              case FATTR4_TIME_MODIFY:
                *attrmask |= FSAL_ATTR_MTIME;
                break;
              case FATTR4_TIME_ACCESS_SET:
                *attrmask |= FSAL_ATTR_ATIME;
                break;
              case FATTR4_TIME_MODIFY_SET:
                *attrmask |= FSAL_ATTR_MTIME;
                break;
              case FATTR4_FILEHANDLE:
                LogFullDebug(COMPONENT_NFS_V4,
                             "Filehandle attribute requested on readdir!");
                /* pFSAL_attr->asked_attributes |= FSAL_ATTR_FILEHANDLE; */
                break;
#ifdef _USE_NFS4_ACL
              case FATTR4_ACL:
                *attrmask |= FSAL_ATTR_ACL;
                break;
#endif                          /* _USE_NFS4_ACL */
              }
          }
        }
    }
  return NFS4_OK;
}                               /* nfs4_Fattr_To_FSAL_attr */


/**
 * 
 * nfs4_Fattr_To_FSAL_attr: Converts NFSv4 attributes buffer to a FSAL attributes structure.
 * 
 * Converts NFSv4 attributes buffer to a FSAL attributes structure.
 *
 * @param pFSAL_attr [OUT]  pointer to FSAL attributes.
 * @param Fattr      [IN] pointer to NFSv4 attributes. 
 * 
 * @return NFS4_OK if successful, NFS4ERR codes if not.
 *
 */
int nfs4_Fattr_To_FSAL_attr(fsal_attrib_list_t * pFSAL_attr, fattr4 * Fattr)
{
  u_int LastOffset = 0;
  unsigned int i = 0;
  char __attribute__ ((__unused__)) funcname[] = "nfs4_FattrToSattr";
  uint32_t attrmasklist[FATTR4_MOUNTED_ON_FILEID];      /* List cannot be longer than FATTR4_MOUNTED_ON_FILEID */
  uint32_t attrmasklen = 0;
  uint32_t attribute_to_set = 0;

  int len;
  char buffer[MAXNAMLEN];
  utf8string utf8buffer;

  fattr4_type attr_type;
  fattr4_fsid attr_fsid;
  fattr4_fileid attr_fileid;
  fattr4_time_modify_set attr_time_set;
  fattr4_rdattr_error rdattr_error;
  nfstime4 attr_time;
  fattr4_size attr_size;
  fattr4_change attr_change;
  fattr4_numlinks attr_numlinks;
  fattr4_rawdev attr_rawdev;
  fattr4_space_used attr_space_used;
  fattr4_time_access attr_time_access;
  fattr4_time_modify attr_time_modify;
  fattr4_time_metadata attr_time_metadata;
#ifdef _USE_NFS4_ACL
  int rc;
#endif

  if(pFSAL_attr == NULL || Fattr == NULL)
    return NFS4ERR_BADXDR;

  /* Check attributes data */
  if(Fattr->attr_vals.attrlist4_val == NULL)
    return NFS4ERR_BADXDR;

  /* Convert the attribute bitmap to an attribute list */
  nfs4_bitmap4_to_list(&(Fattr->attrmask), &attrmasklen, attrmasklist);

  LogFullDebug(COMPONENT_NFS_V4,
               "   nfs4_bitmap4_to_list ====> attrmasklen = %d", attrmasklen);

  /* Init */
  pFSAL_attr->asked_attributes = 0;

  for(i = 0; i < attrmasklen; i++)
    {
      attribute_to_set = attrmasklist[i];

#ifdef _USE_NFS4_1
      if(attrmasklist[i] > FATTR4_FS_CHARSET_CAP)
#else
      if(attrmasklist[i] > FATTR4_MOUNTED_ON_FILEID)
#endif
        {
          /* Erroneous value... skip */
          continue;
        }
      LogFullDebug(COMPONENT_NFS_V4,
                   "=================> nfs4_Fattr_To_FSAL_attr: i=%u attr=%u", i,
                   attrmasklist[i]);
      LogFullDebug(COMPONENT_NFS_V4,
                   "Flag for Operation = %d|%d is ON,  name  = %s  reply_size = %d",
                   attrmasklist[i], fattr4tab[attribute_to_set].val,
                   fattr4tab[attribute_to_set].name,
                   fattr4tab[attribute_to_set].size_fattr4);

      switch (attribute_to_set)
        {
        case FATTR4_TYPE:      /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_type,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_type));

          switch (ntohl(attr_type))
            {
            case NF4REG:
              pFSAL_attr->type = FSAL_TYPE_FILE;
              break;

            case NF4DIR:
              pFSAL_attr->type = FSAL_TYPE_DIR;
              break;

            case NF4BLK:
              pFSAL_attr->type = FSAL_TYPE_BLK;
              break;

            case NF4CHR:
              pFSAL_attr->type = FSAL_TYPE_CHR;
              break;

            case NF4LNK:
              pFSAL_attr->type = FSAL_TYPE_LNK;
              break;

            case NF4SOCK:
              pFSAL_attr->type = FSAL_TYPE_SOCK;
              break;

            case NF4FIFO:
              pFSAL_attr->type = FSAL_TYPE_FIFO;
              break;

            case FSAL_TYPE_JUNCTION:
              /* For wanting of a better solution */
              pFSAL_attr->type = 0;
              break;
            }                   /* switch( pattr->type ) */

          pFSAL_attr->asked_attributes |= FSAL_ATTR_TYPE;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          break;

        case FATTR4_FILEID:    /* Used only by FSAL_PROXY to reverse convert */
          /* The analog to the inode number. RFC3530 says "a number uniquely identifying the file within the filesystem"
           * I use hpss_GetObjId to extract this information from the Name Server's handle */
          memcpy((char *)&attr_fileid,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_fileid));
          pFSAL_attr->fileid = nfs_ntohl64(attr_fileid);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_FILEID;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_FSID:      /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_fsid,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_fsid));
          pFSAL_attr->fsid.major = nfs_ntohl64(attr_fsid.major);
          pFSAL_attr->fsid.minor = nfs_ntohl64(attr_fsid.minor);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_FSID;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_NUMLINKS:  /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_numlinks,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_numlinks));
          pFSAL_attr->numlinks = ntohl(attr_numlinks);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_FILEID;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_SIZE:
          memcpy((char *)&attr_size,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_size));

          /* Do not forget the XDR marshalling for the fattr4 stuff */
          pFSAL_attr->filesize = nfs_ntohl64(attr_size);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_SIZE;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: size seen %zu", (size_t)pFSAL_attr->filesize);
          break;

        case FATTR4_MODE:
          memcpy((char *)&(pFSAL_attr->mode),
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_mode));

          /* Do not forget the XDR marshalling for the fattr4 stuff */
          pFSAL_attr->mode = ntohl(pFSAL_attr->mode);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_MODE;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: On voit le mode 0%o", pFSAL_attr->mode);
          break;

        case FATTR4_OWNER:
          memcpy(&len, (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          len = ntohl(len);     /* xdr marshalling on fattr4 */
          LastOffset += sizeof(u_int);

          memcpy(buffer, (char *)(Fattr->attr_vals.attrlist4_val + LastOffset), len);
          buffer[len] = '\0';

          /* Do not forget that xdr_opaque are aligned on 32bit long words */
          while((len % 4) != 0)
            len += 1;

          LastOffset += len;

          utf8buffer.utf8string_val = buffer;
          utf8buffer.utf8string_len = strlen(buffer);

          utf82uid(&utf8buffer, &(pFSAL_attr->owner));
          pFSAL_attr->asked_attributes |= FSAL_ATTR_OWNER;

          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: On voit le owner %s len = %d", buffer, len);
          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: On voit le owner %d", pFSAL_attr->owner);
          break;

        case FATTR4_OWNER_GROUP:
          memcpy(&len, (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          len = ntohl(len);
          LastOffset += sizeof(u_int);

          memcpy(buffer, (char *)(Fattr->attr_vals.attrlist4_val + LastOffset), len);
          buffer[len] = '\0';

          /* Do not forget that xdr_opaque are aligned on 32bit long words */
          while((len % 4) != 0)
            len += 1;

          LastOffset += len;

          utf8buffer.utf8string_val = buffer;
          utf8buffer.utf8string_len = strlen(buffer);

          utf82gid(&utf8buffer, &(pFSAL_attr->group));
          pFSAL_attr->asked_attributes |= FSAL_ATTR_GROUP;

          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: On voit le owner_group %s len = %d", buffer, len);
          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: On voit le owner_group %d", pFSAL_attr->group);
          break;

        case FATTR4_CHANGE:
          memcpy((char *)&attr_change,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_change));
          pFSAL_attr->chgtime.seconds = (uint32_t) nfs_ntohl64(attr_change);
          pFSAL_attr->chgtime.nseconds = 0;

          pFSAL_attr->change =  nfs_ntohl64(attr_change);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_CHGTIME;
          pFSAL_attr->asked_attributes |= FSAL_ATTR_CHANGE;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_RAWDEV:    /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_rawdev,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_rawdev));
          pFSAL_attr->rawdev.major = (uint32_t) nfs_ntohl64(attr_rawdev.specdata1);
          pFSAL_attr->rawdev.minor = (uint32_t) nfs_ntohl64(attr_rawdev.specdata2);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_RAWDEV;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_SPACE_USED:        /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_space_used,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_space_used));
          pFSAL_attr->spaceused = (uint32_t) nfs_ntohl64(attr_space_used);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_SPACEUSED;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_TIME_ACCESS:       /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_time_access,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_time_access));
          pFSAL_attr->atime.seconds = (uint32_t) nfs_ntohl64(attr_time_access.seconds);
          pFSAL_attr->atime.nseconds = (uint32_t) ntohl(attr_time_access.nseconds);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_ATIME;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_TIME_METADATA:     /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_time_metadata,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_time_metadata));
          pFSAL_attr->ctime.seconds = (uint32_t) nfs_ntohl64(attr_time_metadata.seconds);
          pFSAL_attr->ctime.nseconds = (uint32_t) ntohl(attr_time_metadata.nseconds);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_CTIME;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_TIME_MODIFY:       /* Used only by FSAL_PROXY to reverse convert */
          memcpy((char *)&attr_time_modify,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_time_modify));
          pFSAL_attr->mtime.seconds = (uint32_t) nfs_ntohl64(attr_time_modify.seconds);
          pFSAL_attr->mtime.nseconds = (uint32_t) ntohl(attr_time_modify.nseconds);

          pFSAL_attr->asked_attributes |= FSAL_ATTR_MTIME;
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

        case FATTR4_TIME_ACCESS_SET:
          memcpy((char *)&(attr_time_set.set_it),
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(time_how4));

          LastOffset += sizeof(time_how4);

          if(ntohl(attr_time_set.set_it) == SET_TO_SERVER_TIME4)
            {
              pFSAL_attr->atime.seconds = time(NULL);   /* Use current server's time */
              pFSAL_attr->atime.nseconds = 0;
            }
          else
            {
              /* Read the remaining part of the data */
              memcpy((char *)&(attr_time_set.settime4_u.time),
                     (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                     sizeof(nfstime4));

              //LastOffset += sizeof(nfstime4);
              LastOffset += sizeof(int64_t) + sizeof(uint32_t);

              /* Take care of XDR when dealing with fattr4 */
              attr_time = attr_time_set.settime4_u.time;
              attr_time.seconds = nfs_ntohl64(attr_time.seconds);
              attr_time.nseconds = ntohl(attr_time.nseconds);

              pFSAL_attr->atime.seconds = attr_time.seconds;
              pFSAL_attr->atime.nseconds = attr_time.nseconds;
            }
          pFSAL_attr->asked_attributes |= FSAL_ATTR_ATIME;

          break;

        case FATTR4_TIME_MODIFY_SET:
          memcpy((char *)&(attr_time_set.set_it),
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(time_how4));

          LastOffset += sizeof(time_how4);

          if(ntohl(attr_time_set.set_it) == SET_TO_SERVER_TIME4)
            {
              pFSAL_attr->mtime.seconds = time(NULL);   /* Use current server's time */
              pFSAL_attr->mtime.nseconds = 0;
            }
          else
            {
              /* Read the remaining part of the data */
              memcpy((char *)&(attr_time_set.settime4_u.time),
                     (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                     sizeof(nfstime4));

              /* Take care of XDR when dealing with fattr4 */
              attr_time = attr_time_set.settime4_u.time;
              attr_time.seconds = nfs_ntohl64(attr_time.seconds);
              attr_time.nseconds = ntohl(attr_time.nseconds);

              //LastOffset += sizeof(nfstime4);
              LastOffset += sizeof(int64_t) + sizeof(uint32_t);

              pFSAL_attr->mtime.seconds = attr_time.seconds;
              pFSAL_attr->mtime.nseconds = attr_time.nseconds;
            }

          pFSAL_attr->asked_attributes |= FSAL_ATTR_MTIME;

          break;

        case FATTR4_FILEHANDLE:
          memcpy(&len, (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(u_int));
          len = ntohl(len);
          LastOffset += sizeof(u_int);
          LastOffset += len;
          LogFullDebug(COMPONENT_NFS_V4,
                       "     SATTR: On a demande le filehandle len =%u", len);
          break;

        case FATTR4_RDATTR_ERROR:
          memcpy((char *)&rdattr_error,
                 (char *)(Fattr->attr_vals.attrlist4_val + LastOffset),
                 sizeof(fattr4_rdattr_error));
          rdattr_error = ntohl(rdattr_error);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;

          break;

#ifdef _USE_NFS4_ACL
        case FATTR4_ACL:
          if((rc = nfs4_decode_acl(pFSAL_attr, Fattr, &LastOffset)) != NFS4_OK)
            return rc;

          pFSAL_attr->asked_attributes |= FSAL_ATTR_ACL;
          break;
#endif                          /* _USE_NFS4_ACL */

        default:
          LogFullDebug(COMPONENT_NFS_V4,
                       "      SATTR: Attribut no supporte %d name=%s",
                       attribute_to_set, fattr4tab[attribute_to_set].name);
          LastOffset += fattr4tab[attribute_to_set].size_fattr4;
          /* return NFS4ERR_ATTRNOTSUPP ; *//* Should not stop processing */
          break;
        }                       /* switch */
    }                           /* for */

  return NFS4_OK;
}                               /* nfs4_Fattr_To_FSAL_attr */

/* Error conversion routines */
/**
 * 
 * nfs4_Errno: Converts a cache_inode status to a nfsv4 status.
 * 
 *  Converts a cache_inode status to a nfsv4 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 * 
 * @return the converted NFSv4 status.
 *
 */
nfsstat4 nfs4_Errno(cache_inode_status_t error)
{
  nfsstat4 nfserror= NFS4ERR_INVAL;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS4_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_CACHE_CONTENT_EXISTS:
    case CACHE_INODE_CACHE_CONTENT_EMPTY:
      nfserror = NFS4ERR_SERVERFAULT;
      break;

    case CACHE_INODE_UNAPPROPRIATED_KEY:
      nfserror = NFS4ERR_BADHANDLE;
      break;

    case CACHE_INODE_BAD_TYPE:
      nfserror = NFS4ERR_INVAL;
      break;

    case CACHE_INODE_INVALID_ARGUMENT:
      nfserror = NFS4ERR_PERM;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFS4ERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFS4ERR_EXIST;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFS4ERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFS4ERR_NOENT;
      break;

    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
      nfserror = NFS4ERR_IO;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFS4ERR_ACCESS;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFS4ERR_PERM;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFS4ERR_NOSPC;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFS4ERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFS4ERR_ROFS;
      break;

    case CACHE_INODE_IO_ERROR:
      nfserror = NFS4ERR_IO;
      break;

     case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFS4ERR_NAMETOOLONG;
      break;

    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFS4ERR_STALE;
      break;

    case CACHE_INODE_STATE_CONFLICT:
      nfserror = NFS4ERR_PERM;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFS4ERR_DQUOT;
      break;

    case CACHE_INODE_NOT_SUPPORTED:
      nfserror = NFS4ERR_NOTSUPP;
      break;

    case CACHE_INODE_FSAL_DELAY:
      nfserror = NFS4ERR_DELAY;
      break;

    case CACHE_INODE_FILE_BIG:
      nfserror = NFS4ERR_FBIG;
      break;

    case CACHE_INODE_STATE_ERROR:
      nfserror = NFS4ERR_BAD_STATEID;
      break;

    case CACHE_INODE_BAD_COOKIE:
      nfserror = NFS4ERR_BAD_COOKIE;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_CACHE_CONTENT_ERROR:
    case CACHE_INODE_ASYNC_POST_ERROR:
      /* Should not occur */
      nfserror = NFS4ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs4_Errno */

/**
 * 
 * nfs3_Errno: Converts a cache_inode status to a nfsv3 status.
 * 
 *  Converts a cache_inode status to a nfsv3 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 * 
 * @return the converted NFSv3 status.
 *
 */
nfsstat3 nfs3_Errno(cache_inode_status_t error)
{
  nfsstat3 nfserror= NFS3ERR_INVAL;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS3_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_CACHE_CONTENT_EXISTS:
    case CACHE_INODE_CACHE_CONTENT_EMPTY:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFS3ERR_IO but was set non-retryable",
              error);
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_INVALID_ARGUMENT:
      nfserror = NFS3ERR_INVAL;
      break;

    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_CACHE_CONTENT_ERROR:
                                         /** @todo: Check if this works by making stress tests */
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_FSAL_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFS3ERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFS3ERR_EXIST;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFS3ERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFS3ERR_NOENT;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFS3ERR_ACCES;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFS3ERR_PERM;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFS3ERR_NOSPC;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFS3ERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFS3ERR_ROFS;
      break;

    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFS3ERR_STALE;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFS3ERR_DQUOT;
      break;

    case CACHE_INODE_BAD_TYPE:
      nfserror = NFS3ERR_BADTYPE;
      break;

    case CACHE_INODE_NOT_SUPPORTED:
      nfserror = NFS3ERR_NOTSUPP;
      break;

    case CACHE_INODE_FSAL_DELAY:
      nfserror = NFS3ERR_JUKEBOX;
      break;

    case CACHE_INODE_IO_ERROR:
        LogCrit(COMPONENT_NFSPROTO,
                "Error CACHE_INODE_IO_ERROR converted to NFS3ERR_IO but was set non-retryable");
      nfserror = NFS3ERR_IO;
      break;

    case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFS3ERR_NAMETOOLONG;
      break;

    case CACHE_INODE_FILE_BIG:
      nfserror = NFS3ERR_FBIG;
      break;

    case CACHE_INODE_BAD_COOKIE:
      nfserror = NFS3ERR_BAD_COOKIE;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
        /* Should not occur */
        LogDebug(COMPONENT_NFSPROTO,
                 "Line %u should never be reached in nfs3_Errno for cache_status=%u",
                 __LINE__, error);
      nfserror = NFS3ERR_INVAL;
      break;
    }

  return nfserror;
}                               /* nfs3_Errno */

/**
 * 
 * nfs2_Errno: Converts a cache_inode status to a nfsv2 status.
 * 
 *  Converts a cache_inode status to a nfsv2 status.
 *
 * @param error  [IN] Input cache inode ewrror.
 * 
 * @return the converted NFSv2 status.
 *
 */
nfsstat2 nfs2_Errno(cache_inode_status_t error)
{
  nfsstat2 nfserror= NFSERR_IO;

  switch (error)
    {
    case CACHE_INODE_SUCCESS:
      nfserror = NFS_OK;
      break;

    case CACHE_INODE_MALLOC_ERROR:
    case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
    case CACHE_INODE_GET_NEW_LRU_ENTRY:
    case CACHE_INODE_UNAPPROPRIATED_KEY:
    case CACHE_INODE_INIT_ENTRY_FAILED:
    case CACHE_INODE_BAD_TYPE:
    case CACHE_INODE_CACHE_CONTENT_EXISTS:
    case CACHE_INODE_CACHE_CONTENT_EMPTY:
    case CACHE_INODE_INSERT_ERROR:
    case CACHE_INODE_LRU_ERROR:
    case CACHE_INODE_HASH_SET_ERROR:
    case CACHE_INODE_INVALID_ARGUMENT:
      LogCrit(COMPONENT_NFSPROTO,
              "Error %u converted to NFSERR_IO but was set non-retryable",
              error);
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_NOT_A_DIRECTORY:
      nfserror = NFSERR_NOTDIR;
      break;

    case CACHE_INODE_ENTRY_EXISTS:
      nfserror = NFSERR_EXIST;
      break;

    case CACHE_INODE_FSAL_ERROR:
    case CACHE_INODE_CACHE_CONTENT_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_FSAL_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_DIR_NOT_EMPTY:
      nfserror = NFSERR_NOTEMPTY;
      break;

    case CACHE_INODE_NOT_FOUND:
      nfserror = NFSERR_NOENT;
      break;

    case CACHE_INODE_FSAL_EACCESS:
      nfserror = NFSERR_ACCES;
      break;

    case CACHE_INODE_NO_SPACE_LEFT:
      nfserror = NFSERR_NOSPC;
      break;

    case CACHE_INODE_FSAL_EPERM:
    case CACHE_INODE_FSAL_ERR_SEC:
      nfserror = NFSERR_PERM;
      break;

    case CACHE_INODE_IS_A_DIRECTORY:
      nfserror = NFSERR_ISDIR;
      break;

    case CACHE_INODE_READ_ONLY_FS:
      nfserror = NFSERR_ROFS;
      break;

    case CACHE_INODE_DEAD_ENTRY:
    case CACHE_INODE_FSAL_ESTALE:
      nfserror = NFSERR_STALE;
      break;

    case CACHE_INODE_QUOTA_EXCEEDED:
      nfserror = NFSERR_DQUOT;
      break;

    case CACHE_INODE_IO_ERROR:
      LogCrit(COMPONENT_NFSPROTO,
              "Error CACHE_INODE_IO_ERROR converted to NFSERR_IO but was set non-retryable");
      nfserror = NFSERR_IO;
      break;

    case CACHE_INODE_NAME_TOO_LONG:
      nfserror = NFSERR_NAMETOOLONG;
      break;

    case CACHE_INODE_INCONSISTENT_ENTRY:
    case CACHE_INODE_HASH_TABLE_ERROR:
    case CACHE_INODE_STATE_CONFLICT:
    case CACHE_INODE_ASYNC_POST_ERROR:
    case CACHE_INODE_STATE_ERROR:
    case CACHE_INODE_NOT_SUPPORTED:
    case CACHE_INODE_FSAL_DELAY:
    case CACHE_INODE_BAD_COOKIE:
    case CACHE_INODE_FILE_BIG:
        /* Should not occur */
      LogDebug(COMPONENT_NFSPROTO,
               "Line %u should never be reached in nfs2_Errno", __LINE__);
      nfserror = NFSERR_IO;
      break;
    }

  return nfserror;
}                               /* nfs2_Errno */

/**
 * 
 * nfs4_AllocateFH: Allocates a buffer to be used for storing a NFSv4 filehandle.
 * 
 * Allocates a buffer to be used for storing a NFSv4 filehandle.
 *
 * @param fh [INOUT] the filehandle to manage.
 * 
 * @return NFS4_OK if successful, NFS3ERR_SERVERFAULT, NFS4ERR_RESOURCE or NFS4ERR_STALE  otherwise.
 *
 */
int nfs4_AllocateFH(nfs_fh4 * fh)
{
  char __attribute__ ((__unused__)) funcname[] = "AllocateFH4";

  if(fh == NULL)
    return NFS4ERR_SERVERFAULT;

  /* Allocating the filehandle in memory */
  fh->nfs_fh4_len = sizeof(file_handle_v4_t);
  if((fh->nfs_fh4_val = (char *)Mem_Alloc_Label(fh->nfs_fh4_len,
                                                "nfs4_AllocateFH")) == NULL)
    {
      LogError(COMPONENT_NFS_V4, ERR_SYS, ERR_MALLOC, errno);
      return NFS4ERR_RESOURCE;
    }

  memset((char *)fh->nfs_fh4_val, 0, fh->nfs_fh4_len);

  return NFS4_OK;
}                               /* nfs4_AllocateFH */

/**
 *
 * nfs4_MakeCred
 *
 * This routine fills in the pcontext field in the compound data.
 *
 * @param pfh [INOUT] pointer to compound data to be used. NOT YET IMPLEMENTED
 *
 * @return NFS4_OK if successful, NFS4ERR_WRONGSEC otherwise.
 *
 */
int nfs4_MakeCred(compound_data_t * data)
{
  exportlist_client_entry_t related_client;
  nfs_worker_data_t *pworker = NULL;
  struct user_cred user_credentials;

  pworker = (nfs_worker_data_t *) data->pclient->pworker;

  if (get_req_uid_gid(data->reqp,
                      data->pexport,
                      &user_credentials) == FALSE)
    return NFS4ERR_WRONGSEC;

  LogFullDebug(COMPONENT_DISPATCH,
               "nfs4_MakeCred about to call nfs_export_check_access");
  if(nfs_export_check_access(&pworker->hostaddr,
                             data->reqp,
                             data->pexport,
                             nfs_param.core_param.program[P_NFS],
                             nfs_param.core_param.program[P_MNT],
                             pworker->ht_ip_stats,
                             &pworker->ip_stats_pool,
                             &related_client,
                             &user_credentials,
                             FALSE) /* So check_access() doesn't deny based on whether this is a RO export. */
     == FALSE)
    return NFS4ERR_WRONGSEC;

  if(nfs_build_fsal_context(data->reqp,
                            data->pexport,
                            data->pcontext,
                            &user_credentials) == FALSE)
    return NFS4ERR_WRONGSEC;

  return NFS4_OK;
}                               /* nfs4_MakeCred */

/* Create access mask based on given access operation. Both mode and ace4
 * mask are encoded. */
fsal_accessflags_t nfs_get_access_mask(uint32_t op, fsal_attrib_list_t *pattr)
{
  fsal_accessflags_t access_mask = 0;

  switch(op)
    {
      case ACCESS3_READ:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_R_OK);
        if(IS_FSAL_DIR(pattr->type))
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_DATA);
      break;

      case ACCESS3_LOOKUP:
        if(!IS_FSAL_DIR(pattr->type))
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_X_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_LIST_DIR);
      break;

      case ACCESS3_MODIFY:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        if(IS_FSAL_DIR(pattr->type))
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_WRITE_DATA);
      break;

      case ACCESS3_EXTEND:
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        if(IS_FSAL_DIR(pattr->type))
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_ADD_FILE |
                                            FSAL_ACE_PERM_ADD_SUBDIRECTORY);
        else
          access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_APPEND_DATA);
      break;

      case ACCESS3_DELETE:
        if(!IS_FSAL_DIR(pattr->type))
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_W_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_DELETE_CHILD);
      break;

      case ACCESS3_EXECUTE:
        if(IS_FSAL_DIR(pattr->type))
          break;
        access_mask |= FSAL_MODE_MASK_SET(FSAL_X_OK);
        access_mask |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_EXECUTE);
      break;
    }

  return access_mask;
}

void nfs3_access_debug(char *label, uint32_t access)
{
  LogDebug(COMPONENT_NFSPROTO, "%s=%s,%s,%s,%s,%s,%s",
           label,
           FSAL_TEST_MASK(access, ACCESS3_READ) ? "READ" : "-",
           FSAL_TEST_MASK(access, ACCESS3_LOOKUP) ? "LOOKUP" : "-",
           FSAL_TEST_MASK(access, ACCESS3_MODIFY) ? "MODIFY" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXTEND) ? "EXTEND" : "-",
           FSAL_TEST_MASK(access, ACCESS3_DELETE) ? "DELETE" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXECUTE) ? "EXECUTE" : "-");
}

void nfs4_access_debug(char *label, uint32_t access, fsal_aceperm_t v4mask)
{
  LogDebug(COMPONENT_NFSPROTO, "%s=%s,%s,%s,%s,%s,%s",
           label,
           FSAL_TEST_MASK(access, ACCESS3_READ) ? "READ" : "-",
           FSAL_TEST_MASK(access, ACCESS3_LOOKUP) ? "LOOKUP" : "-",
           FSAL_TEST_MASK(access, ACCESS3_MODIFY) ? "MODIFY" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXTEND) ? "EXTEND" : "-",
           FSAL_TEST_MASK(access, ACCESS3_DELETE) ? "DELETE" : "-",
           FSAL_TEST_MASK(access, ACCESS3_EXECUTE) ? "EXECUTE" : "-");

  if(v4mask)
    LogDebug(COMPONENT_NFSPROTO, "v4mask=%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_DATA)		 ? 'r':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_DATA)		 ? 'w':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_EXECUTE)		 ? 'x':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_ADD_SUBDIRECTORY)    ? 'm':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_NAMED_ATTR)	 ? 'n':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_NAMED_ATTR) 	 ? 'N':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_DELETE_CHILD) 	 ? 'p':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_ATTR)		 ? 't':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_ATTR)		 ? 'T':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_DELETE)		 ? 'd':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_READ_ACL) 		 ? 'c':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_ACL)		 ? 'C':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_WRITE_OWNER)	 ? 'o':'-',
             FSAL_TEST_MASK(v4mask, FSAL_ACE_PERM_SYNCHRONIZE)	 ? 'z':'-');
}
