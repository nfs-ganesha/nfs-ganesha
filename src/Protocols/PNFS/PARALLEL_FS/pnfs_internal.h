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
 * \file    pnfs_internal.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/08 12:45:27 $
 * \version $Revision: 1.19 $
 * \brief   File System Abstraction Layer types and constants.
 *
 *
 *
 */

#ifndef _PNFS_PARALLEL_FS_INTERNAL_H
#define _PNFS_PARALLEL_FS_INTERNAL_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define MAX_PNFS_DS 2

typedef struct pnfs_ds_parameter__
{
  unsigned int ipaddr;
  unsigned short ipport;
  unsigned int prognum;
  char rootpath[MAXPATHLEN];
  char ipaddr_ascii[MAXNAMLEN+1];
  unsigned int id;
  bool_t is_ganesha;
} pnfs_ds_parameter_t;

typedef struct pnfs_layoutfile_parameter__
{
  unsigned int stripe_size;
  unsigned int stripe_width;
  pnfs_ds_parameter_t ds_param[MAX_PNFS_DS];
} pnfs_parameter_t;

nfsstat4 PARALLEL_FS_pnfs_getdevicelist( GETDEVICELIST4args * pargs, 
             			         compound_data_t * data,
			                 GETDEVICELIST4res  * pres ) ;

nfsstat4 PARALLEL_FS_pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, 
			                 compound_data_t * data,
			                 GETDEVICEINFO4res  * pres ) ;

nfsstat4 PARALLEL_FS_pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
			                compound_data_t * data,
			                LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 PARALLEL_FS_pnfs_layoutget( LAYOUTGET4args   * pargs, 
			             compound_data_t  * data,
			             LAYOUTGET4res    * pres ) ;

nfsstat4 PARALLEL_FS_pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
			                compound_data_t   * data,
			                LAYOUTRETURN4res  * pres ) ; 

#endif  /*  _PNFS_PARALLEL_FS_INTERNAL_H  */
