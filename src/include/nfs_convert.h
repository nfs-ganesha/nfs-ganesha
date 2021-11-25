/* SPDX-License-Identifier: LGPL-3.0-or-later */
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file    nfs_convert.h
 * @brief   Prototypes for miscellaneous conversion routines.
 *
 * nfs_convert.h :  Prototypes for miscellaneous conversion routines.
 *
 *
 */

#ifndef _NFS_CONVERT_H
#define _NFS_CONVERT_H

#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "fsal.h"

char *nfsstat3_to_str(nfsstat3 code);
char *nfsstat4_to_str(nfsstat4 code);
char *nfstype3_to_str(ftype3 code);
const char *auth_stat2str(enum auth_stat);

#ifdef _USE_NFS3
const char *nfsproc3_to_str(int nfsproc3);
#endif
const char *nfsop4_to_str(int nfsop4);

uint64_t nfs_htonl64(uint64_t);
uint64_t nfs_ntohl64(uint64_t);

/* Error conversion routines */
nfsstat4 nfs4_Errno_verbose(fsal_status_t, const char *);
#define nfs4_Errno_status(e) nfs4_Errno_verbose(e, __func__)
#ifdef _USE_NFS3
nfsstat3 nfs3_Errno_verbose(fsal_status_t, const char *);
#define nfs3_Errno_status(e) nfs3_Errno_verbose(e, __func__)
#endif

#endif				/* _NFS_CONVERT_H */
