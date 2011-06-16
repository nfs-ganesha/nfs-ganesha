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
 * \file    nfs_creds.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:23 $
 * \version $Revision: 1.4 $
 * \brief   Prototypes for the RPC credentials used in NFS.
 *
 * nfs_creds.h : Prototypes for the RPC credentials used in NFS.
 *
 *
 */

#ifndef _NFS_CREDS_H
#define _NFS_CREDS_H

#include <pthread.h>
#include <sys/types.h>
#include <sys/param.h>

#include "rpc.h"
#include "LRU_List.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"

#include "err_LRU_List.h"
#include "err_HashTable.h"

typedef enum CredType__
{ CRED_NONE = 1, CRED_UNIX = 2, CRED_GSS = 3 } CredType_t;

typedef struct CredUnix__
{
  u_int uid;
  u_int gid;
  /* May be we could had list of groups management */
} CredUnix_t;

typedef struct CredGss__
{
#if(  defined( HAVE_KRB5 ) && defined ( _HAVE_GSSAPI ) )
  gss_qop_t qop;
  gss_OID mech;
  rpc_gss_svc_t svc;
  gss_ctx_id_t context;
#else
  int dummy;
#endif
} CredGss_t;

typedef union CredData__
{
#ifdef HAVE_KRB5
  CredUnix_t unix_cred;
  CredGss_t gss_cred;
#else
  int dummy;
#endif
} CredData_t;

typedef struct RPCSEC_GSS_cred__
{
  CredType_t type;
  CredData_t data;
} RPCSEC_GSS_cred_t;

#endif                          /* _NFS_CREDS_H */
