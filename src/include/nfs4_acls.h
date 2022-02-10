/*
 * Copyright IBM Corporation, 2012
 *  Contributor: Frank Filz <ffilz@us.ibm.com>
 *
 * --------------------------
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
 *
 */

#ifndef _NFS4_ACLS_H
#define _NFS4_ACLS_H

#include "fsal_types.h"

/* Define the return value of ACL operation. */

typedef int fsal_acl_status_t;

#define NFS_V4_ACL_SUCCESS  0
#define NFS_V4_ACL_ERROR  1
#define NFS_V4_ACL_EXISTS  2
#define NFS_V4_ACL_INTERNAL_ERROR  3
#define NFS_V4_ACL_UNAPPROPRIATED_KEY  4
#define NFS_V4_ACL_HASH_SET_ERROR  5
#define NFS_V4_ACL_INIT_ENTRY_FAILED  6
#define NFS_V4_ACL_NOT_FOUND  7

fsal_acl_t *nfs4_acl_alloc(void);
fsal_ace_t *nfs4_ace_alloc(int nace);

void nfs4_acl_free(fsal_acl_t *acl);
void nfs4_ace_free(fsal_ace_t *pace);

void nfs4_acl_entry_inc_ref(fsal_acl_t *pacl);

fsal_acl_t *nfs4_acl_new_entry(fsal_acl_data_t *pacldata,
			       fsal_acl_status_t *pstatus);

fsal_acl_status_t nfs4_acl_release_entry(fsal_acl_t *pacl);

int nfs4_acls_init(void);

#endif				/* _NFS4_ACLS_H */
