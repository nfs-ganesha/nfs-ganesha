/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2011)
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
 * \file    9p_proto_tools.c
 * \brief   9P version
 *
 * 9p_proto_tools.c : _9P_interpretor, protocol's service functions
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
#include <sys/types.h>
#include <pwd.h>
#include "nfs_core.h"
#include "stuff_alloc.h"
#include "log_macros.h"
#include "9p.h"

int _9p_init(  _9p_parameter_t * pparam ) 
{
  return 0 ;
} /* _9p_init */

int _9p_tools_get_fsal_op_context_by_uid( u32 uid, _9p_fid_t * pfid ) 
{
  char buff[1024];
  struct passwd p;
  struct passwd *pp;
  gid_t gid ;
  fsal_status_t fsal_status ;

  if((getpwuid_r( uid, &p, buff, MAXPATHLEN, &pp) != 0) || (pp == NULL))
    {
      LogFullDebug(COMPONENT_IDMAPPER, "getpwuid_r %d failed", uid ) ;
      return -ENOENT;
    }
  else
   gid = p.pw_gid ;
  

  fsal_status = FSAL_GetClientContext( &pfid->fsal_op_context,
                                       &pfid->pexport->FS_export_context,
                                       uid, gid, NULL, 0 ) ;
  if( FSAL_IS_ERROR( fsal_status ) )
   return -fsal_status.major ; 

  return 0 ;
} /* _9p_tools_get_fsal_cred */

int _9p_tools_get_fsal_op_context_by_name( int uname_len, char * uname_str, _9p_fid_t * pfid ) 
{
  char name[1024] ;
  uid_t uid ;

  strncpy( name, uname_str, uname_len) ;

  if(name2uid(name, &uid) )
    {
      LogFullDebug(COMPONENT_IDMAPPER, "uidmap_get mapped %s to uid= %d",
                   name, uid);
    }
  else
    return -ENOENT ;
  
  return _9p_tools_get_fsal_op_context_by_uid( uid, pfid ) ; 
} /* _9p_tools_get_fsal_cred */


int _9p_tools_errno( cache_inode_status_t cache_status )
{
  int rc = 0 ;

  switch( cache_status )
   {
     case CACHE_INODE_SUCCESS:
	rc = 0 ;
        break ;

     case CACHE_INODE_MALLOC_ERROR:
        rc = ENOMEM ;
        break ;

     case CACHE_INODE_NOT_A_DIRECTORY:
        rc = ENOTDIR ;
        break ;

     case CACHE_INODE_ENTRY_EXISTS:
	rc = EEXIST ;
        break ;

     case CACHE_INODE_DIR_NOT_EMPTY:
	rc = ENOTEMPTY ;
        break ;

     case CACHE_INODE_NOT_FOUND:
	rc = ENOENT ;
	break ;

     case CACHE_INODE_IS_A_DIRECTORY:
	rc = EISDIR ;
	break ;

     case CACHE_INODE_FSAL_EPERM:
     case CACHE_INODE_FSAL_ERR_SEC:
	rc = EPERM ;
	break ;

     case CACHE_INODE_INVALID_ARGUMENT:
     case CACHE_INODE_NAME_TOO_LONG:
     case CACHE_INODE_UNAPPROPRIATED_KEY:
     case CACHE_INODE_INCONSISTENT_ENTRY:
     case CACHE_INODE_FSAL_ERROR:
     case CACHE_INODE_BAD_TYPE: 
     case CACHE_INODE_STATE_CONFLICT:
     case CACHE_INODE_STATE_ERROR:
     case CACHE_INODE_POOL_MUTEX_INIT_ERROR:
     case CACHE_INODE_INIT_ENTRY_FAILED:
	rc = EINVAL ;
        break ;

     case CACHE_INODE_NO_SPACE_LEFT:
	rc = ENOSPC ;
        break ;

     case CACHE_INODE_READ_ONLY_FS: 
	rc = EROFS ;
	break ;

     case CACHE_INODE_FSAL_ESTALE:
     case CACHE_INODE_DEAD_ENTRY:
	rc = ESTALE ;
        break ;

     case CACHE_INODE_QUOTA_EXCEEDED:
	rc = EDQUOT ;
        break ; 

     case CACHE_INODE_CACHE_CONTENT_ERROR:
     case CACHE_INODE_CACHE_CONTENT_EXISTS:
     case CACHE_INODE_CACHE_CONTENT_EMPTY:
     case CACHE_INODE_IO_ERROR:
     case CACHE_INODE_ASYNC_POST_ERROR:
     case CACHE_INODE_GET_NEW_LRU_ENTRY:
     case CACHE_INODE_LRU_ERROR:
     case CACHE_INODE_HASH_SET_ERROR:
     case CACHE_INODE_INSERT_ERROR:
     case CACHE_INODE_HASH_TABLE_ERROR:
	rc = EIO ;
        break ;

     case CACHE_INODE_NOT_SUPPORTED:
	rc = ENOTSUP ;
        break ;

     case CACHE_INODE_FSAL_EACCESS:
	rc = EACCES ;
	break ;

     case CACHE_INODE_FSAL_DELAY:
	rc = EAGAIN ;
        break ;

     default:
	rc = EIO ;
	break ; 
   }

  return rc ;
} /* _9p_tools_errno */

void _9p_tools_fsal_attr2stat( fsal_attrib_list_t * pfsalattr, struct stat * pstat )
{
  /* zero output structure */
  memset( (char *)pstat, 0, sizeof( struct stat ) ) ;

  pstat->st_dev   = 0 ; /* Omitted in 9P */
  pstat->st_ino   = pfsalattr->fileid ;

  pstat->st_mode  = pfsalattr->mode ;
  if( pfsalattr->type == FSAL_TYPE_DIR ) pstat->st_mode  |= __S_IFDIR  ;
  if( pfsalattr->type == FSAL_TYPE_FILE ) pstat->st_mode |= __S_IFREG  ;
  if( pfsalattr->type == FSAL_TYPE_LNK ) pstat->st_mode  |= __S_IFLNK  ;
  if( pfsalattr->type == FSAL_TYPE_SOCK ) pstat->st_mode |= __S_IFSOCK ;
  if( pfsalattr->type == FSAL_TYPE_BLK ) pstat->st_mode  |= __S_IFBLK  ;
  if( pfsalattr->type == FSAL_TYPE_CHR ) pstat->st_mode  |= __S_IFCHR  ;
  if( pfsalattr->type == FSAL_TYPE_FIFO ) pstat->st_mode |= __S_IFIFO  ;

  pstat->st_nlink   = (u32)pfsalattr->numlinks ;
  pstat->st_uid     = pfsalattr->owner ;
  pstat->st_gid     = pfsalattr->group ;
  pstat->st_rdev    = pfsalattr->rawdev.major ;
  pstat->st_size    = pfsalattr->filesize ;
  pstat->st_blksize = 4096 ;
  pstat->st_blocks  = (pfsalattr->filesize/4096) + 1 ; 
  pstat->st_atime   = pfsalattr->atime.seconds ;
  pstat->st_mtime   = pfsalattr->mtime.seconds ;
  pstat->st_ctime   = pfsalattr->ctime.seconds ;

} /* _9p_tools_fsal_attr2stat */

void _9p_tools_acess2fsal( u32 * paccessin, fsal_accessflags_t * pfsalaccess )
{
  memset( (char *)pfsalaccess, 0 , sizeof( fsal_accessflags_t ) ) ;

  if( *paccessin & O_WRONLY ) *pfsalaccess |= FSAL_W_OK ;
  if( *paccessin & O_RDONLY ) *pfsalaccess |= FSAL_R_OK ;
  if( *paccessin & O_RDWR )   *pfsalaccess |= FSAL_R_OK|FSAL_W_OK ; 
} /* _9p_tools_acess2fsal */
