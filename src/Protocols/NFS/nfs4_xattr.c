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
 * \file    nfs4_xattr.c
 * \author  $Author$
 * \date    $Date$
 * \version $Revision$
 * \brief   Routines used for managing the NFS4 xattrs
t*
 * nfs4_xattr.c: Routines used for managing the NFS4 xattrs
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
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

int nfs4_XattrToFattr(fattr4 * Fattr,
                      compound_data_t * data, nfs_fh4 * objFH,struct bitmap4 * Bitmap)
{
	struct attrlist attrs;
	file_handle_v4_t *pfile_handle = (file_handle_v4_t *) (objFH->nfs_fh4_val);

	/* cobble up an inode (attributes) for this pseudo */
	memset(&attrs, 0, sizeof(attrs));
	if(pfile_handle->xattr_pos == 1) {
		attrs.type = DIRECTORY;
		attrs.mode = unix2fsal_mode(0555);
	} else {
		attrs.type = REGULAR_FILE;
		attrs.mode = unix2fsal_mode(0644);
	}
	attrs.filesize = DEV_BSIZE;
	attrs.fsid.major = data->pexport->filesystem_id.major;
	attrs.fsid.minor = data->pexport->filesystem_id.minor;
#ifndef _XATTR_D_USE_SAME_INUM  /* I wrapped off this part of the code... Not sure it would be useful */
	attrs.fileid = ~(data->current_entry->obj_handle->attributes.fileid);
	attrs.fileid = ~(data->current_entry->obj_handle->attributes.fileid) - pfile_handle->xattr_pos;
#else
	attrs.fileid = data->current_entry->obj_handle->attributes.fileid;
#endif
	attrs.numlinks = 2; /* not 1.  for '.' and '../me' */
	attrs.owner = NFS4_ROOT_UID; /* is this right? shouldn't it mirror the file's owner? */
	attrs.group = 2; /* daemon? same here */
	attrs.atime.tv_sec = time(NULL);
	attrs.ctime.tv_sec = attrs.atime.tv_sec;
	attrs.chgtime.tv_sec = attrs.atime.tv_sec;
	attrs.change = attrs.atime.tv_sec;
	attrs.spaceused = DEV_BSIZE;
	attrs.mounted_on_fileid = attrs.fileid;
	return nfs4_FSALattr_To_Fattr(&attrs, Fattr, data, objFH, Bitmap);
}                               /* nfs4_XattrToFattr */

/** 
 * nfs4_fh_to_xattrfh: builds the fh to the xattrs ghost directory 
 *
 * @param pfhin  [IN]  input file handle (the object whose xattr fh is queryied)
 * @param pfhout [OUT] output file handle
 *
 * @return NFS4_OK 
 *
 */
nfsstat4 nfs4_fh_to_xattrfh(nfs_fh4 * pfhin, nfs_fh4 * pfhout)
{
  file_handle_v4_t *pfile_handle = NULL;

  memcpy(pfhout->nfs_fh4_val, pfhin->nfs_fh4_val, pfhin->nfs_fh4_len);

  pfile_handle = (file_handle_v4_t *) (pfhout->nfs_fh4_val);

  /* the following choice is made for xattr: the field xattr_pos contains :
   * - 0 if the FH is related to an actual FH object
   * - 1 if the FH is the one for the xattr ghost directory
   * - a value greater than 1 if the fh is related to a ghost file in ghost xattr directory that represents a xattr. The value is then equal 
   *   to the xattr_id + 1 (see how FSAL manages xattrs for meaning of this field). This limits the number of xattrs per object to 254. 
   */
  pfile_handle->xattr_pos = 1;  /**< 1 = xattr ghost directory */

  return NFS4_OK;
}                               /* nfs4_fh_to_xattrfh */

/** 
 * nfs4_xattrfh_to_fh: builds the fh from the xattrs ghost directory 
 *
 * @param pfhin  [IN]  input file handle 
 * @param pfhout [OUT] output file handle
 *
 * @return NFS4_OK 
 *
 */
nfsstat4 nfs4_xattrfh_to_fh(nfs_fh4 * pfhin, nfs_fh4 * pfhout)
{
  file_handle_v4_t *pfile_handle = NULL;

  memcpy(pfhout->nfs_fh4_val, pfhin->nfs_fh4_val, pfhin->nfs_fh4_len);

  pfile_handle = (file_handle_v4_t *) (pfhout->nfs_fh4_val);

  pfile_handle->xattr_pos = 0;  /**< 0 = real filehandle */

  return NFS4_OK;
}                               /* nfs4_fh_to_xattrfh */


/**
 * nfs4_op_getattr_xattr: Gets attributes for xattrs objects
 * 
 * Gets attributes for xattrs objects
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

#define arg_GETATTR4 op->nfs_argop4_u.opgetattr
#define res_GETATTR4 resp->nfs_resop4_u.opgetattr

int nfs4_op_getattr_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_GETATTR;

  res_GETATTR4.status = NFS4_OK;

  if(nfs4_XattrToFattr(&(res_GETATTR4.GETATTR4res_u.resok4.obj_attributes),
                       data, &(data->currentFH), &(arg_GETATTR4.attr_request)) != 0)
    res_GETATTR4.status = NFS4ERR_SERVERFAULT;
  else
    res_GETATTR4.status = NFS4_OK;

  return res_GETATTR4.status;
}                               /* nfs4_op_getattr_xattr */

/**
 * nfs4_op_access_xattrs: Checks for xattrs accessibility 
 * 
 * Checks for object accessibility in xattrs fs. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK 
 * 
 */

/* Shorter notation to avoid typos */
#define res_ACCESS4 resp->nfs_resop4_u.opaccess
#define arg_ACCESS4 op->nfs_argop4_u.opaccess

int nfs4_op_access_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_ACCESS;

  /* All access types are supported */
  /** @todo think about making this RW, it is RO for now */
  res_ACCESS4.ACCESS4res_u.resok4.supported = ACCESS4_READ | ACCESS4_LOOKUP;

  /* DELETE/MODIFY/EXTEND are not supported in the pseudo fs */
  res_ACCESS4.ACCESS4res_u.resok4.access =
      arg_ACCESS4.access & ~(ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE);

  return NFS4_OK;
}                               /* nfs4_op_access_xattr */

/**
 * nfs4_op_lookup_xattr: looks up into the pseudo fs.
 * 
 * looks up into the pseudo fs. If a junction traversal is detected, does the necessary stuff for correcting traverse.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

/* Shorter notation to avoid typos */
#define arg_LOOKUP4 op->nfs_argop4_u.oplookup
#define res_LOOKUP4 resp->nfs_resop4_u.oplookup

int nfs4_op_lookup_xattr(struct nfs_argop4 *op,
                         compound_data_t * data, struct nfs_resop4 *resp)
{
  char name[MAXNAMLEN];
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = NULL;
  unsigned int xattr_id = 0;
  file_handle_v4_t *pfile_handle = NULL;

  /* The xattr directory contains no subdirectory, lookup always returns ENOENT */
  res_LOOKUP4.status = NFS4_OK;

  /* Get the FSAL Handle for the current object */
  obj_hdl = data->current_entry->obj_handle;

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(name, sizeof(name), &arg_LOOKUP4.objname);

  /* Try to get a FSAL_XAttr of that name */
  fsal_status = obj_hdl->ops->getextattr_id_by_name(obj_hdl, data->req_ctx, name, &xattr_id);
  if(FSAL_IS_ERROR(fsal_status))
    {
      return NFS4ERR_NOENT;
    }

  /* Attribute was found */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  pfile_handle->xattr_pos = xattr_id + 2;

  return NFS4_OK;
}                               /* nfs4_op_lookup_xattr */

/**
 * nfs4_op_lookupp_xattr: looks up into the pseudo fs for the parent directory
 * 
 * looks up into the pseudo fs for the parent directory of the current file handle. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

/* Shorter notation to avoid typos */
#define arg_LOOKUPP4 op->nfs_argop4_u.oplookupp
#define res_LOOKUPP4 resp->nfs_resop4_u.oplookupp

int nfs4_op_lookupp_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  resp->resop = NFS4_OP_LOOKUPP;

  res_LOOKUPP4.status = nfs4_xattrfh_to_fh(&(data->currentFH), &(data->currentFH));

  res_LOOKUPP4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_lookupp_xattr */

/**
 * nfs4_op_readdir_xattr: Reads a directory in the pseudo fs 
 * 
 * Reads a directory in the pseudo fs.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */

/* shorter notation to avoid typo */
#define arg_READDIR4 op->nfs_argop4_u.opreaddir
#define res_READDIR4 resp->nfs_resop4_u.opreaddir

static const struct bitmap4 RdAttrErrorBitmap = {
	.bitmap4_len = 1,
	.map[0] = (1<<FATTR4_RDATTR_ERROR),
	.map[1] = 0,
	.map[2] = 0
};
static const  attrlist4 RdAttrErrorVals = { 0, NULL };      /* Nothing to be seen here */

int nfs4_op_readdir_xattr(struct nfs_argop4 *op,
                          compound_data_t * data, struct nfs_resop4 *resp)
{
  unsigned long dircount = 0;
  unsigned long maxcount = 0;
  unsigned long estimated_num_entries = 0;
  unsigned long i = 0;
  bool eod_met = false;
  unsigned int nb_xattrs_read = 0;
  fsal_xattrent_t xattrs_tab[255];
  nfs_cookie4 cookie;
  verifier4 cookie_verifier;
  unsigned long space_used = 0;
  entry4 *entry_nfs_array = NULL;
  char **entry_name_array = NULL;
  struct fsal_obj_handle *obj_hdl = NULL;
  fsal_status_t fsal_status;
  file_handle_v4_t *file_handle;
  nfs_fh4 nfsfh;
  struct alloc_file_handle_v4 temp_handle;

  resp->resop = NFS4_OP_READDIR;
  res_READDIR4.status = NFS4_OK;

  nfsfh.nfs_fh4_val = (caddr_t) &temp_handle;
  nfsfh.nfs_fh4_len = sizeof(struct alloc_file_handle_v4);  
  memset(nfsfh.nfs_fh4_val, 0, nfsfh.nfs_fh4_len);

  memcpy(nfsfh.nfs_fh4_val, data->currentFH.nfs_fh4_val, data->currentFH.nfs_fh4_len);
  nfsfh.nfs_fh4_len = data->currentFH.nfs_fh4_len;

  file_handle = &temp_handle.handle;

  LogFullDebug(COMPONENT_NFS_V4_XATTR, "Entering NFS4_OP_READDIR_PSEUDO");

  /* get the caracteristic value for readdir operation */
  dircount = arg_READDIR4.dircount;
  maxcount = arg_READDIR4.maxcount;
  cookie = arg_READDIR4.cookie;
  space_used = sizeof(entry4);

  /* dircount is considered meaningless by many nfsv4 client (like the CITI one). we use maxcount instead */
  estimated_num_entries = maxcount / sizeof(entry4);

  LogFullDebug(COMPONENT_NFS_V4_XATTR,
               "PSEUDOFS READDIR: dircount=%lu, maxcount=%lu, cookie=%"PRIu64", sizeof(entry4)=%lu num_entries=%lu",
               dircount, maxcount, (uint64_t)cookie, space_used, estimated_num_entries);

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Cookie delivered by the server and used by the client SHOULD not ne 0, 1 or 2 (cf RFC3530, page192)
   * because theses value are reserved for special use.
   *      0 - cookie for first READDIR
   *      1 - reserved for . on client handside
   *      2 - reserved for .. on client handside
   * Entries '.' and '..' are not returned also
   * For these reason, there will be an offset of 3 between NFS4 cookie and HPSS cookie */

  /* Do not use a cookie of 1 or 2 (reserved values) */
  if(cookie == 1 || cookie == 2)
    {
      res_READDIR4.status = NFS4ERR_BAD_COOKIE;
      return res_READDIR4.status;
    }

  if(cookie != 0)
    cookie = cookie - 3;        /* 0,1 and 2 are reserved, there is a delta of '3' because of this */

  /* Get only attributes that are allowed to be read */
  if(!nfs4_Fattr_Check_Access_Bitmap(&arg_READDIR4.attr_request, FATTR4_ATTR_READ))
    {
      res_READDIR4.status = NFS4ERR_INVAL;
      return res_READDIR4.status;
    }

  /* If maxcount is too short, return NFS4ERR_TOOSMALL */
  if(maxcount < sizeof(entry4) || estimated_num_entries == 0)
    {
      res_READDIR4.status = NFS4ERR_TOOSMALL;
      return res_READDIR4.status;
    }

  /* Cookie verifier has the value of the Server Boot Time for pseudo fs */
  memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
#ifdef _WITH_COOKIE_VERIFIER
  /* BUGAZOMEU: management of the cookie verifier */
  if(NFS_SpecificConfig.UseCookieVerf == 1)
    {
      memcpy(cookie_verifier, &ServerBootTime.tv_sec, sizeof(cookie_verifier));
      if(cookie != 0)
        {
          if(memcmp(cookie_verifier, arg_READDIR4.cookieverf, NFS4_VERIFIER_SIZE) != 0)
            {
              res_READDIR4.status = NFS4ERR_BAD_COOKIE;
              gsh_free(entry_nfs_array);
              return res_READDIR4.status;
            }
        }
    }
#endif

  /* The default behaviour is to consider that eof is not reached, the returned values by cache_inode_readdir 
   * will let us know if eod was reached or not */
  res_READDIR4.READDIR4res_u.resok4.reply.eof = FALSE;

  /* Get the fsal_handle */
  obj_hdl = data->current_entry->obj_handle;

  /* Used FSAL extended attributes functions */
  fsal_status = obj_hdl->ops->list_ext_attrs(obj_hdl, data->req_ctx, cookie, xattrs_tab,
					     estimated_num_entries, &nb_xattrs_read, (int *)&eod_met);
  if(FSAL_IS_ERROR(fsal_status))
    {
      res_READDIR4.status = NFS4ERR_SERVERFAULT;
      return res_READDIR4.status;
    }

  if(eod_met)
    {
      /* This is the end of the directory */
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }

  if(nb_xattrs_read == 0)
    {
      /* only . and .. */
      res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
      res_READDIR4.READDIR4res_u.resok4.reply.eof = TRUE;
      memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
             NFS4_VERIFIER_SIZE);
    }
  else
    {
      /* Allocation of reply structures */
      if((entry_name_array =
          gsh_calloc(estimated_num_entries, (1024 + 1))) == NULL)
        {
          LogError(COMPONENT_NFS_V4_XATTR, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      if((entry_nfs_array =
          gsh_calloc(estimated_num_entries, sizeof(entry4))) == NULL)
        {
          LogError(COMPONENT_NFS_V4_XATTR, ERR_SYS, ERR_MALLOC, errno);
          res_READDIR4.status = NFS4ERR_SERVERFAULT;
          return res_READDIR4.status;
        }

      for(i = 0; i < nb_xattrs_read; i++)
        {
          entry_nfs_array[i].name.utf8string_val = entry_name_array[i];

          if(str2utf8(xattrs_tab[i].xattr_name, &entry_nfs_array[i].name) == -1)
            {
              res_READDIR4.status = NFS4ERR_SERVERFAULT;
              return res_READDIR4.status;
            }

          /* Set the cookie value */
          entry_nfs_array[i].cookie = cookie + i + 3;   /* 0, 1 and 2 are reserved */

          file_handle->xattr_pos = xattrs_tab[i].xattr_id + 2;
          if(nfs4_XattrToFattr(&(entry_nfs_array[i].attrs),
                               data, &nfsfh, &(arg_READDIR4.attr_request)) != 0)
            {
              /* Return the fattr4_rdattr_error , cf RFC3530, page 192 */
              entry_nfs_array[i].attrs.attrmask = RdAttrErrorBitmap;
              entry_nfs_array[i].attrs.attr_vals = RdAttrErrorVals;
            }

          /* Chain the entries together */
          entry_nfs_array[i].nextentry = NULL;
          if(i != 0)
            entry_nfs_array[i - 1].nextentry = &(entry_nfs_array[i]);

          /* This test is there to avoid going further than the buffer provided by the client 
           * the factor "9/10" is there for safety. Its value could be change as beta tests will be done */
          if((caddr_t)
             ((caddr_t) (&entry_nfs_array[i]) - (caddr_t) (&entry_nfs_array[0])) >
             (caddr_t) (maxcount * 9 / 10))
            break;

        }                       /* for */

    }                           /* else */

  /* Build the reply */
  memcpy(res_READDIR4.READDIR4res_u.resok4.cookieverf, cookie_verifier,
         NFS4_VERIFIER_SIZE);
  if(i == 0)
    res_READDIR4.READDIR4res_u.resok4.reply.entries = NULL;
  else
    res_READDIR4.READDIR4res_u.resok4.reply.entries = entry_nfs_array;

  res_READDIR4.status = NFS4_OK;

  return NFS4_OK;

}                               /* nfs4_op_readdir_xattr */

/**
 * nfs4_op_open_xattr: Opens the content of a xattr attribute
 * 
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

#define arg_OPEN4 op->nfs_argop4_u.opopen
#define res_OPEN4 resp->nfs_resop4_u.opopen
int nfs4_op_open_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp)
{
  char name[MAXNAMLEN];
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = NULL;
  unsigned int xattr_id = 0;
  file_handle_v4_t *pfile_handle = NULL;
  char empty_buff[16] = "";

  res_OPEN4.status = NFS4_OK;

  /* Get the FSAL Handle fo the current object */
  obj_hdl = data->current_entry->obj_handle;

  /* UTF8 strings may not end with \0, but they carry their length */
  utf82str(name, sizeof(name), &arg_OPEN4.claim.open_claim4_u.file);

  /* we do not use the stateful logic for accessing xattrs */
  switch (arg_OPEN4.openhow.opentype)
    {
    case OPEN4_CREATE:
      /* To be done later */
      /* set empty attr */
      fsal_status = obj_hdl->ops->setextattr_value(obj_hdl,
                                                   data->req_ctx,
                                                   name,
                                                   empty_buff,
                                                   sizeof(empty_buff),
                                                   true);

      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = nfs4_Errno(cache_inode_error_convert(fsal_status));
          return res_OPEN4.status;
        }

      /* Now, getr the id */
      fsal_status = obj_hdl->ops->getextattr_id_by_name(obj_hdl, data->req_ctx, name, &xattr_id);
      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = NFS4ERR_NOENT;
          return res_OPEN4.status;
        }

      /* Attribute was found */
      pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

      /* for Xattr FH, we adopt the current convention:
       * xattr_pos = 0 ==> the FH is the one of the actual FS object
       * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
       * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
      pfile_handle->xattr_pos = xattr_id + 2;

      res_OPEN4.status = NFS4_OK;
      return NFS4_OK;

      break;

    case OPEN4_NOCREATE:

      /* Try to get a FSAL_XAttr of that name */
      fsal_status = obj_hdl->ops->getextattr_id_by_name(obj_hdl, data->req_ctx, name, &xattr_id);
      if(FSAL_IS_ERROR(fsal_status))
        {
          res_OPEN4.status = NFS4ERR_NOENT;
          return res_OPEN4.status;
        }

      /* Attribute was found */
      pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

      /* for Xattr FH, we adopt the current convention:
       * xattr_pos = 0 ==> the FH is the one of the actual FS object
       * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
       * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
      pfile_handle->xattr_pos = xattr_id + 2;

      res_OPEN4.status = NFS4_OK;
      return NFS4_OK;

      break;

    }                           /* switch (arg_OPEN4.openhow.opentype) */

  res_OPEN4.status = NFS4_OK;
  return NFS4_OK;
}                               /* nfs4_op_open_xattr */

/**
 * nfs4_op_read_xattr: Reads the content of a xattr attribute
 * 
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
#define arg_READ4 op->nfs_argop4_u.opread
#define res_READ4 resp->nfs_resop4_u.opread

int nfs4_op_read_xattr(struct nfs_argop4 *op,
                       compound_data_t * data, struct nfs_resop4 *resp)
{
  struct fsal_obj_handle *obj_hdl = NULL;
  file_handle_v4_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  fsal_status_t fsal_status;
  char *buffer = NULL;
  size_t size_returned;

  /* Get the FSAL Handle fo the current object */
  obj_hdl = data->current_entry->obj_handle;

  /* Get the xattr_id */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  /* Get the xattr related to this xattr_id */
  if((buffer = gsh_calloc(1, XATTR_BUFFERSIZE)) == NULL)
    {
      LogEvent(COMPONENT_NFS_V4_XATTR,
               "FAILED to allocate xattr buffer");
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }

  fsal_status = obj_hdl->ops->getextattr_value_by_id(obj_hdl, data->req_ctx, xattr_id,
						     buffer, XATTR_BUFFERSIZE, &size_returned);

  if(FSAL_IS_ERROR(fsal_status))
    {
      res_READ4.status = NFS4ERR_SERVERFAULT;
      return res_READ4.status;
    }

  res_READ4.READ4res_u.resok4.data.data_len = size_returned;
  res_READ4.READ4res_u.resok4.data.data_val = buffer;

  res_READ4.READ4res_u.resok4.eof = TRUE;

  res_READ4.status = NFS4_OK;

  return res_READ4.status;
}                               /* nfs4_op_read_xattr */

/**
 * nfs4_op_write_xattr: Writes the content of a xattr attribute
 * 
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 * 
 * @return NFS4_OK if successfull, other values show an error. 
 * 
 */
#define arg_WRITE4 op->nfs_argop4_u.opwrite
#define res_WRITE4 resp->nfs_resop4_u.opwrite

int nfs4_op_write_xattr(struct nfs_argop4 *op,
                        compound_data_t * data, struct nfs_resop4 *resp)
{
  struct fsal_obj_handle *obj_hdl = NULL;
  file_handle_v4_t *pfile_handle = NULL;
  unsigned int xattr_id = 0;
  fsal_status_t fsal_status;

  /* Get the FSAL Handle fo the current object */
  obj_hdl = data->current_entry->obj_handle;

  /* Get the xattr_id */
  pfile_handle = (file_handle_v4_t *) (data->currentFH.nfs_fh4_val);

  /* for Xattr FH, we adopt the current convention:
   * xattr_pos = 0 ==> the FH is the one of the actual FS object
   * xattr_pos = 1 ==> the FH is the one of the xattr ghost directory 
   * xattr_pos > 1 ==> The FH is the one for the xattr ghost file whose xattr_id = xattr_pos -2 */
  xattr_id = pfile_handle->xattr_pos - 2;

  fsal_status = obj_hdl->ops->setextattr_value_by_id(obj_hdl,
                                                     data->req_ctx,
                                                     xattr_id,
						     arg_WRITE4.data.data_val,
						     arg_WRITE4.data.data_len);

  if(FSAL_IS_ERROR(fsal_status))
    {
      res_WRITE4.status = NFS4ERR_SERVERFAULT;
      return res_WRITE4.status;
    }

  res_WRITE4.WRITE4res_u.resok4.committed = FILE_SYNC4;

  res_WRITE4.WRITE4res_u.resok4.count = arg_WRITE4.data.data_len;
  memcpy(res_WRITE4.WRITE4res_u.resok4.writeverf, NFS4_write_verifier, sizeof(verifier4));

  res_WRITE4.status = NFS4_OK;

  return NFS4_OK;
}                               /* nfs4_op_write_xattr */

#define arg_REMOVE4 op->nfs_argop4_u.opremove
#define res_REMOVE4 resp->nfs_resop4_u.opremove
int nfs4_op_remove_xattr(struct nfs_argop4 *op, compound_data_t * data,
                         struct nfs_resop4 *resp)
{
  fsal_status_t fsal_status;
  struct fsal_obj_handle *obj_hdl = NULL;
  char *name;

  /* get the filename from the argument, it should not be empty */
  if(arg_REMOVE4.target.utf8string_len == 0)
    {
      res_REMOVE4.status = NFS4ERR_INVAL;
      return res_REMOVE4.status;
    }

  /* NFS4_OP_REMOVE can delete files as well as directory, it replaces NFS3_RMDIR and NFS3_REMOVE
   * because of this, we have to know if object is a directory or not */
  name = alloca(arg_REMOVE4.target.utf8string_len + 1);
  name[arg_REMOVE4.target.utf8string_len] = '\0';
  memcpy(name, arg_REMOVE4.target.utf8string_val,
         arg_REMOVE4.target.utf8string_len);

  /* Get the FSAL Handle fo the current object */
  obj_hdl = data->current_entry->obj_handle;

  /* Test RM7: remiving '.' should return NFS4ERR_BADNAME */
  if ((strcmp(name, ".") == 0) ||
      (strcmp(name, "..") == 0))
    {
      res_REMOVE4.status = NFS4ERR_BADNAME;
      return res_REMOVE4.status;
    }

  fsal_status = obj_hdl->ops->remove_extattr_by_name(obj_hdl, data->req_ctx, name);
  if(FSAL_IS_ERROR(fsal_status))
    {
      res_REMOVE4.status = NFS4ERR_SERVERFAULT;
      return res_REMOVE4.status;
    }

  res_REMOVE4.status = NFS4_OK;
  return res_REMOVE4.status;
}
