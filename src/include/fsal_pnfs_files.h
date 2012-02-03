/*
 *
 * Copyright (C) 2011 Linux Box Corporation
 * Author: Adam C. Emerson
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
 * \file    fsal_pnfs_files.h
 * \brief   Common utility functions for LAYOUT4_NFSV4_1_FILES
 *
 * fsal_pnfs_files.h: Utility functions for LAYOUT4_NFSV4_1_FILES
 *
 *
 */

#ifndef _FSAL_PNFS_FILES_H
#define _FSAL_PNFS_FILES_H

#include "nfs4.h"
#include "fsal_pnfs.h"

/* The next 3 line are mandatory for proper autotools based management */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _PNFS_DS

/**
 * This type exists soleley so arrays of hosts can be passed to
 * FSAL_encode_multipath_list.
 *
 */

typedef struct fsal_multipath_member
{
     uint16_t proto;
     uint32_t addr;
     uint16_t port;
} fsal_multipath_member_t;

nfsstat4 FSAL_fh4_dshandle(fsal_handle_t *fsal_handle,
                          unsigned short export_id,
                          fsal_export_context_t *export,
                          nfs_fh4 *wirehandle);

nfsstat4 FSAL_encode_file_layout(XDR *xdrs,
                                 fsal_op_context_t *context,
                                 const struct pnfs_deviceid *deviceid,
                                 nfl_util4 util,
                                 uint32_t first_idx,
                                 offset4 ptrn_ofst,
                                 uint32_t num_fhs,
                                 fsal_handle_t *fhs);

nfsstat4 FSAL_encode_v4_multipath(XDR *xdrs,
                                  uint32_t num_hosts,
                                  fsal_multipath_member_t *hosts);

#endif /* _PNFS_DS */
#endif /* _FSAL_PNFS_FILES_H */
