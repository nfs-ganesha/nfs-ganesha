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
 * \file    nfs4_op_create.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.18 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_create.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_tools.h"
#include "nfs_file_handle.h"

/**
 * nfs4_op_create: NFS4_OP_CREATE, creates a non-regular entry.
 * 
 * NFS4_OP_CREATE, creates a non-regular entry.
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */

#define arg_CREATE4 op->nfs_argop4_u.opcreate
#define res_CREATE4 resp->nfs_resop4_u.opcreate

int nfs4_op_create(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_create";

  cache_entry_t        * pentry_parent = NULL;
  cache_entry_t        * pentry_new = NULL;
  fsal_attrib_list_t     attr_parent;
  fsal_attrib_list_t     attr_new;
  fsal_attrib_list_t     sattr;
  fsal_handle_t        * pnewfsal_handle = NULL;
  nfs_fh4                newfh4;
  cache_inode_status_t   cache_status;
  int                    convrc = 0;
  fsal_accessmode_t      mode = 0777;
  fsal_name_t            name;
#ifdef _USE_QUOTA
  fsal_status_t          fsal_status ;
#endif
  cache_inode_create_arg_t create_arg;
  unsigned int             i = 0;

  memset(&create_arg, 0, sizeof(create_arg));

  resp->resop = NFS4_OP_CREATE;
  res_CREATE4.status = NFS4_OK;

  /* Do basic checks on a filehandle */
  res_CREATE4.status = nfs4_sanity_check_FH(data, 0LL);
  if(res_CREATE4.status != NFS4_OK)
    return res_CREATE4.status;

#ifdef _USE_QUOTA
  /* if quota support is active, then we should check is the FSAL allows inode creation or not */
  fsal_status = FSAL_check_quota( data->pexport->fullpath, 
                                  FSAL_QUOTA_INODES,
                                  FSAL_OP_CONTEXT_TO_UID( data->pcontext ) ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
    {
      res_CREATE4.status = NFS4ERR_DQUOT ;
      return res_CREATE4.status;
    }
#endif /* _USE_QUOTA */

  /* Ask only for supported attributes */
  if(!nfs4_Fattr_Supported(&arg_CREATE4.createattrs))
    {
      res_CREATE4.status = NFS4ERR_ATTRNOTSUPP;
      return res_CREATE4.status;
    }

  /* Do not use READ attr, use WRITE attr */
  if(!nfs4_Fattr_Check_Access(&arg_CREATE4.createattrs, FATTR4_ATTR_WRITE))
    {
      res_CREATE4.status = NFS4ERR_INVAL;
      return res_CREATE4.status;
    }

  /* Check for name to long */
  if(arg_CREATE4.objname.utf8string_len > FSAL_MAX_NAME_LEN)
    {
      res_CREATE4.status = NFS4ERR_NAMETOOLONG;
      return res_CREATE4.status;
    }

  /* 
   * This operation is used to create a non-regular file, 
   * this means: - a symbolic link
   *             - a block device file
   *             - a character device file
   *             - a socket file
   *             - a fifo
   *             - a directory 
   *
   * You can't use this operation to create a regular file, you have to use NFS4_OP_OPEN for this
   */

  /* Convert the UFT8 objname to a regular string */
  if(arg_CREATE4.objname.utf8string_len == 0)
    {
      res_CREATE4.status = NFS4ERR_INVAL;
      return res_CREATE4.status;
    }

  if(utf82str(name.name, sizeof(name.name), &arg_CREATE4.objname) == -1)
    {
      res_CREATE4.status = NFS4ERR_INVAL;
      return res_CREATE4.status;
    }
  name.len = strlen(name.name);

  /* Sanuty check: never create a directory named '.' or '..' */
  if(arg_CREATE4.objtype.type == NF4DIR)
    {
      if(!FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT)
         || !FSAL_namecmp(&name, (fsal_name_t *) & FSAL_DOT_DOT))
        {
          res_CREATE4.status = NFS4ERR_BADNAME;
          return res_CREATE4.status;
        }

    }

  /* Filename should contain not slash */
  for(i = 0; i < name.len; i++)
    {
      if(name.name[i] == '/')
        {
          res_CREATE4.status = NFS4ERR_BADCHAR;
          return res_CREATE4.status;
        }
    }
  /* Convert current FH into a cached entry, the current_pentry (assocated with the current FH will be used for this */
  pentry_parent = data->current_entry;

  /* The currentFH must point to a directory (objects are always created within a directory) */
  if(data->current_filetype != DIRECTORY)
    {
      res_CREATE4.status = NFS4ERR_NOTDIR;
      return res_CREATE4.status;
    }

  /* get attributes of parent directory, for 'change4' info replyed */
  if((cache_status = cache_inode_getattr(pentry_parent,
                                         &attr_parent,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_CREATE4.status = nfs4_Errno(cache_status);
      return res_CREATE4.status;
    }

  res_CREATE4.CREATE4res_u.resok4.cinfo.before
       = cache_inode_get_changeid4(pentry_parent);

  /* Convert the incoming fattr4 to a vattr structure, if such arguments are supplied */
  if(arg_CREATE4.createattrs.attrmask.bitmap4_len != 0)
    {
      /* Arguments were supplied, extract them */
      convrc = nfs4_Fattr_To_FSAL_attr(&sattr, &(arg_CREATE4.createattrs));

      if(convrc != NFS4_OK)
      	{
          res_CREATE4.status = convrc;
          return res_CREATE4.status;
      	}
    }

  /* Create either a symbolic link or a directory */
  switch (arg_CREATE4.objtype.type)
    {
    case NF4LNK:
      /* Convert the name to link from into a regular string */
      if(arg_CREATE4.objtype.createtype4_u.linkdata.utf8string_len == 0)
        {
          res_CREATE4.status = NFS4ERR_INVAL;
          return res_CREATE4.status;
        }
      else
        {
          if(utf82str
             (create_arg.link_content.path, sizeof(create_arg.link_content.path),
              &arg_CREATE4.objtype.createtype4_u.linkdata) == -1)
            {
              res_CREATE4.status = NFS4ERR_INVAL;
              return res_CREATE4.status;
            }
          create_arg.link_content.len = strlen(create_arg.link_content.path);
        }

      /* do the symlink operation */
      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          SYMBOLIC_LINK,
                                          mode,
                                          &create_arg,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }

      break;
    case NF4DIR:
      /* Create a new directory */

      /* The create_arg structure contains the information "newly created directory"
       * to be passed to cache_inode_new_entry from cache_inode_create */
      create_arg.newly_created_dir = TRUE ;

      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          DIRECTORY,
                                          mode,
                                          &create_arg,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      break;

    case NF4SOCK:

      /* Create a new socket file */
      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          SOCKET_FILE,
                                          mode,
                                          NULL,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      break;

    case NF4FIFO:

      /* Create a new socket file */
      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          FIFO_FILE,
                                          mode,
                                          NULL,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      break;

    case NF4CHR:

      create_arg.dev_spec.major = arg_CREATE4.objtype.createtype4_u.devdata.specdata1;
      create_arg.dev_spec.minor = arg_CREATE4.objtype.createtype4_u.devdata.specdata2;

      /* Create a new socket file */
      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          CHARACTER_FILE,
                                          mode,
                                          &create_arg,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      break;

    case NF4BLK:

      create_arg.dev_spec.major = arg_CREATE4.objtype.createtype4_u.devdata.specdata1;
      create_arg.dev_spec.minor = arg_CREATE4.objtype.createtype4_u.devdata.specdata2;

      /* Create a new socket file */
      if((pentry_new = cache_inode_create(pentry_parent,
                                          &name,
                                          BLOCK_FILE,
                                          mode,
                                          &create_arg,
                                          &attr_new,
                                          data->pcontext, &cache_status)) == NULL)
        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          return res_CREATE4.status;
        }

      /* If entry exists pentry_new is not null but cache_status was set */
      if(cache_status == CACHE_INODE_ENTRY_EXISTS)
        {
          res_CREATE4.status = NFS4ERR_EXIST;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      break;

    default:
      /* Should never happen, but return NFS4ERR_BADTYPE in this case */
      res_CREATE4.status = NFS4ERR_BADTYPE;
      return res_CREATE4.status;
      break;
    }                           /* switch( arg_CREATE4.objtype.type ) */

  /* Now produce the filehandle to this file */
  pnewfsal_handle = &pentry_new->handle;

  /* Allocation of a new file handle */
  if(nfs4_AllocateFH(&newfh4) != NFS4_OK)
    {
      res_CREATE4.status = NFS4ERR_SERVERFAULT;
      cache_inode_put(pentry_new);
      return res_CREATE4.status;
    }

  /* Building the new file handle */
  if(!nfs4_FSALToFhandle(&newfh4, pnewfsal_handle, data))
    {
      res_CREATE4.status = NFS4ERR_SERVERFAULT;
      cache_inode_put(pentry_new);
      return res_CREATE4.status;
    }

  /* This new fh replaces the current FH */
  data->currentFH.nfs_fh4_len = newfh4.nfs_fh4_len;
  memcpy(data->currentFH.nfs_fh4_val, newfh4.nfs_fh4_val, newfh4.nfs_fh4_len);

  /* No do not need newfh any more */
  gsh_free(newfh4.nfs_fh4_val);

  /* Set the mode if requested */
  /* Use the same fattr mask for reply, if one attribute was not settable, NFS4ERR_ATTRNOTSUPP was replyied */
  res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_len =
      arg_CREATE4.createattrs.attrmask.bitmap4_len;

  if(arg_CREATE4.createattrs.attrmask.bitmap4_len != 0)
    {
      if (data->pcontext->credential.user != 0)
        {
          /* Setting uid/gid only works for root. */
          sattr.asked_attributes &= ~(FSAL_ATTR_OWNER | FSAL_ATTR_GROUP);
        }
      if((cache_status = cache_inode_setattr(pentry_new,
                                             &sattr,
                                             data->pcontext,
                                             &cache_status)) != CACHE_INODE_SUCCESS)

        {
          res_CREATE4.status = nfs4_Errno(cache_status);
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }

      /* Allocate a new bitmap */
      res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_val =
        gsh_calloc(res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_len,
                   sizeof(uint32_t));

      if(res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_val == NULL)
        {
          res_CREATE4.status = NFS4ERR_SERVERFAULT;
          cache_inode_put(pentry_new);
          return res_CREATE4.status;
        }
      memcpy(res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_val,
             arg_CREATE4.createattrs.attrmask.bitmap4_val,
             res_CREATE4.CREATE4res_u.resok4.attrset.bitmap4_len
             * sizeof(uint32_t));
    }

  /* Get the change info on parent directory after the operation was successfull */
  if((cache_status = cache_inode_getattr(pentry_parent,
                                         &attr_parent,
                                         data->pcontext,
                                         &cache_status)) != CACHE_INODE_SUCCESS)
    {
      res_CREATE4.status = nfs4_Errno(cache_status);
      cache_inode_put(pentry_new);
      return res_CREATE4.status;
    }
  memset(&(res_CREATE4.CREATE4res_u.resok4.cinfo.after), 0, sizeof(changeid4));
  res_CREATE4.CREATE4res_u.resok4.cinfo.after
       = cache_inode_get_changeid4(pentry_parent);

  /* Operation is supposed to be atomic .... */
  res_CREATE4.CREATE4res_u.resok4.cinfo.atomic = FALSE;

  LogFullDebug(COMPONENT_NFS_V4,
               "CREATE CINFO before = %"PRIu64"  after = %"PRIu64"  atomic = %d",
               res_CREATE4.CREATE4res_u.resok4.cinfo.before,
               res_CREATE4.CREATE4res_u.resok4.cinfo.after,
               res_CREATE4.CREATE4res_u.resok4.cinfo.atomic);

  /* @todo : BUGAZOMEU: fair ele free dans cette fonction */

  /* Keep the vnode entry for the file in the compound data */

  if (data->current_entry) {
      cache_inode_put(data->current_entry);
  }

  /* Update stuff on compound data, do not have to call nfs4_SetCompoundExport
   * because the new file is on the same export, so data->pexport and
   * data->export_perms will not change.
   */
  data->current_entry = pentry_new;
  data->current_filetype = pentry_new->type;

  /* If you reach this point, then no error occured */
  res_CREATE4.status = NFS4_OK;

  return res_CREATE4.status;
}                               /* nfs4_op_create */

/**
 * nfs4_op_create_Free: frees what was allocared to handle nfs4_op_create.
 *
 * Frees what was allocared to handle nfs4_op_create.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 *
 */
void nfs4_op_create_Free(CREATE4res * resp)
{
  if(resp->status == NFS4_OK)
    gsh_free(resp->CREATE4res_u.resok4.attrset.bitmap4_val);

  return;
}                               /* nfs4_op_create_Free */
