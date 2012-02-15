/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
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
 * ---------------------------------------
 */

/**
 * \file    pnfs_layoutcommit.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:02:50 $
 * \version $Revision: 1.8 $
 * \brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * pnfs_lock.c : Routines used for managing the NFS4 COMPOUND functions.
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
#include <stdint.h>
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "pnfs.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_file_handle.h"
#include "nfs_tools.h"
#include "nfs_proto_tools.h"

pnfs_functions_t pnfs_functions ;

nfsstat4 pnfs_getdevicelist( GETDEVICELIST4args * pargs, 
                             compound_data_t * data,
                             GETDEVICELIST4res  * pres ) 
{
  return pnfs_functions.getdevicelist( pargs, data, pres ) ;
}

nfsstat4 pnfs_getdeviceinfo( GETDEVICEINFO4args * pargs, 
                             compound_data_t * data,
                             GETDEVICEINFO4res  * pres ) 
{
  return pnfs_functions.getdeviceinfo( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutcommit( LAYOUTCOMMIT4args * pargs, 
                            compound_data_t * data,
                            LAYOUTCOMMIT4res  * pres ) 
{
  return pnfs_functions.layoutcommit( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutget( LAYOUTGET4args   * pargs, 
                         compound_data_t  * data,
                         LAYOUTGET4res    * pres ) 
{ 
  return pnfs_functions.layoutget( pargs, data, pres ) ;
}

nfsstat4 pnfs_layoutreturn( LAYOUTRETURN4args * pargs, 
                            compound_data_t   * data,
                            LAYOUTRETURN4res  * pres ) 
{
  return pnfs_functions.layoutreturn( pargs, data, pres ) ;
}

void pnfs_layoutget_Free( LAYOUTGET4res * pres ) 
{ 
  return pnfs_functions.layoutget_Free( pres ) ;
}

void pnfs_layoutcommit_Free( LAYOUTCOMMIT4res * pres ) 
{
  return pnfs_functions.layoutcommit_Free( pres ) ;
}

void pnfs_layoutreturn_Free( LAYOUTRETURN4res * pres ) 
{
  return pnfs_functions.layoutreturn_Free( pres ) ;
}

void pnfs_getdevicelist_Free(  GETDEVICELIST4res  * pres ) 
{
  return pnfs_functions.getdevicelist_Free( pres ) ;
}

void pnfs_getdeviceinfo_Free(  GETDEVICEINFO4res  * pres ) 
{
  return pnfs_functions.getdeviceinfo_Free( pres ) ;
}

pnfs_functions_t pNFS_LoadFunctions( void ) 
{
  pnfs_functions = pNFS_GetFunctions() ;
}

