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

#ifndef _PNFS_LAYOUT4_NFSV4_1_FILES__TYPES_H
#define _PNFS_LAYOUT4_NFSV4_1_FILES__TYPES_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif                          /* HAVE_CONFIG_H */

#ifdef _SOLARIS
#include "solaris_port.h"
#endif                          /* _SOLARIS */

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "RW_Lock.h"
#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log_macros.h"
#include "config_parsing.h"
#include "nfs23.h"
#include "nfs4.h"

#define NB_MAX_PNFS_DS 2
#define PNFS_NFS4      4
#define PNFS_SENDSIZE 32768
#define PNFS_RECVSIZE 32768

#define PNFS_LAYOUTFILE_FILEHANDLE_MAX_LEN 128
#define PNFS_LAYOUTFILE_PADDING_LEN  NFS4_OPAQUE_LIMIT
#define PNFS_LAYOUTFILE_OWNER_LEN 128

typedef struct pnfs_ds_parameter__
{
  unsigned int ipaddr;
  unsigned short ipport;
  unsigned int prognum;
  char rootpath[MAXPATHLEN];
  char ipaddr_ascii[MAXNAMLEN];
  unsigned int id;
  bool_t is_ganesha;
} pnfs_ds_parameter_t;

typedef struct pnfs_layoutfile_parameter__
{
  unsigned int stripe_size;
  unsigned int stripe_width;
  pnfs_ds_parameter_t ds_param[NB_MAX_PNFS_DS];
} pnfs_layoutfile_parameter_t;


typedef struct pnfs_client__
{
  unsigned int nb_ds;
} pnfs_client_t;


typedef struct pnfs_ds_loc__
{
  int nothing_right_now ;
}  pnfs_ds_loc_t ;


typedef struct pnfs_layoutfile_hints__
{
  int nothing_right_now ;
} pnfs_ds_hints_t ;
typedef struct fsal_layout__ /** @todo : make a better definition of this */
{
   unsigned int length ;
   char data[1024] ;
} fsal_layout_t ;

typedef struct fsal_layout_update__ /** @todo : make a better definition of this */
{
   unsigned int length ;
   char data[1024] ;
} fsal_layout_update_data_t ;

typedef struct fsal_layout_return__ /** @todo : make a better definition of this */
{
   unsigned int length ;
   char data[1024] ;
} fsal_layout_return_data_t ;

typedef struct nfs_fh4 fsal_pnfs_file_t ;

#endif                          /* _PNFS_LAYOUT4_NFSV4_1_FILES_H */
