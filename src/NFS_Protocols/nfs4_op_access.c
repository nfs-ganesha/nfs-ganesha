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
  fsal_cred_t credentials;
  fsal_status_t st;

  /* do we need to test read/write/exec ? */
  int test_read, test_write, test_exec;
  /* NFSv4 rights that are to be set if read, write, exec are allowed */
  uint32_t nfsv4_read_mask, nfsv4_write_mask, nfsv4_exec_mask;

  uint32_t max_access =
      (ACCESS4_READ | ACCESS4_LOOKUP | ACCESS4_MODIFY | ACCESS4_EXTEND | ACCESS4_DELETE |
       ACCESS4_EXECUTE);
  char __attribute__ ((__unused__)) funcname[] = "nfs4_op_access";

  /* initialize output */
  res_ACCESS4.ACCESS4res_u.resok4.supported = 0;
  res_ACCESS4.ACCESS4res_u.resok4.access = 0;

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

  /* determine the rights to be tested in FSAL */
  test_read = test_exec = test_write = FALSE;
  nfsv4_read_mask = nfsv4_write_mask = nfsv4_exec_mask = 0;

  if(arg_ACCESS4.access & ACCESS4_READ)
    {
      /* we need to test read access in FSAL */
      test_read = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_READ;
      /* if read is allowed in FSAL, ACCESS4_READ will be granted */
      nfsv4_read_mask |= ACCESS4_READ;
    }

  if((arg_ACCESS4.access & ACCESS4_LOOKUP) && (attr.type == FSAL_TYPE_DIR))
    {
      /* we need to test execute access in FSAL */
      test_exec = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_LOOKUP;
      /* if exec is allowed in FSAL, ACCESS4_LOOKUP will be granted */
      nfsv4_exec_mask |= ACCESS4_LOOKUP;
    }

  if(arg_ACCESS4.access & ACCESS4_MODIFY)
    {
      /* we need to test write access in FSAL */
      test_write = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_MODIFY;
      /* if write is allowed in FSAL, ACCESS4_MODIFY will be granted */
      nfsv4_write_mask |= ACCESS4_MODIFY;
    }

  if(arg_ACCESS4.access & ACCESS4_EXTEND)
    {
      /* we need to test write access in FSAL */
      test_write = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXTEND;
      /* if write is allowed in FSAL, ACCESS4_EXTEND will be granted */
      nfsv4_write_mask |= ACCESS4_EXTEND;
    }

  if((arg_ACCESS4.access & ACCESS4_DELETE) && (attr.type == FSAL_TYPE_DIR))
    {
      /* we need to test write access in FSAL */
      test_write = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_DELETE;
      /* if write is allowed in FSAL, ACCESS4_DELETE will be granted */
      nfsv4_write_mask |= ACCESS4_DELETE;
    }

  if((arg_ACCESS4.access & ACCESS4_EXECUTE) && (attr.type != FSAL_TYPE_DIR))
    {
      /* we need to test execute access in FSAL */
      test_exec = TRUE;
      res_ACCESS4.ACCESS4res_u.resok4.supported |= ACCESS4_EXECUTE;
      /* if exec is allowed in FSAL, ACCESS4_LOOKUP will be granted */
      nfsv4_exec_mask |= ACCESS4_EXECUTE;
    }

  /* now, test R/W/X independently */

  if(test_read)
    {
      st = FSAL_test_access(data->pcontext, FSAL_R_OK, &attr);
      if(st.major == 0)
        {
          /* grant NFSv4 asked rights related to READ */
          res_ACCESS4.ACCESS4res_u.resok4.access |= nfsv4_read_mask;
        }
      else if(st.major != ERR_FSAL_ACCESS)
        {
          /* not an access error */
          res_ACCESS4.status = nfs4_Errno(cache_inode_error_convert(st));
          return res_ACCESS4.status;
        }
    }

  if(test_write)
    {
      st = FSAL_test_access(data->pcontext, FSAL_W_OK, &attr);
      if(st.major == 0)
        {
          /* grant NFSv4 asked rights related to WRITE */
          res_ACCESS4.ACCESS4res_u.resok4.access |= nfsv4_write_mask;
        }
      else if(st.major != ERR_FSAL_ACCESS)
        {
          /* not an access error */
          res_ACCESS4.status = nfs4_Errno(cache_inode_error_convert(st));
          return res_ACCESS4.status;
        }
    }

  if(test_exec)
    {
      st = FSAL_test_access(data->pcontext, FSAL_X_OK, &attr);
      if(st.major == 0)
        {
          /* grant NFSv4 asked rights related to EXEC */
          res_ACCESS4.ACCESS4res_u.resok4.access |= nfsv4_exec_mask;
        }
      else if(st.major != ERR_FSAL_ACCESS)
        {
          /* not an access error */
          res_ACCESS4.status = nfs4_Errno(cache_inode_error_convert(st));
          return res_ACCESS4.status;
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
