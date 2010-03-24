/*
 *
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2008)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 * ---------------------------------------
 */

/**
 * \file    cache_inode_async.h
 * \author  $Author: deniel $
 * \date    $Date: 2008/03/12 $
 * \version 1.0
 * \brief   Asynchronous (write back) management of the cached inode layer. 
 *
 * cache_inode_async.h : Asynchronous (write back) management of the cached inode layer
 *
 *
 */

#ifndef _CACHE_INODE__ASYNC_H
#define _CACHE_INODE__ASYNC_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "fsal_types.h"
#include "log_functions.h"
#include "config_parsing.h"
#include "nfs23.h"

typedef enum cache_inode_async_health__
{ CACHE_INODE_ASYNC_STAYING_ALIVE = 1,
  CACHE_INODE_ASYNC_ZOMBIE = 2,
  CACHE_INODE_ASYNC_DEAD = 3
} cache_inode_async_health_t;
typedef enum cache_inode_async_addr_type__
{ CACHE_INODE_ASYNC_ADDR_DIRECT = 1,
  CACHE_INODE_ASYNC_ADDR_INDIRECT = 2
} cache_inode_async_addr_type_t;

typedef struct cache_inode_synclet_data__
{
  unsigned int my_index;
  pthread_cond_t op_condvar;
  pthread_mutex_t mutex_op_condvar;
  fsal_op_context_t root_fsal_context;
  LRU_list_t *op_lru;
} cache_inode_synclet_data_t;

typedef enum cache_inode_async_op_type__
{
  CACHE_INODE_ASYNC_OP_CREATE = 0,
  CACHE_INODE_ASYNC_OP_LINK = 1,
  CACHE_INODE_ASYNC_OP_REMOVE = 2,
  CACHE_INODE_ASYNC_OP_RENAME_SRC = 3,
  CACHE_INODE_ASYNC_OP_RENAME_DST = 4,
  CACHE_INODE_ASYNC_OP_SETATTR = 5,
  CACHE_INODE_ASYNC_OP_TRUNCATE = 6
} cache_inode_async_op_type_t;

typedef struct cache_inode_async_op_create_args__
{
  fsal_handle_t *pfsal_handle_dir_pre;
  fsal_handle_t *pfsal_handle_obj_pre;
  fsal_u64_t fileid;
  fsal_handle_t *pfsal_handle_dir;
  fsal_nodetype_t object_type;
  fsal_name_t name;
  fsal_accessmode_t mode;
} cache_inode_async_op_create_args_t;

typedef struct cache_inode_async_op_create_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_create_res_t;

typedef struct cache_inode_async_op_link_args__
{
  fsal_handle_t *pfsal_handle_src;
  fsal_handle_t *pfsal_handle_dirdest;
  fsal_name_t name_link;
} cache_inode_async_op_link_args_t;

typedef struct cache_inode_async_op_link_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_link_res_t;

typedef struct cache_inode_async_op_remove_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_name_t name;
} cache_inode_async_op_remove_args_t;

typedef struct cache_inode_async_op_remove_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_remove_res_t;

typedef struct cache_inode_async_op_rename_args__
{
  fsal_handle_t *pfsal_handle_dirsrc;
  fsal_name_t name_src;
  fsal_handle_t *pfsal_handle_dirdest;
  fsal_name_t name_dest;
} cache_inode_async_op_rename_args_t;

typedef struct cache_inode_async_op_rename_res__
{
  fsal_attrib_list_t attrsrc;
  fsal_attrib_list_t attrdest;
} cache_inode_async_op_rename_res_t;

typedef struct cache_inode_async_op_setattr_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_attrib_list_t attr;
} cache_inode_async_op_setattr_args_t;

typedef struct cache_inode_async_op_setattr_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_setattr_res_t;

typedef struct cache_inode_async_op_truncate_args__
{
  fsal_handle_t *pfsal_handle;
  fsal_size_t size;
} cache_inode_async_op_truncate_args_t;

typedef struct cache_inode_async_op_truncate_res__
{
  fsal_attrib_list_t attr;
} cache_inode_async_op_truncate_res_t;

typedef union cache_inode_async_op_args__
{
  cache_inode_async_op_create_args_t create;
  cache_inode_async_op_link_args_t link;
  cache_inode_async_op_remove_args_t remove;
  cache_inode_async_op_rename_args_t rename;
  cache_inode_async_op_setattr_args_t setattr;
  cache_inode_async_op_truncate_args_t truncate;
} cache_inode_async_op_args_t;

typedef union cache_inode_async_op_res__
{
  cache_inode_async_op_create_res_t create;
  cache_inode_async_op_link_res_t link;
  cache_inode_async_op_remove_res_t remove;
  cache_inode_async_op_rename_res_t rename;
  cache_inode_async_op_setattr_res_t setattr;
  cache_inode_async_op_truncate_res_t truncate;
} cache_inode_async_op_res_t;

typedef struct cache_inode_async_op_desc__
{
  struct timeval op_time;
  cache_inode_async_op_type_t op_type;
  cache_inode_async_op_args_t op_args;
  cache_inode_async_op_res_t op_res;
  unsigned int synclet_index;
   fsal_status_t(*op_func) (struct cache_inode_async_op_desc__ *);
  fsal_op_context_t fsal_op_context;
  fsal_export_context_t fsal_export_context;
  hash_table_t *ht;
  struct cache_inode_async_op_desc__ *origine_pool;
  pthread_mutex_t *ppool_lock;
  struct cache_inode_async_op_desc__ *next;
  struct cache_inode_async_op_desc__ *next_alloc;
} cache_inode_async_op_desc_t;

typedef struct cache_inode_async_op_queue_param__
{
  LRU_parameter_t lru_param;
} cache_inode_async_op_queue_parameter_t;

void *cache_inode_synclet_thread(void *Arg);
void *cache_inode_asynchronous_dispatcher_thread(void *Arg);

/* Async Operations on FSAL */
fsal_status_t cache_inode_async_create(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_link(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_remove(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_rename_src(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_rename_dst(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_setattr(cache_inode_async_op_desc_t * popasyncdesc);
fsal_status_t cache_inode_async_truncate(cache_inode_async_op_desc_t * popasyncdesc);

#endif                          /* _CACHE_INODE__ASYNC_H */
