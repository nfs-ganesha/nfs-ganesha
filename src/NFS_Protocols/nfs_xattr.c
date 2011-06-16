/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
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
 * \file    nfs4_xattr.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   Routines used for managing the NFS2/3 xattrs
 *
 * nfs_xattr.c: Routines used for managing the NFS2/3 xattrs
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
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "cache_content.h"

static void nfs_set_times_current(fattr3 * attrs)
{
  time_t now = time(NULL);

  attrs->atime.seconds = now;
  attrs->atime.nseconds = 0;

  attrs->mtime.seconds = now;
  attrs->mtime.nseconds = 0;

  attrs->ctime.seconds = now;
  attrs->ctime.nseconds = 0;
}

static void fsal_set_times_current(fsal_attrib_list_t * attrs)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);

  attrs->atime.seconds = tv.tv_sec;
  attrs->atime.nseconds = 1000 * tv.tv_usec;

  attrs->mtime.seconds = tv.tv_sec;
  attrs->mtime.nseconds = 1000 * tv.tv_usec;

  attrs->ctime.seconds = tv.tv_sec;
  attrs->ctime.nseconds = 1000 * tv.tv_usec;
}

/**
 *
 * nfs_Is_XattrD_Name: returns true is the string given as input is the name of an xattr object.
 *
 * Returns true is the string given as input is the name of an xattr object and returns the name of the object
 *
 * @param strname    [IN]  the name that is to be tested
 * @param objectname [OUT] if strname is related to a xattr, contains the name of the related object
 *
 * @return TRUE if strname is a xattr, FALSE otherwise
 *
 */
int nfs_XattrD_Name(char *strname, char *objectname)
{
  if(strname == NULL)
    return 0;

  if(!strncmp(strname, XATTRD_NAME, XATTRD_NAME_LEN))
    {
      memcpy(objectname, (char *)(strname + XATTRD_NAME_LEN),
             strlen(strname) - XATTRD_NAME_LEN + 1);
      return 1;
    }

  return 0;
}                               /* nfs_Is_XattrD_Name */

/**
 * nfs3_fh_to_xattrfh: builds the fh to the xattrs ghost directory
 *
 * @param pfhin  [IN]  input file handle (the object whose xattr fh is queryied)
 * @param pfhout [OUT] output file handle
 *
 * @return NFS4_OK
 *
 */
nfsstat3 nfs3_fh_to_xattrfh(nfs_fh3 * pfhin, nfs_fh3 * pfhout)
{
  file_handle_v3_t *pfile_handle = NULL;

  if(pfhin != pfhout)
    {
      memcpy(pfhout->data.data_val, pfhin->data.data_val, pfhin->data.data_len);
      pfhout->data.data_len = pfhin->data.data_len;
    }

  pfile_handle = (file_handle_v3_t *) (pfhout->data.data_val);

  /* the following choice is made for xattr: the field xattr_pos contains :
   * - 0 if the FH is related to an actual FH object
   * - 1 if the FH is the one for the xattr ghost directory
   * - a value greater than 1 if the fh is related to a ghost file in ghost xattr directory that represents a xattr. The value is then equal
   *   to the xattr_id + 1 (see how FSAL manages xattrs for meaning of this field). This limits the number of xattrs per object to 254.
   */
  pfile_handle->xattr_pos = 1;  /**< 1 = xattr ghost directory */

  return NFS3_OK;
}                               /* nfs3_fh_to_xattrfh */

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
int nfs3_FSALattr_To_XattrDir(exportlist_t * pexport,   /* In: the related export entry */
                              fsal_attrib_list_t * FSAL_attr,   /* In: file attributes */
                              fattr3 * Fattr)   /* Out: file attributes */
{
  if(FSAL_attr == NULL || Fattr == NULL)
    return 0;

  Fattr->type = NF3DIR;  /** Xattr directory is indeed a directory */

  /* r-xr-xr-x (cannot create or remove xattrs, except if HAVE_XATTR_CREATE is defined) */
#ifdef HAVE_XATTR_CREATE
  Fattr->mode = 0755;
#else
  Fattr->mode = 0555;
#endif

  Fattr->nlink = 2;             /* like a directory */
  Fattr->uid = FSAL_attr->owner;
  Fattr->gid = FSAL_attr->group;
  Fattr->size = DEV_BSIZE;
  Fattr->used = DEV_BSIZE;

  Fattr->rdev.specdata1 = 0;
  Fattr->rdev.specdata2 = 0;

  /* in NFSv3, we only keeps fsid.major, casted into an nfs_uint64 */
  Fattr->fsid = (nfs3_uint64) pexport->filesystem_id.major;

  Fattr->fileid = (0xFFFFFFFF & ~(FSAL_attr->fileid)) - 1;        /* xattr_pos = 1 => Parent Xattrd */

  /* set current time, to force the client refreshing its xattr dir */
  nfs_set_times_current(Fattr);

  return 1;
}                               /* nfs3_FSALattr_To_XattrDir */

/**
 *
 * nfs_SetPostOpAttrDir: Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
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
int nfs_SetPostOpXAttrDir(fsal_op_context_t * pcontext,
                          exportlist_t * pexport,
                          fsal_attrib_list_t * pfsal_attr, post_op_attr * presult)
{
  if(pfsal_attr == NULL)
    {
      presult->attributes_follow = FALSE;
      return 0;
    }

  if(nfs3_FSALattr_To_XattrDir
     (pexport, pfsal_attr, &(presult->post_op_attr_u.attributes)) == 0)
    presult->attributes_follow = FALSE;
  else
    presult->attributes_follow = TRUE;

  return 0;
}                               /* nfs_SetPostOpXAttrDir */

/**
 *
 * nfs_SetPostOpAttrDir: Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * @param pexport    [IN]  the related export entry
 * @param pfsal_attr [IN]  FSAL attributes for xattr
 * @param pattr      [OUT] NFSv3 PostOp structure attributes.
 *
 * @return 0 in all cases (making it a void function maybe a good idea)
 *
 */
int nfs_SetPostOpXAttrFile(fsal_op_context_t * pcontext,
                           exportlist_t * pexport,
                           fsal_attrib_list_t * pfsal_attr, post_op_attr * presult)
{
  if(pfsal_attr == NULL)
    {
      presult->attributes_follow = FALSE;
      return 0;
    }

  if(nfs3_FSALattr_To_Fattr(pexport, pfsal_attr, &(presult->post_op_attr_u.attributes))
     == 0)
    presult->attributes_follow = FALSE;
  else
  {
    nfs_set_times_current(&(presult->post_op_attr_u.attributes));
    presult->attributes_follow = TRUE;
  }

  return 0;
}                               /* nfs_SetPostOpXAttrFile */

/**
 * nfs3_Access_Xattr: Implements NFSPROC3_ACCESS for xattr objects
 *
 * Implements NFSPROC3_ACCESS.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */

int nfs3_Access_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      fsal_op_context_t * pcontext,
                      cache_inode_client_t * pclient,
                      hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  fsal_attrib_list_t attr;
  int rc;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  cache_entry_t *pentry = NULL;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;

  /* to avoid setting it on each error case */
  pres->res_access3.ACCESS3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  if((pentry = nfs_FhandleToCache(NFS_V3,
                                  NULL,
                                  &(parg->arg_access3.object),
                                  NULL,
                                  NULL,
                                  &(pres->res_access3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_access3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* Rebuild the FH */
  pfile_handle = (file_handle_v3_t *) (parg->arg_access3.object.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  /* retrieve entry attributes  */
  if(pfile_handle->xattr_pos == 0)
    {
      /* should not occur */
      pres->res_access3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }
  else if(pfile_handle->xattr_pos == 1)
    {

      pres->res_access3.ACCESS3res_u.resok.access =
          parg->arg_access3.access & ~(ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE);

      /* Build directory attributes */
      nfs_SetPostOpXAttrDir(pcontext, pexport,
                            &attr,
                            &(pres->res_access3.ACCESS3res_u.resok.obj_attributes));

    }
  else                          /* named attribute */
    {
      fsal_status_t fsal_status;
      fsal_attrib_list_t xattrs;
      fsal_accessflags_t access_mode;

      access_mode = 0;
      if(parg->arg_access3.access & ACCESS3_READ)
        access_mode |= FSAL_R_OK;

      if(parg->arg_access3.access & (ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE))
        access_mode |= FSAL_W_OK;

      if(parg->arg_access3.access & ACCESS3_LOOKUP)
        access_mode |= FSAL_X_OK;

      xattrs.asked_attributes = pclient->attrmask;
      fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, xattr_id, &xattrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_access3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          return NFS_REQ_OK;
        }

      fsal_status = FSAL_test_access(pcontext, access_mode, &xattrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          if(fsal_status.major == ERR_FSAL_ACCESS)
            {
              pres->res_access3.ACCESS3res_u.resok.access = 0;

              /* we have to check read/write permissions */
              if(!FSAL_IS_ERROR(FSAL_test_access(pcontext, FSAL_R_OK, &xattrs)))
                pres->res_access3.ACCESS3res_u.resok.access |= ACCESS3_READ;
              if(!FSAL_IS_ERROR(FSAL_test_access(pcontext, FSAL_W_OK, &xattrs)))
                pres->res_access3.ACCESS3res_u.resok.access |=
                    ACCESS3_MODIFY | ACCESS3_EXTEND;
            }
          else
            {
              /* this is an error */
              nfs_SetPostOpXAttrFile(pcontext, pexport,
                                     &xattrs,
                                     &(pres->res_access3.ACCESS3res_u.resfail.
                                       obj_attributes));

              pres->res_access3.status =
                  nfs3_Errno(cache_inode_error_convert(fsal_status));
              return NFS_REQ_OK;
            }

        }
      else                      /* access granted */
        {
          pres->res_access3.ACCESS3res_u.resok.access = parg->arg_access3.access;
        }

      nfs_SetPostOpXAttrFile(pcontext, pexport,
                             &xattrs,
                             &(pres->res_access3.ACCESS3res_u.resok.obj_attributes));
    }

  pres->res_access3.status = NFS3_OK;

  return NFS_REQ_OK;

}                               /* nfs3_Access_Xattr */

/**
 * nfs3_Lookup_Xattr: Implements NFSPROC3_LOOKUP for xattr ghost directory
 *
 * Implements NFSPROC3_LOOKUP.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */

int nfs3_Lookup_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      fsal_op_context_t * pcontext,
                      cache_inode_client_t * pclient,
                      hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  int rc;
  cache_inode_status_t cache_status;
  fsal_attrib_list_t attr, xattr_attrs;
  fsal_name_t name;
  fsal_status_t fsal_status;
  unsigned int xattr_id = 0;
  fsal_handle_t *pfsal_handle = NULL;
  char *strpath = parg->arg_lookup3.what.name;
  file_handle_v3_t *pfile_handle = NULL;
  cache_entry_t *pentry_dir = NULL;

  if((pentry_dir = nfs_FhandleToCache(NFS_V3,
                                      NULL,
                                      &(parg->arg_lookup3.what.dir),
                                      NULL,
                                      NULL,
                                      &(pres->res_lookup3.status),
                                      NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry_dir, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_lookup3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  if((cache_status = cache_inode_error_convert(FSAL_str2name(strpath,
                                                             MAXNAMLEN,
                                                             &name))) !=
     CACHE_INODE_SUCCESS)
    {
      pres->res_lookup3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* Try to get a FSAL_XAttr of that name */
  fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &name, pcontext, &xattr_id);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_lookup3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  /* Build FH */
  if((pres->res_lookup3.LOOKUP3res_u.resok.object.data.data_val =
      Mem_Alloc(NFS3_FHSIZE)) == NULL)
    pres->res_lookup3.status = NFS3ERR_INVAL;

  if(nfs3_FSALToFhandle((nfs_fh3 *) & (pres->res_lookup3.LOOKUP3res_u.resok.object.data),
                        pfsal_handle, pexport))
    {
      pres->res_lookup3.status =
          nfs3_fh_to_xattrfh((nfs_fh3 *) &
                             (pres->res_lookup3.LOOKUP3res_u.resok.object.data),
                             (nfs_fh3 *) & (pres->res_lookup3.LOOKUP3res_u.resok.object.
                                            data));

      /* Retrieve xattr attributes */
      xattr_attrs.asked_attributes = pclient->attrmask;
      fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, xattr_id, &xattr_attrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_lookup3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          return NFS_REQ_OK;
        }

      nfs_SetPostOpXAttrFile(pcontext, pexport,
                             &xattr_attrs,
                             &(pres->res_lookup3.LOOKUP3res_u.resok.obj_attributes));

      /* Build directory attributes */
      nfs_SetPostOpXAttrDir(pcontext, pexport,
                            &attr,
                            &(pres->res_lookup3.LOOKUP3res_u.resok.dir_attributes));

      pres->res_lookup3.status = NFS3_OK;
    }

  /* if */
  /* Rebuild the FH */
  pfile_handle =
      (file_handle_v3_t *) (pres->res_lookup3.LOOKUP3res_u.resok.object.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  pfile_handle->xattr_pos = xattr_id + 2;

  return NFS_REQ_OK;
}                               /* nfs3_Lookup_Xattr */

/**
 * nfs3_Readdir_Xattr: Implements NFSPROC3_READDIR for xattr ghost directory
 *
 * Implements NFSPROC3_READDIR.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */

int nfs3_Readdir_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       fsal_op_context_t * pcontext,
                       cache_inode_client_t * pclient,
                       hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN];
  typedef char fh3_buffer_item_t[NFS3_FHSIZE];

  unsigned int delta = 0;
  cache_entry_t *dir_pentry = NULL;
  unsigned long dircount;
  unsigned long maxcount = 0;
  fsal_attrib_list_t dir_attr;
  unsigned int begin_cookie;
  unsigned int xattr_cookie;
  cookieverf3 cookie_verifier;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  int rc;
  unsigned int i = 0;
  unsigned int num_entries = 0;
  unsigned long space_used;
  unsigned long estimated_num_entries;
  unsigned long asked_num_entries;
  unsigned int eod_met;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  entry_name_array_item_t *entry_name_array = NULL;
  fh3_buffer_item_t *fh3_array = NULL;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdir3.count;
  begin_cookie = (unsigned int)parg->arg_readdir3.cookie;
  space_used = sizeof(READDIRPLUS3resok);
  estimated_num_entries = dircount / sizeof(entry3);

  /* BUGAZOMEU : rajouter acces direct au DIR_CONTINUE */
  if((dir_pentry = nfs_FhandleToCache(preq->rq_vers,
                                      NULL,
                                      &(parg->arg_readdir3.dir),
                                      NULL,
                                      NULL,
                                      &(pres->res_readdir3.status),
                                      NULL,
                                      &dir_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* return NFS_REQ_DROP ; */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(dir_pentry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_readdir3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_readdir3.dir.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos;

  if(xattr_id != 1)             /* If this is not the xattrd */
    {
      pres->res_readdir3.status = NFS3ERR_NOTDIR;
      return NFS_REQ_OK;
    }

  /* Cookie verifier management */
  memset(cookie_verifier, 0, sizeof(cookieverf3));

  /*
   * If cookie verifier is used, then an non-trivial value is
   * returned to the client         This value is the mtime of
   * the directory. If verifier is unused (as in many NFS
   * Servers) then only a set of zeros is returned (trivial
   * value)
   */

  if(pexport->UseCookieVerifier)
    memcpy(cookie_verifier, &(dir_attr.mtime), sizeof(dir_attr.mtime));

  /*
   * nothing to do if != 0 because the area is already full of
   * zero
   */

  if(pexport->UseCookieVerifier && (begin_cookie != 0))
    {
      /*
       * Not the first call, so we have to check the cookie
       * verifier
       */
      if(memcmp(cookie_verifier, parg->arg_readdir3.cookieverf, NFS3_COOKIEVERFSIZE) != 0)
        {
          pres->res_readdir3.status = NFS3ERR_BAD_COOKIE;

          return NFS_REQ_OK;
        }
    }

  pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;
  pres->res_readdir3.READDIR3res_u.resok.reply.eof = FALSE;

  /* How many entries will we retry from cache_inode ? */
  if(begin_cookie > 1)
    {
      asked_num_entries = estimated_num_entries;
      xattr_cookie = begin_cookie - 2;
    }
  else
    {
      asked_num_entries = ((estimated_num_entries > 2) ? estimated_num_entries - 2 : 0);        /* Keep space for '.' and '..' */
      xattr_cookie = 0;
    }

  /* A definition that will be very useful to avoid very long names for variables */
#define RES_READDIR_REPLY pres->res_readdir3.READDIR3res_u.resok.reply

  /* Used FSAL extended attributes functions */
  fsal_status = FSAL_ListXAttrs(pfsal_handle,
                                xattr_cookie,
                                pcontext,
                                xattrs_tab, asked_num_entries, &nb_xattrs_read, &eod_met);
  if(!FSAL_IS_ERROR(fsal_status))
    {
      if((nb_xattrs_read == 0) && (begin_cookie > 1))
        {
          pres->res_readdir3.status = NFS3_OK;
          pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;
          pres->res_readdir3.READDIR3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpXAttrDir(pcontext,
                                pexport,
                                NULL,
                                &(pres->res_readdir3.READDIR3res_u.resok.dir_attributes));

          memcpy(pres->res_readdir3.READDIR3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
        }
      else
        {
          /* Allocation of the structure for reply */
          entry_name_array =
              (entry_name_array_item_t *) Mem_Alloc_Label(estimated_num_entries *
                                                          (FSAL_MAX_NAME_LEN + 1),
                                                          "entry_name_array in nfs3_Readdir");

          if(entry_name_array == NULL)
            {
              return NFS_REQ_DROP;
            }

          pres->res_readdir3.READDIR3res_u.resok.reply.entries =
              (entry3 *) Mem_Alloc_Label(estimated_num_entries * sizeof(entry3),
                                         "READDIR3res_u.resok.reply.entries");

          if(pres->res_readdir3.READDIR3res_u.resok.reply.entries == NULL)
            {
              Mem_Free((char *)entry_name_array);
              return NFS_REQ_DROP;
            }

          /* Allocation of the file handles */

          fh3_array =
              (fh3_buffer_item_t *) Mem_Alloc_Label(estimated_num_entries * NFS3_FHSIZE,
                                                    "Filehandle V3 in nfs3_Readdir");

          if(fh3_array == NULL)
            {
              Mem_Free((char *)entry_name_array);
              Mem_Free(pres->res_readdir3.READDIR3res_u.resok.reply.entries);
              pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;
              return NFS_REQ_DROP;
            }

          delta = 0;

          /* manage . and .. */
          if(begin_cookie == 0)
            {
              /* Fill in '.' */
              if(estimated_num_entries > 0)
                {
                  RES_READDIR_REPLY.entries[0].fileid = (0xFFFFFFFF & ~(dir_attr.fileid)) - 1;    /* xattr_pos = 1 => Parent Xattrd */

                  RES_READDIR_REPLY.entries[0].name = entry_name_array[0];
                  strcpy(RES_READDIR_REPLY.entries[0].name, ".");
                  RES_READDIR_REPLY.entries[0].cookie = 1;

                  delta += 1;
                }

            }

          /* Fill in '..' */
          if(begin_cookie <= 1)
            {
              if(estimated_num_entries > delta)
                {
                  RES_READDIR_REPLY.entries[delta].fileid = (0xFFFFFFFF & ~(dir_attr.fileid)) - delta;    /* xattr_pos > 1 => attribute */

                  RES_READDIR_REPLY.entries[delta].name = entry_name_array[delta];
                  strcpy(RES_READDIR_REPLY.entries[delta].name, "..");
                  RES_READDIR_REPLY.entries[delta].cookie = 2;

                  RES_READDIR_REPLY.entries[0].nextentry =
                      &(RES_READDIR_REPLY.entries[delta]);

                  if(num_entries > delta + 1)   /* not 0 ??? */
                    RES_READDIR_REPLY.entries[delta].nextentry =
                        &(RES_READDIR_REPLY.entries[delta + 1]);
                  else
                    RES_READDIR_REPLY.entries[delta].nextentry = NULL;

                  delta += 1;
                }
            }
          /* if( begin_cookie == 0 ) */
          for(i = delta; i < nb_xattrs_read + delta; i++)
            {
              unsigned long needed;

              /* dircount is the size without the FH and attributes overhead, so entry3 is used intead of entryplus3 */
              needed =
                  sizeof(entry3) +
                  ((strlen(xattrs_tab[i - delta].xattr_name.name) + 3) & ~3);

              if((space_used += needed) > maxcount)
                {
                  if(i == delta)
                    {
                      /*
                       * Not enough room to make even a single reply
                       */
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);
                      Mem_Free(pres->res_readdir3.READDIR3res_u.resok.reply.entries);
                      pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;

                      pres->res_readdir3.status = NFS3ERR_TOOSMALL;

                      return NFS_REQ_OK;
                    }
                  break;        /* Make post traitement */
                }
              RES_READDIR_REPLY.entries[i].fileid =
                  0xFFFFFFFF & xattrs_tab[i - delta].attributes.fileid;
              FSAL_name2str(&xattrs_tab[i - delta].xattr_name, entry_name_array[i],
                            FSAL_MAX_NAME_LEN);
              RES_READDIR_REPLY.entries[i].name = entry_name_array[i];

              RES_READDIR_REPLY.entries[i].cookie =
                  xattrs_tab[i - delta].xattr_cookie + 2;

              RES_READDIR_REPLY.entries[i].nextentry = NULL;
              if(i != 0)
                RES_READDIR_REPLY.entries[i - 1].nextentry =
                    &(RES_READDIR_REPLY.entries[i]);

            }                   /* for */
        }                       /* else */

      if(eod_met)
        RES_READDIR_REPLY.eof = TRUE;
      else
        RES_READDIR_REPLY.eof = FALSE;

      nfs_SetPostOpXAttrDir(pcontext,
                            pexport,
                            &dir_attr,
                            &(pres->res_readdir3.READDIR3res_u.resok.dir_attributes));
      memcpy(pres->res_readdir3.READDIR3res_u.resok.cookieverf, cookie_verifier,
             sizeof(cookieverf3));

      pres->res_readdir3.status = NFS3_OK;

      return NFS_REQ_OK;
    }

  /* if( !FSAL_IS_ERROR( fsal_status ) ) */
  /* Is this point is reached, then there is an error */
  pres->res_readdir3.status = NFS3ERR_IO;

  /*  Set failed status */
  nfs_SetFailedStatus(pcontext,
                      pexport,
                      NFS_V3,
                      cache_status,
                      NULL,
                      &pres->res_readdir3.status,
                      dir_pentry,
                      &(pres->res_readdir3.READDIR3res_u.resfail.dir_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs3_Readdir_Xattr */

/**
 * nfs3_Write_Xattr: Implements NFSPROC3_WRITE for xattr ghost files
 *
 * Implements NFSPROC3_WRITE.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Create_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      fsal_op_context_t * pcontext,
                      cache_inode_client_t * pclient,
                      hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *parent_pentry = NULL;
  fsal_attrib_list_t pre_attr;
  fsal_attrib_list_t post_attr;
  fsal_attrib_list_t attr_attrs;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_name_t attr_name = FSAL_NAME_INITIALIZER;
  fsal_status_t fsal_status;
  file_handle_v3_t *p_handle_out;
  unsigned int attr_id;
  int rc;
  char empty_buff[16] = "";
  /* alias to clear code */
  CREATE3resok *resok = &pres->res_create3.CREATE3res_u.resok;

  if((parent_pentry = nfs_FhandleToCache(preq->rq_vers,
                                         NULL,
                                         &(parg->arg_create3.where.dir),
                                         NULL,
                                         &(pres->res_dirop2.status),
                                         NULL,
                                         NULL,
                                         &pre_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Get the associated FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(parent_pentry, &cache_status);

  /* convert attr name to FSAL name */
  FSAL_str2name(parg->arg_create3.where.name, FSAL_MAX_NAME_LEN, &attr_name);

  /* set empty attr */
  fsal_status = FSAL_SetXAttrValue(pfsal_handle,
                                   &attr_name,
                                   pcontext, empty_buff, sizeof(empty_buff), TRUE);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  /* get attr id */
  fsal_status = FSAL_GetXAttrIdByName(pfsal_handle, &attr_name, pcontext, &attr_id);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  attr_attrs.asked_attributes = pclient->attrmask;
  fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, attr_id, &attr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  /* Build file handle */

  if((resok->obj.post_op_fh3_u.handle.data.data_val = Mem_Alloc(NFS3_FHSIZE)) == NULL)
    {
      pres->res_create3.status = NFS3ERR_IO;
      return NFS_REQ_OK;
    }

  /* Set Post Op Fh3 structure */
  if(nfs3_FSALToFhandle(&resok->obj.post_op_fh3_u.handle, pfsal_handle, pexport) == 0)
    {
      Mem_Free((char *)(resok->obj.post_op_fh3_u.handle.data.data_val));
      pres->res_create3.status = NFS3ERR_BADHANDLE;
      return NFS_REQ_OK;
    }

  /* Turn the nfs FH into something readable */
  p_handle_out = (file_handle_v3_t *) (resok->obj.post_op_fh3_u.handle.data.data_val);

  /* xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   */
  p_handle_out->xattr_pos = attr_id + 2;

  resok->obj.handle_follows = TRUE;
  resok->obj.post_op_fh3_u.handle.data.data_len = sizeof(file_handle_v3_t);

  /* set current time (the file is new) */
  fsal_set_times_current(&attr_attrs);

  /* Set Post Op attrs */
  nfs_SetPostOpXAttrFile(pcontext, pexport, &attr_attrs, &resok->obj_attributes);

  /* We assume that creating xattr did not change related entry attrs.
   * Just update times for ghost directory.
   */
  post_attr = pre_attr;

  /* set current time, to force the client refreshing its xattr dir */
  fsal_set_times_current(&post_attr);

  pres->res_create3.status = NFS3_OK;

  return NFS_REQ_OK;
}

extern writeverf3 NFS3_write_verifier;  /* NFS V3 write verifier      */

int nfs3_Write_Xattr(nfs_arg_t * parg,
                     exportlist_t * pexport,
                     fsal_op_context_t * pcontext,
                     cache_inode_client_t * pclient,
                     hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *pentry;
  fsal_attrib_list_t attr;
  fsal_attrib_list_t attr_attrs;
  int rc;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_off_t offset = 0;
  fsal_status_t fsal_status;
  file_handle_v3_t *pfile_handle = NULL;
  fsal_handle_t *pfsal_handle = NULL;
  unsigned int xattr_id = 0;

  pres->res_write3.WRITE3res_u.resfail.file_wcc.before.attributes_follow = FALSE;
  pres->res_write3.WRITE3res_u.resfail.file_wcc.after.attributes_follow = FALSE;
  /* Convert file handle into a cache entry */
  if((pentry = nfs_FhandleToCache(NFS_V3,
                                  NULL,
                                  &(parg->arg_write3.file),
                                  NULL,
                                  NULL,
                                  &(pres->res_write3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_write3.file.data.data_val);

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry, &cache_status);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  if(pfile_handle->xattr_pos == 0)
    {
      pres->res_write3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }

  if(pfile_handle->xattr_pos == 1)
    {
      pres->res_write3.status = NFS3ERR_ISDIR;
      return NFS_REQ_OK;
    }

  /* xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  offset = parg->arg_write3.offset;

  if(offset > 0)
    {
      pres->res_write3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }

  fsal_status = FSAL_SetXAttrValueById(pfsal_handle,
                                       xattr_id,
                                       pcontext,
                                       parg->arg_write3.data.data_val,
                                       parg->arg_write3.data.data_len);

  /* @TODO deal with error cases */

  attr_attrs.asked_attributes = pclient->attrmask;
  fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, xattr_id, &attr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_write3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  /* Set the written size */
  pres->res_write3.WRITE3res_u.resok.count = parg->arg_write3.data.data_len;
  pres->res_write3.WRITE3res_u.resok.committed = FILE_SYNC;

  /* Set the write verifier */
  memcpy(pres->res_write3.WRITE3res_u.resok.verf, NFS3_write_verifier,
         sizeof(writeverf3));

  pres->res_write3.status = NFS3_OK;

  return NFS_REQ_OK;
}                               /* nfs3_Write_Xattr */

/**
 * nfs3_Read_Xattr: Implements NFSPROC3_READ for xattr ghost directory
 *
 * Implements NFSPROC3_READ.
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Read_Xattr(nfs_arg_t * parg,
                    exportlist_t * pexport,
                    fsal_op_context_t * pcontext,
                    cache_inode_client_t * pclient,
                    hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *pentry;
  fsal_attrib_list_t attr, xattr_attrs;
  int rc;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  fsal_size_t size = 0;
  size_t size_returned = 0;
  fsal_off_t offset = 0;
  fsal_status_t fsal_status;
  caddr_t data = NULL;
  unsigned int xattr_id = 0;
  file_handle_v3_t *pfile_handle = NULL;
  fsal_handle_t *pfsal_handle = NULL;

  /* Convert file handle into a cache entry */
  if((pentry = nfs_FhandleToCache(NFS_V3,
                                  NULL,
                                  &(parg->arg_read3.file),
                                  NULL,
                                  NULL,
                                  &(pres->res_read3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* to avoid setting it on each error case */
  pres->res_read3.READ3res_u.resfail.file_attributes.attributes_follow = FALSE;

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_read3.file.data.data_val);

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry, &cache_status);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  if(pfile_handle->xattr_pos == 0)
    {
      pres->res_read3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }

  if(pfile_handle->xattr_pos == 1)
    {
      pres->res_read3.status = NFS3ERR_ISDIR;
      return NFS_REQ_OK;
    }

  /* xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  offset = parg->arg_read3.offset;
  size = parg->arg_read3.count;

  /* Get the xattr related to this xattr_id */
  if((data = (char *)Mem_Alloc(XATTR_BUFFERSIZE)) == NULL)
    {
      return NFS_REQ_DROP;
    }
  memset(data, 0, XATTR_BUFFERSIZE);

  size_returned = size;
  fsal_status = FSAL_GetXAttrValueById(pfsal_handle,
                                       xattr_id,
                                       pcontext, data, XATTR_BUFFERSIZE, &size_returned);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_read3.status = NFS3ERR_IO;
      return NFS_REQ_OK;
    }

  /* XAttr is ALWAYS smaller than 4096 */
  pres->res_read3.READ3res_u.resok.eof = TRUE;

  /* Retrieve xattr attributes */
  xattr_attrs.asked_attributes = pclient->attrmask;
  fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, xattr_id, &xattr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_read3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      return NFS_REQ_OK;
    }

  /* Build Post Op Attributes */
  nfs_SetPostOpXAttrFile(pcontext,
                         pexport,
                         &xattr_attrs,
                         &(pres->res_read3.READ3res_u.resok.file_attributes));

  pres->res_read3.READ3res_u.resok.file_attributes.attributes_follow = TRUE;

  pres->res_read3.READ3res_u.resok.count = size_returned;
  pres->res_read3.READ3res_u.resok.data.data_val = data;
  pres->res_read3.READ3res_u.resok.data.data_len = size_returned;

  pres->res_read3.status = NFS3_OK;

  return NFS_REQ_OK;
}                               /* nfs3_Read_Xattr */

/**
 *
 * nfs3_Readdirplus_Xattr: The NFS PROC3 READDIRPLUS for xattr ghost objects
 *
 * Implements NFSPROC3_READDIRPLUS for ghost xattr objects
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return NFS_REQ_OK if successfull \n
 *         NFS_REQ_DROP if failed but retryable  \n
 *         NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs3_Readdirplus_Xattr(nfs_arg_t * parg,
                           exportlist_t * pexport,
                           fsal_op_context_t * pcontext,
                           cache_inode_client_t * pclient,
                           hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  typedef char entry_name_array_item_t[FSAL_MAX_NAME_LEN];
  typedef char fh3_buffer_item_t[NFS3_FHSIZE];

  unsigned int delta = 0;
  cache_entry_t *dir_pentry = NULL;
  unsigned long dircount;
  unsigned long maxcount = 0;
  fsal_attrib_list_t dir_attr;
  unsigned int begin_cookie;
  unsigned int xattr_cookie;
  cookieverf3 cookie_verifier;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  int rc;
  unsigned int i = 0;
  unsigned int num_entries = 0;
  unsigned long space_used;
  unsigned long estimated_num_entries;
  unsigned long asked_num_entries;
  unsigned int eod_met;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  entry_name_array_item_t *entry_name_array = NULL;
  fh3_buffer_item_t *fh3_array = NULL;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdirplus3.dircount;
  maxcount = parg->arg_readdirplus3.maxcount;
  begin_cookie = (unsigned int)parg->arg_readdirplus3.cookie;
  space_used = sizeof(READDIRPLUS3resok);
  estimated_num_entries = dircount / sizeof(entryplus3);

  /* BUGAZOMEU : rajouter acces direct au DIR_CONTINUE */
  if((dir_pentry = nfs_FhandleToCache(preq->rq_vers,
                                      NULL,
                                      &(parg->arg_readdirplus3.dir),
                                      NULL,
                                      NULL,
                                      &(pres->res_readdirplus3.status),
                                      NULL,
                                      &dir_attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* return NFS_REQ_DROP ; */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(dir_pentry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_readdirplus3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_readdirplus3.dir.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos;

  if(xattr_id != 1)             /* If this is not the xattrd */
    {
      pres->res_readdirplus3.status = NFS3ERR_NOTDIR;
      return NFS_REQ_OK;
    }

  /* Cookie verifier management */
  memset(cookie_verifier, 0, sizeof(cookieverf3));

  /*
   * If cookie verifier is used, then an non-trivial value is
   * returned to the client         This value is the mtime of
   * the directory. If verifier is unused (as in many NFS
   * Servers) then only a set of zeros is returned (trivial
   * value)
   */

  if(pexport->UseCookieVerifier)
    memcpy(cookie_verifier, &(dir_attr.mtime), sizeof(dir_attr.mtime));

  /*
   * nothing to do if != 0 because the area is already full of
   * zero
   */

  if(pexport->UseCookieVerifier && (begin_cookie != 0))
    {
      /*
       * Not the first call, so we have to check the cookie
       * verifier
       */
      if(memcmp(cookie_verifier, parg->arg_readdirplus3.cookieverf, NFS3_COOKIEVERFSIZE)
         != 0)
        {
          pres->res_readdirplus3.status = NFS3ERR_BAD_COOKIE;

          return NFS_REQ_OK;
        }
    }

  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
  pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = FALSE;

  /* How many entries will we retry from cache_inode ? */
  if(begin_cookie > 1)
    {
      asked_num_entries = estimated_num_entries;
      xattr_cookie = begin_cookie - 2;
    }
  else
    {
      asked_num_entries = ((estimated_num_entries > 2) ? estimated_num_entries - 2 : 0);        /* Keep space for '.' and '..' */
      xattr_cookie = 0;
    }

  /* A definition that will be very useful to avoid very long names for variables */
#define RES_READDIRPLUS_REPLY pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply

  /* Used FSAL extended attributes functions */
  fsal_status = FSAL_ListXAttrs(pfsal_handle,
                                xattr_cookie,
                                pcontext,
                                xattrs_tab, asked_num_entries, &nb_xattrs_read, &eod_met);

  if(!FSAL_IS_ERROR(fsal_status))
    {
      if((nb_xattrs_read == 0) && (begin_cookie > 1))
        {
          pres->res_readdirplus3.status = NFS3_OK;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpXAttrDir(pcontext,
                                pexport,
                                NULL,
                                &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                                  dir_attributes));

          memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
        }
      else
        {
          /* Allocation of the structure for reply */
          entry_name_array =
              (entry_name_array_item_t *) Mem_Alloc_Label(estimated_num_entries *
                                                         (FSAL_MAX_NAME_LEN + 1),
                                                         "entry_name_array in nfs3_Readdirplus");

          if(entry_name_array == NULL)
            {
              return NFS_REQ_DROP;
            }

          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
              (entryplus3 *) Mem_Alloc_Label(estimated_num_entries * sizeof(entryplus3),
                                             "READDIRPLUS3res_u.resok.reply.entries");

          if(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries == NULL)
            {
              Mem_Free((char *)entry_name_array);
              return NFS_REQ_DROP;
            }

          /* Allocation of the file handles */
          fh3_array =
              (fh3_buffer_item_t *) Mem_Alloc_Label(estimated_num_entries * NFS3_FHSIZE,
                                                    "Filehandle V3 in nfs3_Readdirplus");

          if(fh3_array == NULL)
            {
              Mem_Free((char *)entry_name_array);
              Mem_Free(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries);
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;

              return NFS_REQ_DROP;
            }

          delta = 0;

          /* manage . and .. */
          if(begin_cookie == 0)
            {
              /* Fill in '.' */
              if(estimated_num_entries > 0)
                {
                  RES_READDIRPLUS_REPLY.entries[0].fileid = (0xFFFFFFFF & ~(dir_attr.fileid)) - 1;        /* parent xattrd =>xattr_pos == 1 */
                  RES_READDIRPLUS_REPLY.entries[0].name = entry_name_array[0];
                  strcpy(RES_READDIRPLUS_REPLY.entries[0].name, ".");
                  RES_READDIRPLUS_REPLY.entries[0].cookie = 1;

                  RES_READDIRPLUS_REPLY.entries[0].name_handle.post_op_fh3_u.handle.data.
                      data_val = (char *)fh3_array[0];

                  memcpy((char *)RES_READDIRPLUS_REPLY.entries[0].name_handle.
                         post_op_fh3_u.handle.data.data_val,
                         (char *)parg->arg_readdirplus3.dir.data.data_val,
                         parg->arg_readdirplus3.dir.data.data_len);

                  RES_READDIRPLUS_REPLY.entries[0].name_handle.post_op_fh3_u.handle.data.
                      data_len = sizeof(file_handle_v3_t);
                  pfile_handle =
                      (file_handle_v3_t *) (RES_READDIRPLUS_REPLY.entries[0].name_handle.
                                            post_op_fh3_u.handle.data.data_val);
                  pfile_handle->xattr_pos = 1;
                  RES_READDIRPLUS_REPLY.entries[0].name_handle.handle_follows = TRUE;

                  /* Set PostPoFh3 structure */
                  nfs_SetPostOpXAttrDir(pcontext,
                                        pexport,
                                        &dir_attr,
                                        &(RES_READDIRPLUS_REPLY.entries[0].
                                          name_attributes));
                  delta += 1;
                }

            }

          /* Fill in '..' */
          if(begin_cookie <= 1)
            {
              if(estimated_num_entries > delta)
                {
                  RES_READDIRPLUS_REPLY.entries[delta].fileid = (0xFFFFFFFF & ~(dir_attr.fileid)) - delta;        /* different fileids for each xattr */
                  RES_READDIRPLUS_REPLY.entries[delta].name = entry_name_array[delta];
                  strcpy(RES_READDIRPLUS_REPLY.entries[delta].name, "..");
                  RES_READDIRPLUS_REPLY.entries[delta].cookie = 2;

                  RES_READDIRPLUS_REPLY.entries[delta].name_handle.post_op_fh3_u.handle.
                      data.data_val = (char *)fh3_array[delta];

                  memcpy((char *)RES_READDIRPLUS_REPLY.entries[delta].name_handle.
                         post_op_fh3_u.handle.data.data_val,
                         (char *)parg->arg_readdirplus3.dir.data.data_val,
                         parg->arg_readdirplus3.dir.data.data_len);

                  RES_READDIRPLUS_REPLY.entries[delta].name_handle.post_op_fh3_u.handle.
                      data.data_len = sizeof(file_handle_v3_t);
                  pfile_handle =
                      (file_handle_v3_t *) (RES_READDIRPLUS_REPLY.entries[delta].
                                            name_handle.post_op_fh3_u.handle.data.
                                            data_val);
                  pfile_handle->xattr_pos = 0;
                  RES_READDIRPLUS_REPLY.entries[delta].name_handle.handle_follows = TRUE;

                  RES_READDIRPLUS_REPLY.entries[delta].name_attributes.attributes_follow =
                      FALSE;

                  RES_READDIRPLUS_REPLY.entries[0].nextentry =
                      &(RES_READDIRPLUS_REPLY.entries[delta]);

                  if(num_entries > delta + 1)   /* not 0 ??? */
                    RES_READDIRPLUS_REPLY.entries[delta].nextentry =
                        &(RES_READDIRPLUS_REPLY.entries[delta + 1]);
                  else
                    RES_READDIRPLUS_REPLY.entries[delta].nextentry = NULL;

                  delta += 1;
                }
            }
          /* if( begin_cookie == 0 ) */
          for(i = delta; i < nb_xattrs_read + delta; i++)
            {
              unsigned long needed;

              /* dircount is the size without the FH and attributes overhead, so entry3 is used intead of entryplus3 */
              needed =
                  sizeof(entry3) +
                  ((strlen(xattrs_tab[i - delta].xattr_name.name) + 3) & ~3);

              if((space_used += needed) > maxcount)
                {
                  if(i == delta)
                    {
                      /*
                       * Not enough room to make even a single reply
                       */
                      Mem_Free((char *)entry_name_array);
                      Mem_Free((char *)fh3_array);
                      Mem_Free(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.
                               entries);
                      pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;

                      pres->res_readdirplus3.status = NFS3ERR_TOOSMALL;

                      return NFS_REQ_OK;
                    }
                  break;        /* Make post traitement */
                }

              /* Try to get a FSAL_XAttr of that name */
              /* Build the FSAL name */
              fsal_status = FSAL_GetXAttrIdByName(pfsal_handle,
                                                  &xattrs_tab[i - delta].xattr_name,
                                                  pcontext, &xattr_id);
              if(FSAL_IS_ERROR(fsal_status))
                {
                  pres->res_readdirplus3.status =
                      nfs3_Errno(cache_inode_error_convert(fsal_status));
                  return NFS_REQ_OK;
                }

              RES_READDIRPLUS_REPLY.entries[i].fileid =
                (0xFFFFFFFF & xattrs_tab[i - delta].attributes.fileid) - xattr_id;
              FSAL_name2str(&xattrs_tab[i - delta].xattr_name, entry_name_array[i],
                            FSAL_MAX_NAME_LEN);
              RES_READDIRPLUS_REPLY.entries[i].name = entry_name_array[i];

              RES_READDIRPLUS_REPLY.entries[i].cookie =
                  xattrs_tab[i - delta].xattr_cookie + 2;

              RES_READDIRPLUS_REPLY.entries[i].name_attributes.attributes_follow = FALSE;
              RES_READDIRPLUS_REPLY.entries[i].name_handle.handle_follows = FALSE;

              RES_READDIRPLUS_REPLY.entries[i].name_handle.post_op_fh3_u.handle.data.
                  data_val = (char *)fh3_array[i];

              /* Set PostPoFh3 structure */

              memcpy((char *)RES_READDIRPLUS_REPLY.entries[i].name_handle.post_op_fh3_u.
                     handle.data.data_val,
                     (char *)parg->arg_readdirplus3.dir.data.data_val,
                     parg->arg_readdirplus3.dir.data.data_len);
              RES_READDIRPLUS_REPLY.entries[i].name_handle.post_op_fh3_u.handle.data.
                  data_len = sizeof(file_handle_v3_t);
              pfile_handle =
                  (file_handle_v3_t *) (RES_READDIRPLUS_REPLY.entries[i].name_handle.
                                        post_op_fh3_u.handle.data.data_val);
              pfile_handle->xattr_pos = xattr_id + 2;

              RES_READDIRPLUS_REPLY.entries[i].name_handle.handle_follows = TRUE;

              RES_READDIRPLUS_REPLY.entries[i].nextentry = NULL;
              if(i != 0)
                RES_READDIRPLUS_REPLY.entries[i - 1].nextentry =
                    &(RES_READDIRPLUS_REPLY.entries[i]);

            }                   /* for */
        }                       /* else */

      if(eod_met)
        RES_READDIRPLUS_REPLY.eof = TRUE;
      else
        RES_READDIRPLUS_REPLY.eof = FALSE;

      nfs_SetPostOpXAttrDir(pcontext,
                            pexport,
                            &dir_attr,
                            &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                              dir_attributes));
      memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf, cookie_verifier,
             sizeof(cookieverf3));

      pres->res_readdir3.status = NFS3_OK;

      return NFS_REQ_OK;
    }

  /* if( !FSAL_IS_ERROR( fsal_status ) ) */
  /* Is this point is reached, then there is an error */
  pres->res_readdir3.status = NFS3ERR_IO;

  /*  Set failed status */
  nfs_SetFailedStatus(pcontext,
                      pexport,
                      NFS_V3,
                      cache_status,
                      NULL,
                      &pres->res_readdirplus3.status,
                      dir_pentry,
                      &(pres->res_readdirplus3.READDIRPLUS3res_u.resfail.dir_attributes),
                      NULL, NULL, NULL, NULL, NULL, NULL);

  return NFS_REQ_OK;
}                               /* nfs3_Readdirplus_Xattr */

/**
 * nfs3_Getattr_Xattr: Implements NFSPROC3_GETATTR for xattr ghost objects
 *
 * Implements NFSPROC3_GETATTR
 *
 * @param parg    [IN]    pointer to nfs arguments union
 * @param pexport [IN]    pointer to nfs export list
 * @param pcontext   [IN]    credentials to be used for this request
 * @param pclient [INOUT] client resource to be used
 * @param ht      [INOUT] cache inode hash table
 * @param preq    [IN]    pointer to SVC request related to this call
 * @param pres    [OUT]   pointer to the structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Getattr_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       fsal_op_context_t * pcontext,
                       cache_inode_client_t * pclient,
                       hash_table_t * ht, struct svc_req *preq, nfs_res_t * pres)
{
  fsal_attrib_list_t attr;
  int rc;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  cache_entry_t *pentry = NULL;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;

  if((pentry = nfs_FhandleToCache(NFS_V3,
                                  NULL,
                                  &(parg->arg_getattr3.object),
                                  NULL,
                                  NULL,
                                  &(pres->res_getattr3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_getattr3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* Rebuild the FH */
  pfile_handle = (file_handle_v3_t *) (parg->arg_getattr3.object.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  if(pfile_handle->xattr_pos == 0)
    {
      /* should not have been called */
      pres->res_getattr3.status = NFS3ERR_INVAL;
      return NFS_REQ_OK;
    }
  else if(pfile_handle->xattr_pos == 1)
    nfs3_FSALattr_To_XattrDir(pexport, &attr,
                              &pres->res_getattr3.GETATTR3res_u.resok.obj_attributes);
  else
    {
      fsal_status_t fsal_status;
      fsal_attrib_list_t xattrs;

      xattrs.asked_attributes = pclient->attrmask;
      fsal_status = FSAL_GetXAttrAttrs(pfsal_handle, pcontext, xattr_id, &xattrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_getattr3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          return NFS_REQ_OK;
        }

      nfs3_FSALattr_To_Fattr(pexport, &xattrs,
                             &pres->res_getattr3.GETATTR3res_u.resok.obj_attributes);
    }

  pres->res_getattr3.status = NFS3_OK;

  return NFS_REQ_OK;
}                               /* nfs3_Getattr_Xattr */

int nfs3_Remove_Xattr(nfs_arg_t * parg /* IN  */ ,
                      exportlist_t * pexport /* IN  */ ,
                      fsal_op_context_t * pcontext /* IN  */ ,
                      cache_inode_client_t * pclient /* IN  */ ,
                      hash_table_t * ht /* INOUT */ ,
                      struct svc_req *preq /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  cache_entry_t *pentry = NULL;
  cache_inode_status_t cache_status;
  fsal_handle_t *pfsal_handle = NULL;
  fsal_status_t fsal_status;
  fsal_name_t name = FSAL_NAME_INITIALIZER;
  fsal_attrib_list_t attr;
  int rc;

  if((pentry = nfs_FhandleToCache(NFS_V3,
                                  NULL,
                                  &(parg->arg_remove3.object.dir),
                                  NULL,
                                  NULL,
                                  &(pres->res_remove3.status),
                                  NULL, &attr, pcontext, pclient, ht, &rc)) == NULL)
    {
      /* Stale NFS FH ? */
      return rc;
    }

  /* Get the FSAL Handle */
  pfsal_handle = cache_inode_get_fsal_handle(pentry, &cache_status);
  if(cache_status != CACHE_INODE_SUCCESS)
    {
      pres->res_remove3.status = nfs3_Errno(cache_status);
      return NFS_REQ_OK;
    }

  /* convert attr name to FSAL name */
  FSAL_str2name(parg->arg_remove3.object.name, MAXNAMLEN, &name);

  fsal_status = FSAL_RemoveXAttrByName(pfsal_handle, pcontext, &name);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_remove3.status = NFS3ERR_SERVERFAULT;
      return NFS_REQ_OK;
    }

  /* Set Post Op attrs */
  pres->res_remove3.REMOVE3res_u.resok.dir_wcc.before.attributes_follow = FALSE;
  pres->res_remove3.REMOVE3res_u.resok.dir_wcc.after.attributes_follow = FALSE;

  pres->res_remove3.status = NFS3_OK;
  return NFS_REQ_OK;
}
