/*
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * \file    fsal_pnfs_types.h
 * \brief   Management of the pNFS features: FSAL/PNFS types.
 *
 * fsal_pnfs_types.h : Management of the pNFS features: FSAL/PNFS types.
 *
 *
 */

#ifndef _FSAL_PNFS_TYPES_H
#define _FSAL_PNFS_TYPES_H

#ifdef _USE_PNFS_PARALLEL_FS 
#include "PNFS/PARALLEL_FS/pnfs_layout4_nfsv4_1_files_types.h"
#endif

#ifdef _USE_PNFS_SPNFS_LIKE
#include "PNFS/SPNFS_LIKE/pnfs_layout4_nfsv4_1_files_types.h"
#endif

/** PNFS layout mode */
typedef enum fsal_iomode__
{
  FSAL_IOMODE_READ       = 1,
  FSAL_IOMODE_READ_WRITE = 2,
  FSAL_IOMODE_ANY        = 3,
  FSAL_IOMODE_WRITE      = 4 
} fsal_iomode_t ;

typedef enum fsal_layout_type__
{
  FSAL_LAYOUT_FILES = 0x1,
  FSAL_LAYOUT_OSD   = 0x2,
  FSAL_LAYOUT_BLOCK = 0x3
} fsal_layout_type_t ;


#endif                          /* _FSAL_PNFS_TYPES_H */
