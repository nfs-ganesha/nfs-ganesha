/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 */

/**
 * \file    nfs4_op_access.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.12 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * nfs4_op_access.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
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

/**
 * nfs4_op_access: NFS4_OP_ACCESS, checks for file's accessibility. 
 * 
 * NFS4_OP_ACCESS, checks for file's accessibility. 
 *
 * @param op    [IN]    pointer to nfs4_op arguments
 * @param data  [INOUT] Pointer to the compound request's data
 * @param resp  [IN]    Pointer to nfs4_op results
 *
 * @return NFS4_OK if successfull, other values show an error.  
 * 
 */
#define arg_ACCESS4 op->nfs_argop4_u.opaccess
#define res_ACCESS4 resp->nfs_resop4_u.opaccess

int nfs4_op_access(struct nfs_argop4 *op, compound_data_t * data, struct nfs_resop4 *resp)
{
  fsal_attrib_list_t attr;
  fsal_accessflags_t read_flag;
  fsal_accessflags_t write_flag;
  fsal_accessflags_t exec_flag;
  fsal_cred_t credentials;

  uint32_t max_access =
      (ACCESS4_READ | ACCESS4_LOOKUP | ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE |
       ACCESS4_EXECUTE);
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_access";

  resp->resop = NFS4_OP_ACCESS;
  res_ACCESS4.status = NFS4_OK;

  /* If there is no FH */
  if(nfs4_Is_Fh_Empty(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_NOFILEHANDLE;
      return res_ACCESS4.status;
    }

  /* If the filehandle is invalid */
  if(nfs4_Is_Fh_Invalid(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_BADHANDLE;
      return res_ACCESS4.status;
    }

  /* Tests if the Filehandle is expired (for volatile filehandle) */
  if(nfs4_Is_Fh_Expired(&(data->currentFH)))
    {
      res_ACCESS4.status = NFS4ERR_FHEXPIRED;
      return res_ACCESS4.status;
    }

  /* If Filehandle points to a pseudo fs entry, manage it via pseudofs specific functions */
  if(nfs4_Is_Fh_Pseudo(&(data->currentFH)))
    return nfs4_op_access_pseudo(op, data, resp);

  /* If Filehandle points to a xattr object, manage it via the xattrs specific functions */
  if(nfs4_Is_Fh_Xattr(&(data->currentFH)))
    return nfs4_op_access_xattr(op, data, resp);

  /* Check for input parameter's sanity */
  if(arg_ACCESS4.access > max_access)
    {
      res_ACCESS4.status = NFS4ERR_INVAL;
      return res_ACCESS4.status;
    }

  /* Get the attributes for the object */
  cache_inode_get_attributes(data->current_entry, &attr);

#if defined( _USE_POSIX )
  credentials = data->pcontext->credential;
#elif defined( _USE_HPSS )
  credentials = data->pcontext->credential;
#elif defined( _USE_FUSE )
  credentials = data->pcontext->credential;
#elif defined( _USE_SNMP )
  credentials = data->pcontext->user_credential;
#elif defined( _USE_PROXY )
  credentials = data->pcontext->user_credential;
#elif defined( _USE_LUSTRE )
  credentials = data->pcontext->credential;
#else
#error "This FSAL is not supported"
#endif

#if !defined( _USE_HPSS )
  if(credentials.user == attr.owner)
#else
  if(credentials.hpss_usercred.Uid == attr.owner)
#endif
    {
      read_flag = FSAL_MODE_RUSR;
      write_flag = FSAL_MODE_WUSR;
      exec_flag = FSAL_MODE_XUSR;
    }
#if !defined( _USE_HPSS )
  else if(credentials.group == attr.group)   /** @todo make smater group ownership test */
#else
  else if(credentials.hpss_usercred.Gid == attr.group)
#endif
    {
      read_flag = FSAL_MODE_RGRP;
      write_flag = FSAL_MODE_WGRP;
      exec_flag = FSAL_MODE_XGRP;
    }
  else
    {
      read_flag = FSAL_MODE_ROTH;
      write_flag = FSAL_MODE_WOTH;
      exec_flag = FSAL_MODE_XOTH;
    }

  if(arg_ACCESS4.access & ACCESS4_READ)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_READ;
#if !defined( _USE_HPSS )
      if((attr.mode & read_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & read_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_READ;
        }
    }

  if((arg_ACCESS4.access & ACCESS4_LOOKUP) && (attr.type == FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_LOOKUP;
#if !defined( _USE_HPSS )
      if((attr.mode & exec_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & exec_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_LOOKUP;
        }
    }

  if(arg_ACCESS4.access & ACCESS4_MODIFY)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_MODIFY;
#if !defined( _USE_HPSS )
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_MODIFY;
        }
    }

  if(arg_ACCESS4.access & ACCESS4_EXTEND)
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXTEND;
#if !defined( _USE_HPSS )
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_EXTEND;
        }
    }

  if((arg_ACCESS4.access & ACCESS4_DELETE) && (attr.type == FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_DELETE;
#if !defined( _USE_HPSS )
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & write_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_DELETE;
        }
    }

  if((arg_ACCESS4.access & ACCESS4_EXECUTE) && (attr.type != FSAL_TYPE_DIR))
    {
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXECUTE;
#if !defined( _USE_HPSS )
      if((attr.mode & exec_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT) && credentials.user == 0))
#else
      if((attr.mode & exec_flag)
         || ((data->pexport->options & EXPORT_OPTION_ROOT)
             && credentials.hpss_usercred.Uid == 0))
#endif
        {
          res_ACCESS4.ACCESS4res_u.resok4.access |= ACCESS4_EXECUTE;
        }
    }

  res_ACCESS4.status = NFS4_OK;

  return res_ACCESS4.status;
}                               /* nfs4_op_access */

/**
 * nfs4_op_access_Free: frees what was allocared to handle nfs4_op_access.
 * 
 * Frees what was allocared to handle nfs4_op_access.
 *
 * @param resp  [INOUT]    Pointer to nfs4_op results
 *
 * @return nothing (void function )
 * 
 */
void nfs4_op_access_Free(ACCESS4res * resp)
{
  /* Nothing to be done */
  return;
}                               /* nfs4_op_access_Free */
