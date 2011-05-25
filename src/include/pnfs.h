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
 * \file    pnfs.h
 * \author  $Author: deniel $
 * \date    $Date: 2010/01/27 12:44:15 $
 * \brief   Management of the pNFS features.
 *
 * pnfs.h : Management of the pNFS features.
 *
 *
 */

#ifndef _PNFS_H
#define _PNFS_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>


#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_exports.h"
#include "fsal_pnfs.h"

typedef union pnfs_parameter__
{
  pnfs_layoutfile_parameter_t layoutfile;
} pnfs_parameter_t;


nfsstat4 pnfs_getdevicelist( GETDEVICELIST4args * pargs, 
			     compound_data_t * data,
			     GETDEVICELIST4res  * pres ) ;

nfsstat4 pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, 
			     compound_data_t * data,
			     GETDEVICEINFO4res  * pres ) ;

nfsstat4 pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
			    compound_data_t * data,
			    LAYOUTCOMMIT4res  * pres ) ;

nfsstat4 pnfs_layoutget( LAYOUTGET4args   * pargs, 
			 compound_data_t  * data,
			 LAYOUTGET4res    * pres ) ;

nfsstat4 pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
			    compound_data_t   * data,
			    LAYOUTRETURN4res  * pres ) ; 

#endif                          /* _PNFS_H */
