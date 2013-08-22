/*
 * vim:expandtab:shiftwidth=4:tabstop=4:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs4_xattr.c
 * @brief   Routines used for managing the NFS2/3 xattrs
 *
 * Routines used for managing the NFS2/3 xattrs
 *
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashTable.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

static void nfs_set_times_current(fattr3 * attrs)
{
  time_t now = time(NULL);

  attrs->atime.tv_sec = now;
  attrs->atime.tv_nsec = 0;

  attrs->mtime.tv_sec = now;
  attrs->mtime.tv_nsec = 0;

  attrs->ctime.tv_sec = now;
  attrs->ctime.tv_nsec = 0;
}

static void fsal_set_times_current(struct attrlist *attrs)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);

  attrs->atime.tv_sec = tv.tv_sec;
  attrs->atime.tv_nsec = 1000 * tv.tv_usec;

  attrs->mtime.tv_sec = tv.tv_sec;
  attrs->mtime.tv_nsec = 1000 * tv.tv_usec;

  attrs->ctime.tv_sec = tv.tv_sec;
  attrs->ctime.tv_nsec = 1000 * tv.tv_usec;
}

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
 * @brief Converts FSAL Attributes to NFSv3 attributes.
 *
 * Converts FSAL Attributes to NFSv3 attributes.
 *
 * @param[in]  entry Cache entry.
 * @param[out] Fattr NFSv3 attributes.
 *
 * @return 1 if successful, 0 otherwise.
 *
 */
int nfs3_FSALattr_To_XattrDir(cache_entry_t *entry,
                              const struct req_op_context *ctx,
                              fattr3 * Fattr)
{
        if (entry == NULL || Fattr == NULL) {
                return 0;
        }

        if (cache_inode_lock_trust_attrs(entry, ctx, false) !=
            CACHE_INODE_SUCCESS) {
                return 0;
        }

        /** Xattr directory is indeed a directory */
        Fattr->type = NF3DIR;
        Fattr->mode = 0555;
        Fattr->nlink = 2;             /* like a directory */
        Fattr->uid = entry->obj_handle->attributes.owner;
        Fattr->gid = entry->obj_handle->attributes.group;
        Fattr->size = DEV_BSIZE;
        Fattr->used = DEV_BSIZE;
        Fattr->rdev.specdata1 = 0;
        Fattr->rdev.specdata2 = 0;
        /* in NFSv3, we only keeps fsid.major, casted into an nfs_uint64 */
        Fattr->fsid = (nfs3_uint64)
                entry->obj_handle->export->exp_entry->filesystem_id.major;
        /* xattr_pos = 1 => Parent Xattrd */
        Fattr->fileid = (0xFFFFFFFF &
                         ~(entry->obj_handle->attributes.fileid)) - 1;
        /* set current time, to force the client refreshing its xattr dir */
        nfs_set_times_current(Fattr);
        PTHREAD_RWLOCK_unlock(&entry->attr_lock);

        return 1;
} /* nfs3_FSALattr_To_XattrDir */

/**
 * @brief Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * Converts FSAL Attributes to NFSv3 PostOp Attributes structure.
 *
 * @param[in]  entry   The cache entry
 * @param[in]  req_ctx Request context
 * @param[out] result  NFSv3 PostOp structure attributes.
 *
 * @return 0 in all cases (making it a void function maybe a good idea)
 *
 */
int nfs_SetPostOpXAttrDir(cache_entry_t *entry,
                          const struct req_op_context *ctx,
                          post_op_attr *result)
{
  if(entry == NULL)
    {
      result->attributes_follow = FALSE;
      return 0;
    }

  if(nfs3_FSALattr_To_XattrDir
     (entry, ctx, &(result->post_op_attr_u.attributes)) == 0)
    result->attributes_follow = FALSE;
  else
    result->attributes_follow = TRUE;

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
int nfs_SetPostOpXAttrFile(exportlist_t *pexport,
                           const struct attrlist *pfsal_attr,
                           post_op_attr *presult)
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
 * @brief Implements NFSPROC3_ACCESS for xattr objects
 *
 * Implements NFSPROC3_ACCESS.
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */

int nfs3_Access_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres)
{
  struct fsal_obj_handle *obj_hdl = NULL;
  cache_entry_t *pentry = NULL;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  int rc = NFS_REQ_OK;

  /* to avoid setting it on each error case */
  pres->res_access3.ACCESS3res_u.resfail.obj_attributes.attributes_follow = FALSE;

  pentry = nfs3_FhandleToCache(&(parg->arg_access3.object),
			       req_ctx,
			       pexport,
			       &(pres->res_access3.status),
			       &rc);
  if(pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = pentry->obj_handle;
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
      rc = NFS_REQ_OK;
      goto out;
    }
  else if(pfile_handle->xattr_pos == 1)
    {

      pres->res_access3.ACCESS3res_u.resok.access =
          parg->arg_access3.access & ~(ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE);

      /* Build directory attributes */
      nfs_SetPostOpXAttrDir(pentry,
                            req_ctx,
                            &(pres->res_access3.ACCESS3res_u.resok.obj_attributes));

    }
  else                          /* named attribute */
    {
      fsal_status_t fsal_status;
      struct attrlist xattrs;
      fsal_accessflags_t access_mode;

      access_mode = 0;
      if(parg->arg_access3.access & ACCESS3_READ)
        access_mode |= FSAL_R_OK;

      if(parg->arg_access3.access & (ACCESS3_MODIFY | ACCESS3_EXTEND | ACCESS3_DELETE))
        access_mode |= FSAL_W_OK;

      if(parg->arg_access3.access & ACCESS3_LOOKUP)
        access_mode |= FSAL_X_OK;

      fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                                   req_ctx,
                                                   xattr_id, &xattrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_access3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          rc = NFS_REQ_OK;
          goto out;
        }

      fsal_status = obj_hdl->ops->test_access(obj_hdl, req_ctx, access_mode);

      if(FSAL_IS_ERROR(fsal_status))
        {
          if(fsal_status.major == ERR_FSAL_ACCESS)
            {
              pres->res_access3.ACCESS3res_u.resok.access = 0;

              /* we have to check read/write permissions */
              if(!FSAL_IS_ERROR(obj_hdl->ops->test_access(obj_hdl, req_ctx,
                                                          FSAL_R_OK)))
                pres->res_access3.ACCESS3res_u.resok.access |= ACCESS3_READ;
              if(!FSAL_IS_ERROR(obj_hdl->ops->test_access(obj_hdl, req_ctx,
                                                          FSAL_W_OK)))
                pres->res_access3.ACCESS3res_u.resok.access |=
                    ACCESS3_MODIFY | ACCESS3_EXTEND;
            }
          else
            {
              /* this is an error */
              nfs_SetPostOpXAttrFile(pexport,
                                     &xattrs,
                                     &(pres->res_access3.ACCESS3res_u.resfail.
                                       obj_attributes));

              pres->res_access3.status =
                  nfs3_Errno(cache_inode_error_convert(fsal_status));
              rc = NFS_REQ_OK;
              goto out;
            }

        }
      else                      /* access granted */
        {
          pres->res_access3.ACCESS3res_u.resok.access = parg->arg_access3.access;
        }

      nfs_SetPostOpXAttrFile(pexport,
                             &xattrs,
                             &(pres->res_access3.ACCESS3res_u.resok.obj_attributes));
    }

  pres->res_access3.status = NFS3_OK;

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);
}

/**
 * @brief NFSPROC3_LOOKUP for xattr ghost directory
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */

int nfs3_Lookup_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres)
{
  struct attrlist xattr_attrs;
  fsal_status_t fsal_status;
  unsigned int xattr_id = 0;
  struct fsal_obj_handle *obj_hdl = NULL;
  char *name = parg->arg_lookup3.what.name;
  file_handle_v3_t *pfile_handle = NULL;
  cache_entry_t *pentry_dir = NULL;
  int rc = NFS_REQ_OK;

  pentry_dir = nfs3_FhandleToCache(&(parg->arg_lookup3.what.dir),
				   req_ctx,
				   pexport,
				   &(pres->res_lookup3.status),
				   &rc);
  if(pentry_dir == NULL)
    {
      /* Stale NFS FH ? */
        goto out;
    }

  obj_hdl = pentry_dir->obj_handle;

  /* Try to get a FSAL_XAttr of that name */
  fsal_status = obj_hdl->ops->getextattr_id_by_name(obj_hdl, 
                                                    req_ctx,
                                                    name,
                                                    &xattr_id);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_lookup3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Build FH */
  pres->res_lookup3.status =
	  nfs3_AllocateFH((nfs_fh3 *) & (pres->res_lookup3.LOOKUP3res_u.resok.object.data));
  if(pres->res_lookup3.status !=  NFS3_OK)
    {
      rc = NFS_REQ_OK;
      goto out;
    }

  if(nfs3_FSALToFhandle((nfs_fh3 *) & (pres->res_lookup3.LOOKUP3res_u.resok.object.data),
                        obj_hdl))
    {
      pres->res_lookup3.status =
          nfs3_fh_to_xattrfh((nfs_fh3 *) &
                             (pres->res_lookup3.LOOKUP3res_u.resok.object.data),
                             (nfs_fh3 *) & (pres->res_lookup3.LOOKUP3res_u.resok.object.
                                            data));

      /* Retrieve xattr attributes */
      fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                                   req_ctx,
                                                   xattr_id, 
                                                   &xattr_attrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_lookup3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          rc = NFS_REQ_OK;
          goto out;
        }

      nfs_SetPostOpXAttrFile(pexport,
                             &xattr_attrs,
                             &(pres->res_lookup3.LOOKUP3res_u.resok.obj_attributes));

      /* Build directory attributes */
      nfs_SetPostOpXAttrDir(pentry_dir,
                            req_ctx,
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

out:
  /* return references */
  if (pentry_dir)
      cache_inode_put(pentry_dir);

  return (rc);
}

/**
 * @brief NFSPROC3_READDIR for xattr ghost directory
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result
 *
 * @return always NFS_REQ_OK
 */

int nfs3_Readdir_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       struct req_op_context *req_ctx,
                       struct svc_req *preq, nfs_res_t * pres)
{
  typedef char entry_name_array_item_t[1024];
  typedef char fh3_buffer_item_t[NFS3_FHSIZE];

  unsigned int delta = 0;
  cache_entry_t *dir_pentry = NULL;
  unsigned long dircount;
  unsigned long maxcount = 0;
  unsigned int begin_cookie;
  unsigned int xattr_cookie;
  cookieverf3 cookie_verifier;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  unsigned int i = 0;
  unsigned int num_entries = 0;
  unsigned long space_used;
  unsigned long estimated_num_entries;
  unsigned long asked_num_entries;
  unsigned int eod_met;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  struct fsal_obj_handle *obj_hdl = NULL;
  fsal_status_t fsal_status;
  entry_name_array_item_t *entry_name_array = NULL;
  fh3_buffer_item_t *fh3_array = NULL;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];
  int rc = NFS_REQ_OK;

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdir3.count;
  begin_cookie = (unsigned int)parg->arg_readdir3.cookie;
  space_used = sizeof(READDIRPLUS3resok);
  estimated_num_entries = dircount / sizeof(entry3);

  dir_pentry = nfs3_FhandleToCache(&(parg->arg_readdir3.dir),
				   req_ctx,
				   pexport,
				   &(pres->res_readdir3.status),
				   &rc);
  if(dir_pentry == NULL)
    {
      /* return NFS_REQ_DROP ; */
      goto out;
    }

  obj_hdl = dir_pentry->obj_handle;

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
      rc = NFS_REQ_OK;
      goto out;
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
    memcpy(cookie_verifier, &(dir_pentry->change_time),
           sizeof(dir_pentry->change_time));

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
          rc = NFS_REQ_OK;
          goto out;
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
  dirlist3 *const RES_READDIR_REPLY
	  = &pres->res_readdir3.READDIR3res_u.resok.reply;

  /* Used FSAL extended attributes functions */
  fsal_status = obj_hdl->ops->list_ext_attrs(obj_hdl,
                         req_ctx,
					     xattr_cookie,
					     xattrs_tab,
					     asked_num_entries,
					     &nb_xattrs_read,
					     &eod_met);
  if(!FSAL_IS_ERROR(fsal_status))
    {
      if((nb_xattrs_read == 0) && (begin_cookie > 1))
        {
          pres->res_readdir3.status = NFS3_OK;
          pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;
          pres->res_readdir3.READDIR3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpXAttrDir(dir_pentry,
                                req_ctx,
                                &(pres->res_readdir3.READDIR3res_u.resok.dir_attributes));

          memcpy(pres->res_readdir3.READDIR3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
        }
      else
        {
          /* Allocation of the structure for reply */
          entry_name_array
            = gsh_calloc(estimated_num_entries,
                         (1024));

          if(entry_name_array == NULL)
            {
              rc = NFS_REQ_DROP;
              goto out;
            }

          pres->res_readdir3.READDIR3res_u.resok.reply.entries =
            gsh_calloc(estimated_num_entries, sizeof(entry3));

          if(pres->res_readdir3.READDIR3res_u.resok.reply.entries == NULL)
            {
              gsh_free(entry_name_array);
              rc = NFS_REQ_DROP;
              goto out;
            }

          /* Allocation of the file handles */

          fh3_array =
            gsh_calloc(estimated_num_entries, NFS3_FHSIZE);

          if(fh3_array == NULL)
            {
              gsh_free(entry_name_array);
              gsh_free(pres->res_readdir3.READDIR3res_u.resok.reply.entries);
              pres->res_readdir3.READDIR3res_u.resok.reply.entries = NULL;
              rc = NFS_REQ_DROP;
              goto out;
            }

          delta = 0;

          /* manage . and .. */
          if(begin_cookie == 0)
            {
              /* Fill in '.' */
              if(estimated_num_entries > 0)
                {
                  uint64_t fileid = 0;
                  cache_inode_fileid(dir_pentry, req_ctx, &fileid);
                  /* xattr_pos = 1 => Parent Xattrd */
                  RES_READDIR_REPLY->entries[0].fileid
                    = (0xFFFFFFFF & ~fileid) - 1;

                  RES_READDIR_REPLY->entries[0].name = entry_name_array[0];
                  strcpy(RES_READDIR_REPLY->entries[0].name, ".");
                  RES_READDIR_REPLY->entries[0].cookie = 1;

                  delta += 1;
                }

            }

          /* Fill in '..' */
          if(begin_cookie <= 1)
            {
              if(estimated_num_entries > delta)
                {
                  uint64_t fileid = 0;
                  cache_inode_fileid(dir_pentry, req_ctx, &fileid);
                  /* xattr_pos > 1 => attribute */
                  RES_READDIR_REPLY->entries[delta].fileid
                    = (0xFFFFFFFF & ~fileid) - delta;

                  RES_READDIR_REPLY->entries[delta].name
                    = entry_name_array[delta];
                  strcpy(RES_READDIR_REPLY->entries[delta].name, "..");
                  RES_READDIR_REPLY->entries[delta].cookie = 2;

                  RES_READDIR_REPLY->entries[0].nextentry =
                      &(RES_READDIR_REPLY->entries[delta]);

                  if(num_entries > delta + 1)   /* not 0 ??? */
                    RES_READDIR_REPLY->entries[delta].nextentry =
                        &(RES_READDIR_REPLY->entries[delta + 1]);
                  else
                    RES_READDIR_REPLY->entries[delta].nextentry = NULL;

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
                  ((strlen(xattrs_tab[i - delta].xattr_name) + 3) & ~3);

              if((space_used += needed) > maxcount)
                {
                  if(i == delta)
                    {
                      /*
                       * Not enough room to make even a single reply
                       */
                      gsh_free(entry_name_array);
                      gsh_free(fh3_array);
                      gsh_free(pres->res_readdir3.READDIR3res_u
                               .resok.reply.entries);
                      pres->res_readdir3.READDIR3res_u.resok.reply.entries
                        = NULL;

                      pres->res_readdir3.status = NFS3ERR_TOOSMALL;

                      rc = NFS_REQ_OK;
                      goto out;
                    }
                  break;        /* Make post traitement */
                }
              RES_READDIR_REPLY->entries[i].fileid =
                  0xFFFFFFFF & xattrs_tab[i - delta].attributes.fileid;
              RES_READDIR_REPLY->entries[i].name = entry_name_array[i];

              RES_READDIR_REPLY->entries[i].cookie =
                  xattrs_tab[i - delta].xattr_cookie + 2;

              RES_READDIR_REPLY->entries[i].nextentry = NULL;
              if(i != 0)
                RES_READDIR_REPLY->entries[i - 1].nextentry =
                    &(RES_READDIR_REPLY->entries[i]);

            }                   /* for */
        }                       /* else */

      if(eod_met)
        RES_READDIR_REPLY->eof = TRUE;
      else
        RES_READDIR_REPLY->eof = FALSE;

      nfs_SetPostOpXAttrDir(dir_pentry,
                            req_ctx,
                            &(pres->res_readdir3.READDIR3res_u.resok.dir_attributes));
      memcpy(pres->res_readdir3.READDIR3res_u.resok.cookieverf, cookie_verifier,
             sizeof(cookieverf3));

      pres->res_readdir3.status = NFS3_OK;

      rc = NFS_REQ_OK;
      goto out;
    }

  /* if( !FSAL_IS_ERROR( fsal_status ) ) */
  /* Is this point is reached, then there is an error */
  pres->res_readdir3.status = NFS3ERR_IO;

  /*  Set failed status */
  pres->res_readdir3.status = nfs3_Errno(cache_status);
  nfs_SetPostOpAttr(dir_pentry,
                    req_ctx,
                    &(pres->res_readdir3.READDIR3res_u.resfail
                      .dir_attributes));

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (dir_pentry)
      cache_inode_put(dir_pentry);

  return (rc);

}                               /* nfs3_Readdir_Xattr */

/**
 * nfs3_Write_Xattr: Implements NFSPROC3_WRITE for xattr ghost files
 *
 * Implements NFSPROC3_WRITE.
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Create_Xattr(nfs_arg_t * parg,
                      exportlist_t * pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *parent_pentry = NULL;
  struct attrlist attr_attrs;
  struct fsal_obj_handle *obj_hdl = NULL;
  char *attr_name;
  fsal_status_t fsal_status;
  file_handle_v3_t *p_handle_out;
  unsigned int attr_id;
  char empty_buff[16] = "";
  /* alias to clear code */
  CREATE3resok *resok = &pres->res_create3.CREATE3res_u.resok;
  int rc = NFS_REQ_OK;

  parent_pentry = nfs3_FhandleToCache(&(parg->arg_create3.where.dir),
				      req_ctx,
				      pexport,
				      &(pres->res_create3.status),
				      &rc);
  if(parent_pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = parent_pentry->obj_handle;

  attr_name = parg->arg_create3.where.name;

  /* set empty attr */
  fsal_status = obj_hdl->ops->setextattr_value(obj_hdl,
                                               req_ctx,
                                               attr_name,
                                               empty_buff,
                                               sizeof(empty_buff),
                                               true);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  /* get attr id */
  fsal_status = obj_hdl->ops->getextattr_id_by_name(obj_hdl,
                                                    req_ctx,
                                                    attr_name, &attr_id);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                               req_ctx,
                                               attr_id, &attr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_create3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Build file handle */
  pres->res_create3.status =
       nfs3_AllocateFH(&resok->obj.post_op_fh3_u.handle);
  if(pres->res_create3.status != NFS3_OK)
    {
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Set Post Op Fh3 structure */
  if(nfs3_FSALToFhandle(&resok->obj.post_op_fh3_u.handle, obj_hdl) == 0)
    {
      gsh_free(resok->obj.post_op_fh3_u.handle.data.data_val);
      pres->res_create3.status = NFS3ERR_BADHANDLE;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Turn the nfs FH into something readable */
  p_handle_out = (file_handle_v3_t *) (resok->obj.post_op_fh3_u.handle.data.data_val);

  /* xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   */
  p_handle_out->xattr_pos = attr_id + 2;

  resok->obj.handle_follows = TRUE;

  /* set current time (the file is new) */
  fsal_set_times_current(&attr_attrs);

  /* Set Post Op attrs */
  nfs_SetPostOpXAttrFile(pexport, &attr_attrs, &resok->obj_attributes);

  pres->res_create3.status = NFS3_OK;

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (parent_pentry)
      cache_inode_put(parent_pentry);

  return (rc);

}

int nfs3_Write_Xattr(nfs_arg_t * parg,
                     exportlist_t * pexport,
                     struct req_op_context *req_ctx,
                     struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *pentry;
  struct attrlist attr_attrs;
  uint64_t offset = 0;
  fsal_status_t fsal_status;
  file_handle_v3_t *pfile_handle = NULL;
  struct fsal_obj_handle *obj_hdl = NULL;
  unsigned int xattr_id = 0;
  int rc = NFS_REQ_OK;

  pres->res_write3.WRITE3res_u.resfail.file_wcc.before.attributes_follow = FALSE;
  pres->res_write3.WRITE3res_u.resfail.file_wcc.after.attributes_follow = FALSE;
  /* Convert file handle into a cache entry */
  pentry = nfs3_FhandleToCache(&parg->arg_write3.file,
			       req_ctx,
			       pexport,
			       &pres->res_write3.status,
			       &rc);
  if(pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = pentry->obj_handle;

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_write3.file.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  if(pfile_handle->xattr_pos == 0)
    {
      pres->res_write3.status = NFS3ERR_INVAL;
      rc = NFS_REQ_OK;
      goto out;
    }

  if(pfile_handle->xattr_pos == 1)
    {
      pres->res_write3.status = NFS3ERR_ISDIR;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  offset = parg->arg_write3.offset;

  if(offset > 0)
    {
      pres->res_write3.status = NFS3ERR_INVAL;
      rc = NFS_REQ_OK;
      goto out;
    }

  fsal_status = obj_hdl->ops->setextattr_value_by_id(obj_hdl,
                                                     req_ctx,
                                                     xattr_id,
                                                     parg->arg_write3.data.data_val,
                                                     parg->arg_write3.data.data_len);

  /* @TODO deal with error cases */

  fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                               req_ctx,
                                               xattr_id, &attr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_write3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Set the written size */
  pres->res_write3.WRITE3res_u.resok.count = parg->arg_write3.data.data_len;
  pres->res_write3.WRITE3res_u.resok.committed = FILE_SYNC;

  /* Set the write verifier */
  memcpy(pres->res_write3.WRITE3res_u.resok.verf, NFS3_write_verifier,
         sizeof(writeverf3));

  pres->res_write3.status = NFS3_OK;

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);
} /* nfs3_Write_Xattr */

/**
 * @brief NFSPROC3_READ for xattr ghost directory
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    The structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Read_Xattr(nfs_arg_t * parg,
                    exportlist_t * pexport,
                    struct req_op_context *req_ctx,
                    struct svc_req *preq, nfs_res_t * pres)
{
  cache_entry_t *pentry;
  struct attrlist xattr_attrs;
  uint32_t size = 0;
  size_t size_returned = 0;
  fsal_status_t fsal_status;
  caddr_t data = NULL;
  unsigned int xattr_id = 0;
  file_handle_v3_t *pfile_handle = NULL;
  struct fsal_obj_handle *obj_hdl = NULL;
  int rc = NFS_REQ_OK;

  /* Convert file handle into a cache entry */
  pentry = nfs3_FhandleToCache(&(parg->arg_read3.file),
			       req_ctx,
			       pexport,
			       &(pres->res_read3.status),
			       &rc);
  if(pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = pentry->obj_handle;

  /* to avoid setting it on each error case */
  pres->res_read3.READ3res_u.resfail.file_attributes.attributes_follow = FALSE;

  /* Turn the nfs FH into something readable */
  pfile_handle = (file_handle_v3_t *) (parg->arg_read3.file.data.data_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  if(pfile_handle->xattr_pos == 0)
    {
      pres->res_read3.status = NFS3ERR_INVAL;
      rc = NFS_REQ_OK;
      goto out;
    }

  if(pfile_handle->xattr_pos == 1)
    {
      pres->res_read3.status = NFS3ERR_ISDIR;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  size = parg->arg_read3.count;

  /* Get the xattr related to this xattr_id */
  if((data = gsh_calloc(1, XATTR_BUFFERSIZE)) == NULL)
    {
      rc = NFS_REQ_DROP;
      goto out;
    }
  size_returned = size;
  fsal_status = obj_hdl->ops->getextattr_value_by_id(obj_hdl,
                                                     req_ctx,
                                                     xattr_id,
                                                     data, XATTR_BUFFERSIZE,
                                                     &size_returned);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_read3.status = NFS3ERR_IO;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* XAttr is ALWAYS smaller than 4096 */
  pres->res_read3.READ3res_u.resok.eof = TRUE;

  /* Retrieve xattr attributes */
  fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                               req_ctx,
                                               xattr_id, &xattr_attrs);

  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_read3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Build Post Op Attributes */
  nfs_SetPostOpXAttrFile(pexport,
                         &xattr_attrs,
                         &(pres->res_read3.READ3res_u.resok.file_attributes));

  pres->res_read3.READ3res_u.resok.file_attributes.attributes_follow = TRUE;

  pres->res_read3.READ3res_u.resok.count = size_returned;
  pres->res_read3.READ3res_u.resok.data.data_val = data;
  pres->res_read3.READ3res_u.resok.data.data_len = size_returned;

  pres->res_read3.status = NFS3_OK;

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);

}

/**
 * @brief READDIRPLUS for xattr ghost objects
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @retval NFS_REQ_OK if successfull
 * @retval NFS_REQ_DROP if failed but retryable
 * @retval NFS_REQ_FAILED if failed and not retryable.
 *
 */

int nfs3_Readdirplus_Xattr(nfs_arg_t * parg,
                           exportlist_t * pexport,
                           struct req_op_context *req_ctx,
                           struct svc_req *preq, nfs_res_t * pres)
{
  typedef char entry_name_array_item_t[1024];
  typedef char fh3_buffer_item_t[NFS3_FHSIZE];

  unsigned int delta = 0;
  cache_entry_t *dir_pentry = NULL;
  unsigned long dircount;
  unsigned long maxcount = 0;
  unsigned int begin_cookie;
  unsigned int xattr_cookie;
  cookieverf3 cookie_verifier;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  unsigned int i = 0;
  unsigned int num_entries = 0;
  unsigned long space_used;
  unsigned long estimated_num_entries;
  unsigned long asked_num_entries;
  unsigned int eod_met;
  cache_inode_status_t cache_status = CACHE_INODE_SUCCESS;
  struct fsal_obj_handle *obj_hdl;
  fsal_status_t fsal_status;
  entry_name_array_item_t *entry_name_array = NULL;
  fh3_buffer_item_t *fh3_array = NULL;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];
  int rc = NFS_REQ_OK;

  /* to avoid setting it on each error case */
  pres->res_readdir3.READDIR3res_u.resfail.dir_attributes.attributes_follow = FALSE;

  dircount = parg->arg_readdirplus3.dircount;
  maxcount = parg->arg_readdirplus3.maxcount;
  begin_cookie = (unsigned int)parg->arg_readdirplus3.cookie;
  space_used = sizeof(READDIRPLUS3resok);
  estimated_num_entries = dircount / sizeof(entryplus3);

  /* BUGAZOMEU : rajouter acces direct au DIR_CONTINUE */
  dir_pentry = nfs3_FhandleToCache(&(parg->arg_readdirplus3.dir),
				   req_ctx,
				   pexport,
				   &(pres->res_readdirplus3.status),
				   &rc);
  if(dir_pentry == NULL)
    {
      /* return NFS_REQ_DROP ; */
      goto out;
    }

  obj_hdl = dir_pentry->obj_handle;

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
      rc = NFS_REQ_OK;
      goto out;
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
    memcpy(cookie_verifier, &dir_pentry->change_time,
           sizeof(dir_pentry->change_time));

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

          rc = NFS_REQ_OK;
          goto out;
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
  dirlistplus3 *const RES_READDIRPLUS_REPLY
	  = &pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply;

  /* Used FSAL extended attributes functions */
  fsal_status = obj_hdl->ops->list_ext_attrs(obj_hdl,
                         req_ctx,
					     xattr_cookie,
					     xattrs_tab, asked_num_entries,
					     &nb_xattrs_read, &eod_met);

  if(!FSAL_IS_ERROR(fsal_status))
    {
      if((nb_xattrs_read == 0) && (begin_cookie > 1))
        {
          pres->res_readdirplus3.status = NFS3_OK;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries = NULL;
          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.eof = TRUE;

          nfs_SetPostOpXAttrDir(NULL,
                                req_ctx,
                                &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                                  dir_attributes));

          memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf,
                 cookie_verifier, sizeof(cookieverf3));
        }
      else
        {
          /* Allocation of the structure for reply */
          entry_name_array =
            gsh_calloc(estimated_num_entries,
                       (1024));

          if(entry_name_array == NULL)
            {
              rc = NFS_REQ_DROP;
              goto out;
            }

          pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries =
            gsh_calloc(estimated_num_entries, sizeof(entryplus3));

          if(pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries
             == NULL)
            {
              gsh_free(entry_name_array);
              rc = NFS_REQ_DROP;
              goto out;
            }

          /* Allocation of the file handles */
          fh3_array = gsh_calloc(estimated_num_entries, NFS3_FHSIZE);

          if(fh3_array == NULL)
            {
              gsh_free(entry_name_array);
              gsh_free(pres->res_readdirplus3.READDIRPLUS3res_u
                       .resok.reply.entries);
              pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply.entries
                = NULL;

              rc = NFS_REQ_DROP;
              goto out;
            }

          delta = 0;

          /* manage . and .. */
          if(begin_cookie == 0)
            {
              /* Fill in '.' */
              if(estimated_num_entries > 0)
                {
                  uint64_t fileid = 0;
                  cache_inode_fileid(dir_pentry, req_ctx, &fileid);
                  /* parent xattrd =>xattr_pos == 1 */
                  RES_READDIRPLUS_REPLY->entries[0].fileid
                    = (0xFFFFFFFF & ~fileid) - 1;
                  RES_READDIRPLUS_REPLY->entries[0].name = entry_name_array[0];
                  strcpy(RES_READDIRPLUS_REPLY->entries[0].name, ".");
                  RES_READDIRPLUS_REPLY->entries[0].cookie = 1;

                  RES_READDIRPLUS_REPLY->entries[0].name_handle.post_op_fh3_u.handle.data.
                      data_val = (char *)fh3_array[0];

                  memcpy((char *)RES_READDIRPLUS_REPLY->entries[0].name_handle.
                         post_op_fh3_u.handle.data.data_val,
                         (char *)parg->arg_readdirplus3.dir.data.data_val,
                         parg->arg_readdirplus3.dir.data.data_len);

                  RES_READDIRPLUS_REPLY->entries[0].name_handle.post_op_fh3_u.handle.data.
                      data_len = parg->arg_readdirplus3.dir.data.data_len;
                  pfile_handle =
                      (file_handle_v3_t *) (RES_READDIRPLUS_REPLY->entries[0].name_handle.
                                            post_op_fh3_u.handle.data.data_val);
                  pfile_handle->xattr_pos = 1;
                  RES_READDIRPLUS_REPLY->entries[0].name_handle.handle_follows = TRUE;

                  /* Set PostPoFh3 structure */
                  nfs_SetPostOpXAttrDir(dir_pentry,
                                        req_ctx,
                                        &(RES_READDIRPLUS_REPLY->entries[0].
                                          name_attributes));
                  delta += 1;
                }

            }

          /* Fill in '..' */
          if(begin_cookie <= 1)
            {
              if(estimated_num_entries > delta)
                {
                  uint64_t fileid = 0;
                  cache_inode_fileid(dir_pentry, req_ctx, &fileid);
                  /* different fileids for each xattr */
                  RES_READDIRPLUS_REPLY->entries[delta].fileid
                    = (0xFFFFFFFF & ~fileid) - delta;
                  RES_READDIRPLUS_REPLY->entries[delta].name = entry_name_array[delta];
                  strcpy(RES_READDIRPLUS_REPLY->entries[delta].name, "..");
                  RES_READDIRPLUS_REPLY->entries[delta].cookie = 2;

                  RES_READDIRPLUS_REPLY->entries[delta].name_handle.post_op_fh3_u.handle.
                      data.data_val = (char *)fh3_array[delta];

                  memcpy(RES_READDIRPLUS_REPLY->entries[delta].name_handle.
                         post_op_fh3_u.handle.data.data_val,
                         parg->arg_readdirplus3.dir.data.data_val,
                         parg->arg_readdirplus3.dir.data.data_len);

                  RES_READDIRPLUS_REPLY->entries[delta].name_handle.post_op_fh3_u.handle.
                      data.data_len = parg->arg_readdirplus3.dir.data.data_len;
                  pfile_handle =
                      (file_handle_v3_t *) (RES_READDIRPLUS_REPLY->entries[delta].
                                            name_handle.post_op_fh3_u.handle.data.
                                            data_val);
                  pfile_handle->xattr_pos = 0;
                  RES_READDIRPLUS_REPLY->entries[delta].name_handle.handle_follows = TRUE;

                  RES_READDIRPLUS_REPLY->entries[delta].name_attributes.attributes_follow =
                      FALSE;

                  RES_READDIRPLUS_REPLY->entries[0].nextentry =
                      &(RES_READDIRPLUS_REPLY->entries[delta]);

                  if(num_entries > delta + 1)   /* not 0 ??? */
                    RES_READDIRPLUS_REPLY->entries[delta].nextentry =
                        &(RES_READDIRPLUS_REPLY->entries[delta + 1]);
                  else
                    RES_READDIRPLUS_REPLY->entries[delta].nextentry = NULL;

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
                  ((strlen(xattrs_tab[i - delta].xattr_name) + 3) & ~3);

              if((space_used += needed) > maxcount)
                {
                  if(i == delta)
                    {
                      /*
                       * Not enough room to make even a single reply
                       */
                      gsh_free(entry_name_array);
                      gsh_free(fh3_array);
                      gsh_free(pres->res_readdirplus3.READDIRPLUS3res_u
                               .resok.reply.entries);
                      pres->res_readdirplus3.READDIRPLUS3res_u.resok.reply
                           .entries = NULL;

                      pres->res_readdirplus3.status = NFS3ERR_TOOSMALL;

                      rc = NFS_REQ_OK;
                      goto out;
                    }
                  break;        /* Make post traitement */
                }

              /* Try to get a FSAL_XAttr of that name */
              /* Build the FSAL name */
              fsal_status = obj_hdl->ops->getextattr_id_by_name(
                      obj_hdl,
                      req_ctx,
                      xattrs_tab[i - delta].xattr_name,
                      &xattr_id);
              if(FSAL_IS_ERROR(fsal_status))
                {
                  pres->res_readdirplus3.status =
                      nfs3_Errno(cache_inode_error_convert(fsal_status));
                  rc = NFS_REQ_OK;
                  goto out;
                }

              RES_READDIRPLUS_REPLY->entries[i].fileid =
                (0xFFFFFFFF & xattrs_tab[i - delta].attributes.fileid) - xattr_id;
              RES_READDIRPLUS_REPLY->entries[i].name = entry_name_array[i];

              RES_READDIRPLUS_REPLY->entries[i].cookie =
                  xattrs_tab[i - delta].xattr_cookie + 2;

              RES_READDIRPLUS_REPLY->entries[i].name_attributes.attributes_follow = FALSE;
              RES_READDIRPLUS_REPLY->entries[i].name_handle.handle_follows = FALSE;

              RES_READDIRPLUS_REPLY->entries[i].name_handle.post_op_fh3_u.handle.data.
                  data_val = (char *)fh3_array[i];

              /* Set PostPoFh3 structure */

              memcpy((char *)RES_READDIRPLUS_REPLY->entries[i].name_handle.post_op_fh3_u.
                     handle.data.data_val,
                     (char *)parg->arg_readdirplus3.dir.data.data_val,
                     parg->arg_readdirplus3.dir.data.data_len);
              RES_READDIRPLUS_REPLY->entries[i].name_handle.post_op_fh3_u.handle.data.
                  data_len = parg->arg_readdirplus3.dir.data.data_len;
              pfile_handle =
                  (file_handle_v3_t *) (RES_READDIRPLUS_REPLY->entries[i].name_handle.
                                        post_op_fh3_u.handle.data.data_val);
              pfile_handle->xattr_pos = xattr_id + 2;

              RES_READDIRPLUS_REPLY->entries[i].name_handle.handle_follows = TRUE;

              RES_READDIRPLUS_REPLY->entries[i].nextentry = NULL;
              if(i != 0)
                RES_READDIRPLUS_REPLY->entries[i - 1].nextentry =
                    &(RES_READDIRPLUS_REPLY->entries[i]);

            }                   /* for */
        }                       /* else */

      if(eod_met)
        RES_READDIRPLUS_REPLY->eof = TRUE;
      else
        RES_READDIRPLUS_REPLY->eof = FALSE;

      nfs_SetPostOpXAttrDir(dir_pentry,
                            req_ctx,
                            &(pres->res_readdirplus3.READDIRPLUS3res_u.resok.
                              dir_attributes));
      memcpy(pres->res_readdirplus3.READDIRPLUS3res_u.resok.cookieverf, cookie_verifier,
             sizeof(cookieverf3));

      pres->res_readdir3.status = NFS3_OK;

      rc = NFS_REQ_OK;
      goto out;
    }

  /* if( !FSAL_IS_ERROR( fsal_status ) ) */
  /* Is this point is reached, then there is an error */
  pres->res_readdir3.status = NFS3ERR_IO;

  pres->res_readdirplus3.status = nfs3_Errno(cache_status);
  nfs_SetPostOpAttr(dir_pentry,
                    req_ctx,
                    &(pres->res_readdirplus3.READDIRPLUS3res_u.resfail
                      .dir_attributes));

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (dir_pentry)
      cache_inode_put(dir_pentry);

  return (rc);

}

/**
 * @brief Implements NFSPROC3_GETATTR for xattr ghost objects
 *
 * @param[in]  parg    NFS arguments union
 * @param[in]  pexport NFS export list
 * @param[in]  req_ctx Credentials to be used for this request
 * @param[in]  preq    SVC request related to this call
 * @param[out] pres    Structure to contain the result of the call
 *
 * @return always NFS_REQ_OK
 *
 */
int nfs3_Getattr_Xattr(nfs_arg_t * parg,
                       exportlist_t * pexport,
                       struct req_op_context *req_ctx,
                       struct svc_req *preq, nfs_res_t * pres)
{
  struct fsal_obj_handle *obj_hdl;
  cache_entry_t *pentry = NULL;
  file_handle_v3_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  int rc = NFS_REQ_OK;

  pentry = nfs3_FhandleToCache(&(parg->arg_getattr3.object),
			       req_ctx,
			       pexport,
			       &(pres->res_getattr3.status),
			       &rc);
  if(pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = pentry->obj_handle;

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
      rc = NFS_REQ_OK;
      goto out;
    }
  else if(pfile_handle->xattr_pos == 1)
    nfs3_FSALattr_To_XattrDir(pentry, req_ctx,
                              &pres->res_getattr3.GETATTR3res_u.resok.obj_attributes);
  else
    {
      fsal_status_t fsal_status;
      struct attrlist xattrs;

      fsal_status = obj_hdl->ops->getextattr_attrs(obj_hdl,
                                                   req_ctx,
                                                   xattr_id, &xattrs);

      if(FSAL_IS_ERROR(fsal_status))
        {
          pres->res_getattr3.status = nfs3_Errno(cache_inode_error_convert(fsal_status));
          rc = NFS_REQ_OK;
          goto out;
        }

      nfs3_FSALattr_To_Fattr(pexport, &xattrs,
                             &pres->res_getattr3.GETATTR3res_u.resok.obj_attributes);
    }

  pres->res_getattr3.status = NFS3_OK;

  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);

} /* nfs3_Getattr_Xattr */

int nfs3_Remove_Xattr(nfs_arg_t *parg,
                      exportlist_t *pexport,
                      struct req_op_context *req_ctx,
                      struct svc_req *preq,
                      nfs_res_t *pres)
{
  cache_entry_t *pentry = NULL;
  struct fsal_obj_handle *obj_hdl;
  fsal_status_t fsal_status;
  char *name;
  int rc = NFS_REQ_OK;

  pentry = nfs3_FhandleToCache(&(parg->arg_remove3.object.dir),
			       req_ctx,
			       pexport,
			       &(pres->res_remove3.status),
			       &rc);
  if(pentry == NULL)
    {
      /* Stale NFS FH ? */
      goto out;
    }

  obj_hdl = pentry->obj_handle;

  name = parg->arg_remove3.object.name;

  fsal_status = obj_hdl->ops->remove_extattr_by_name(obj_hdl,req_ctx, name);
  if(FSAL_IS_ERROR(fsal_status))
    {
      pres->res_remove3.status = NFS3ERR_SERVERFAULT;
      rc = NFS_REQ_OK;
      goto out;
    }

  /* Set Post Op attrs */
  pres->res_remove3.REMOVE3res_u.resok.dir_wcc.before.attributes_follow = FALSE;
  pres->res_remove3.REMOVE3res_u.resok.dir_wcc.after.attributes_follow = FALSE;

  pres->res_remove3.status = NFS3_OK;
  rc = NFS_REQ_OK;

out:
  /* return references */
  if (pentry)
      cache_inode_put(pentry);

  return (rc);

}
