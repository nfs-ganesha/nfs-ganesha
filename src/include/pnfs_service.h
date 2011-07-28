/*
 *
 *
 * Copyright CEA/DAM/DIF  (2010)
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

#ifndef _PNFS_SERVICE_H
#define _PNFS_SERVICE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#ifdef _USE_PNFS

#ifdef _USE_PNFS_PARALLEL_FS
#include "PNFS/PARALLEL_FS/pnfs_layout4_nfsv4_1_files.h"
#endif

#ifdef _USE_PNFS_SPNFS_LIKE
#include "PNFS/SPNFS_LIKE/pnfs_layout4_nfsv4_1_files.h"
#endif


#endif                          /* _USE_PNFS */

#endif                          /* _PNFS_SERVICE_H */
