/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 *
 * nfs_remote_functions.c : Functions for NFS protocol through RPCs. 
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
#include "rpc.h"
#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_tools.h"
#include "mount.h"

#include "nfs_remote_functions.h"

static struct timeval TIMEOUT = { 25, 0 };

/**
 * mnt1_remote_Null: The Mount proc null function, v1.
 * 
 * The MOUNT proc null function, v1.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  ////printf("REQUEST PROCESSING: Calling mnt1_remote_Null\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC2_NULL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt1_remote_Mnt: The Mount proc mount function, v1.
 * 
 * The MOUNT proc mount function, v1.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_Mnt(CLIENT * clnt /* IN  */ ,
                    nfs_arg_t * parg /* IN  */ ,
                    nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt1_remote_Mnt\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(fhstatus2));
  return clnt_call(clnt, MOUNTPROC2_MNT,
                   (xdrproc_t) xdr_dirpath, (caddr_t) parg,
                   (xdrproc_t) xdr_fhstatus2, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt1_remote_Dump: The Mount proc dump function, v1.
 *  
 * The MOUNT proc dump function, v1
 *
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_Dump(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt1_remote_Dump\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(mountlist));
  return clnt_call(clnt, MOUNTPROC2_DUMP,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_mountlist, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt1_remote_Umnt: The Mount proc umount function, v1.
 *
 * The MOUNT proc umount function, v1
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_Umnt(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt1_remote_Umnt\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC2_UMNT,
                   (xdrproc_t) xdr_dirpath, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt1_remote_UmntAll: The Mount proc umount_all function, v1.
 *
 * The MOUNT proc umount_all function, v1
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_UmntAll(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt1_remote_UmntAll\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC2_UMNTALL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt1_remote_Export: The Mount proc export function, v1.
 *
 * The MOUNT proc export function, v1
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt1_remote_Export(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt1_remote_Export\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(exports));
  return clnt_call(clnt, MOUNTPROC2_EXPORT,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_exports, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_Null: The Mount proc null function, v3.
 * 
 * The MOUNT proc null function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_Null\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC3_NULL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_Mnt: The Mount proc mount function, v3.
 * 
 * The MOUNT proc mount function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_Mnt(CLIENT * clnt /* IN  */ ,
                    nfs_arg_t * parg /* IN  */ ,
                    nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_Mnt\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(mountres3));
  return clnt_call(clnt, MOUNTPROC3_MNT,
                   (xdrproc_t) xdr_dirpath, (caddr_t) parg,
                   (xdrproc_t) xdr_mountres3, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_Dump: The Mount proc dump function, v3.
 *  
 * The MOUNT proc dump function, v3
 *
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_Dump(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_Dump\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(mountlist));
  return clnt_call(clnt, MOUNTPROC3_DUMP,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_mountlist, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_Umnt: The Mount proc umount function, v3.
 *
 * The MOUNT proc umount function, v3
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_Umnt(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_Umnt\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC3_UMNT,
                   (xdrproc_t) xdr_dirpath, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_UmntAll: The Mount proc umount_all function, v3.
 *
 * The MOUNT proc umount_all function, v3
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_UmntAll(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_UmntAll\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, MOUNTPROC3_UMNTALL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * mnt3_remote_Export: The Mount proc export function, v3.
 *
 * The MOUNT proc export function, v3
 *  
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int mnt3_remote_Export(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling mnt3_remote_Export\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(exports));
  return clnt_call(clnt, MOUNTPROC3_EXPORT,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_exports, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Null: The NFS proc null function, v2.
 * 
 * The NFS proc null function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Null\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, NFSPROC_NULL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Getattr: The NFS proc getattr function, v2.
 * 
 * The NFS proc getattr function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Getattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Getattr\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(ATTR2res));
  return clnt_call(clnt, NFSPROC_GETATTR,
                   (xdrproc_t) xdr_fhandle2, (caddr_t) parg,
                   (xdrproc_t) xdr_ATTR2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Setattr: The NFS proc setattr function, v2.
 * 
 * The NFS proc setattr function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Setattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Setattr\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(ATTR2res));
  return clnt_call(clnt, NFSPROC_SETATTR,
                   (xdrproc_t) xdr_SETATTR2args, (caddr_t) parg,
                   (xdrproc_t) xdr_ATTR2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Root: The NFS proc root function, v2.
 * 
 * The NFS proc root function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Root(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Root\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, NFSPROC_ROOT,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Lookup: The NFS proc lookup function, v2.
 * 
 * The NFS proc lookup function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Lookup(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Lookup\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(DIROP2res));
  return clnt_call(clnt, NFSPROC_LOOKUP,
                   (xdrproc_t) xdr_diropargs2, (caddr_t) parg,
                   (xdrproc_t) xdr_DIROP2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Readlink: The NFS proc readlink function, v2.
 * 
 * The NFS proc readlink function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Readlink(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Readlink\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READLINK2res));
  return clnt_call(clnt, NFSPROC_READLINK,
                   (xdrproc_t) xdr_fhandle2, (caddr_t) parg,
                   (xdrproc_t) xdr_READLINK2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Read: The NFS proc read function, v2.
 * 
 * The NFS proc read function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Read(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Read\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READ2res));
  return clnt_call(clnt, NFSPROC_READ,
                   (xdrproc_t) xdr_READ2args, (caddr_t) parg,
                   (xdrproc_t) xdr_READ2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Writecache: The NFS proc writecache function, v2.
 * 
 * The NFS proc writecache function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Writecache(CLIENT * clnt /* IN  */ ,
                           nfs_arg_t * parg /* IN  */ ,
                           nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Writecache\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, NFSPROC_WRITECACHE,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Write: The NFS proc write function, v2.
 * 
 * The NFS proc write function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Write(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Write\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(ATTR2res));
  return clnt_call(clnt, NFSPROC_WRITE,
                   (xdrproc_t) xdr_WRITE2args, (caddr_t) parg,
                   (xdrproc_t) xdr_ATTR2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Create: The NFS proc create function, v2.
 * 
 * The NFS proc create function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Create(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Create\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(DIROP2res));
  return clnt_call(clnt, NFSPROC_CREATE,
                   (xdrproc_t) xdr_CREATE2args, (caddr_t) parg,
                   (xdrproc_t) xdr_DIROP2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Remove: The NFS proc remove function, v2.
 * 
 * The NFS proc remove function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Remove(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Remove\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(nfsstat2));
  return clnt_call(clnt, NFSPROC_REMOVE,
                   (xdrproc_t) xdr_diropargs2, (caddr_t) parg,
                   (xdrproc_t) xdr_nfsstat2, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Rename: The NFS proc rename function, v2.
 * 
 * The NFS proc rename function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Rename(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Rename\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(nfsstat2));
  return clnt_call(clnt, NFSPROC_RENAME,
                   (xdrproc_t) xdr_RENAME2args, (caddr_t) parg,
                   (xdrproc_t) xdr_nfsstat2, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Link: The NFS proc link function, v2.
 * 
 * The NFS proc link function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Link(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Link\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(nfsstat2));
  return clnt_call(clnt, NFSPROC_LINK,
                   (xdrproc_t) xdr_LINK2args, (caddr_t) parg,
                   (xdrproc_t) xdr_nfsstat2, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Symlink: The NFS proc symlink function, v2.
 * 
 * The NFS proc symlink function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Symlink(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Symlink\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(nfsstat2));
  return clnt_call(clnt, NFSPROC_SYMLINK,
                   (xdrproc_t) xdr_SYMLINK2args, (caddr_t) parg,
                   (xdrproc_t) xdr_nfsstat2, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Mkdir: The NFS proc mkdir function, v2.
 * 
 * The NFS proc mkdir function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Mkdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Mkdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(DIROP2res));
  return clnt_call(clnt, NFSPROC_MKDIR,
                   (xdrproc_t) xdr_CREATE2args, (caddr_t) parg,
                   (xdrproc_t) xdr_DIROP2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Rmdir: The NFS proc rmdir function, v2.
 * 
 * The NFS proc rmdir function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Rmdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Rmdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(nfsstat2));
  return clnt_call(clnt, NFSPROC_RMDIR,
                   (xdrproc_t) xdr_diropargs2, (caddr_t) parg,
                   (xdrproc_t) xdr_nfsstat2, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Readdir: The NFS proc readdir function, v2.
 * 
 * The NFS proc readdir function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Readdir(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Readdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READDIR2res));
  return clnt_call(clnt, NFSPROC_READDIR,
                   (xdrproc_t) xdr_READDIR2args, (caddr_t) parg,
                   (xdrproc_t) xdr_READDIR2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs2_remote_Fsstat: The NFS proc statfs function, v2.
 * 
 * The NFS proc statfs function, v2.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs2_remote_Fsstat(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs2_remote_Fsstat\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(STATFS2res));
  return clnt_call(clnt, NFSPROC_STATFS,
                   (xdrproc_t) xdr_fhandle2, (caddr_t) parg,
                   (xdrproc_t) xdr_STATFS2res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Null: The NFS proc null function, v3.
 * 
 * The NFS proc null function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Null(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Null\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, NFSPROC3_NULL,
                   (xdrproc_t) xdr_void, (caddr_t) parg,
                   (xdrproc_t) xdr_void, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Getattr: The NFS proc getattr function, v3.
 * 
 * The NFS proc getattr function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Getattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Getattr\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(GETATTR3res));
  return clnt_call(clnt, NFSPROC3_GETATTR,
                   (xdrproc_t) xdr_GETATTR3args, (caddr_t) parg,
                   (xdrproc_t) xdr_GETATTR3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Setattr: The NFS proc setattr function, v3.
 * 
 * The NFS proc setattr function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Setattr(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Setattr\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(SETATTR3res));
  return clnt_call(clnt, NFSPROC3_SETATTR,
                   (xdrproc_t) xdr_SETATTR3args, (caddr_t) parg,
                   (xdrproc_t) xdr_SETATTR3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Lookup: The NFS proc lookup function, v3.
 * 
 * The NFS proc lookup function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Lookup(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Lookup\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(LOOKUP3res));
  return clnt_call(clnt, NFSPROC3_LOOKUP,
                   (xdrproc_t) xdr_LOOKUP3args, (caddr_t) parg,
                   (xdrproc_t) xdr_LOOKUP3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Readlink: The NFS proc readlink function, v3.
 * 
 * The NFS proc readlink function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Readlink(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Readlink\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READLINK3res));
  return clnt_call(clnt, NFSPROC3_READLINK,
                   (xdrproc_t) xdr_READLINK3args, (caddr_t) parg,
                   (xdrproc_t) xdr_READLINK3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Read: The NFS proc read function, v3.
 * 
 * The NFS proc read function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Read(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Read\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READ3res));
  return clnt_call(clnt, NFSPROC3_READ,
                   (xdrproc_t) xdr_READ3args, (caddr_t) parg,
                   (xdrproc_t) xdr_READ3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Write: The NFS proc write function, v3.
 * 
 * The NFS proc write function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Write(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Write\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(WRITE3res));
  return clnt_call(clnt, NFSPROC3_WRITE,
                   (xdrproc_t) xdr_WRITE3args, (caddr_t) parg,
                   (xdrproc_t) xdr_WRITE3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Create: The NFS proc create function, v3.
 * 
 * The NFS proc create function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Create(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Create\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(CREATE3res));
  return clnt_call(clnt, NFSPROC3_CREATE,
                   (xdrproc_t) xdr_CREATE3args, (caddr_t) parg,
                   (xdrproc_t) xdr_CREATE3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Remove: The NFS proc remove function, v3.
 * 
 * The NFS proc remove function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Remove(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Remove\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(REMOVE3res));
  return clnt_call(clnt, NFSPROC3_REMOVE,
                   (xdrproc_t) xdr_REMOVE3args, (caddr_t) parg,
                   (xdrproc_t) xdr_REMOVE3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Rename: The NFS proc rename function, v3.
 * 
 * The NFS proc rename function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Rename(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Rename\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(RENAME3res));
  return clnt_call(clnt, NFSPROC3_RENAME,
                   (xdrproc_t) xdr_RENAME3args, (caddr_t) parg,
                   (xdrproc_t) xdr_RENAME3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Link: The NFS proc link function, v3.
 * 
 * The NFS proc link function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Link(CLIENT * clnt /* IN  */ ,
                     nfs_arg_t * parg /* IN  */ ,
                     nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Link\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(LINK3res));
  return clnt_call(clnt, NFSPROC3_LINK,
                   (xdrproc_t) xdr_LINK3args, (caddr_t) parg,
                   (xdrproc_t) xdr_LINK3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Symlink: The NFS proc symlink function, v3.
 * 
 * The NFS proc symlink function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Symlink(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Symlink\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(SYMLINK3res));
  return clnt_call(clnt, NFSPROC3_SYMLINK,
                   (xdrproc_t) xdr_SYMLINK3args, (caddr_t) parg,
                   (xdrproc_t) xdr_SYMLINK3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Mkdir: The NFS proc mkdir function, v3.
 * 
 * The NFS proc mkdir function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Mkdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Mkdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(MKDIR3res));
  return clnt_call(clnt, NFSPROC3_MKDIR,
                   (xdrproc_t) xdr_MKDIR3args, (caddr_t) parg,
                   (xdrproc_t) xdr_MKDIR3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Rmdir: The NFS proc rmdir function, v3.
 * 
 * The NFS proc rmdir function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Rmdir(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Rmdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(RMDIR3res));
  return clnt_call(clnt, NFSPROC3_RMDIR,
                   (xdrproc_t) xdr_RMDIR3args, (caddr_t) parg,
                   (xdrproc_t) xdr_RMDIR3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Readdir: The NFS proc readdir function, v3.
 * 
 * The NFS proc readdir function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Readdir(CLIENT * clnt /* IN  */ ,
                        nfs_arg_t * parg /* IN  */ ,
                        nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Readdir\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READDIR3res));
  return clnt_call(clnt, NFSPROC3_READDIR,
                   (xdrproc_t) xdr_READDIR3args, (caddr_t) parg,
                   (xdrproc_t) xdr_READDIR3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Fsstat: The NFS proc statfs function, v3.
 * 
 * The NFS proc statfs function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Fsstat(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Fsstat\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(FSSTAT3res));
  return clnt_call(clnt, NFSPROC3_FSSTAT,
                   (xdrproc_t) xdr_FSSTAT3args, (caddr_t) parg,
                   (xdrproc_t) xdr_FSSTAT3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Access: The NFS proc access function, v3.
 * 
 * The NFS proc access function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Access(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Access\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(ACCESS3res));
  return clnt_call(clnt, NFSPROC3_ACCESS,
                   (xdrproc_t) xdr_ACCESS3args, (caddr_t) parg,
                   (xdrproc_t) xdr_ACCESS3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Readdirplus: The NFS proc readdirplus function, v3.
 * 
 * The NFS proc readdirplus function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Readdirplus(CLIENT * clnt /* IN  */ ,
                            nfs_arg_t * parg /* IN  */ ,
                            nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Readdirplus\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(READDIRPLUS3res));
  return clnt_call(clnt, NFSPROC3_READDIRPLUS,
                   (xdrproc_t) xdr_READDIRPLUS3args, (caddr_t) parg,
                   (xdrproc_t) xdr_READDIRPLUS3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Fsinfo: The NFS proc fsinfo function, v3.
 * 
 * The NFS proc fsinfo function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Fsinfo(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Fsinfo\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(FSINFO3res));
  return clnt_call(clnt, NFSPROC3_FSINFO,
                   (xdrproc_t) xdr_FSINFO3args, (caddr_t) parg,
                   (xdrproc_t) xdr_FSINFO3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Pathconf: The NFS proc pathconf function, v3.
 * 
 * The NFS proc pathconf function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Pathconf(CLIENT * clnt /* IN  */ ,
                         nfs_arg_t * parg /* IN  */ ,
                         nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Pathconf\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(PATHCONF3res));
  return clnt_call(clnt, NFSPROC3_PATHCONF,
                   (xdrproc_t) xdr_PATHCONF3args, (caddr_t) parg,
                   (xdrproc_t) xdr_PATHCONF3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Commit: The NFS proc commit function, v3.
 * 
 * The NFS proc commit function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Commit(CLIENT * clnt /* IN  */ ,
                       nfs_arg_t * parg /* IN  */ ,
                       nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Commit\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(COMMIT3res));
  return clnt_call(clnt, NFSPROC3_COMMIT,
                   (xdrproc_t) xdr_COMMIT3args, (caddr_t) parg,
                   (xdrproc_t) xdr_COMMIT3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs3_remote_Mknod: The NFS proc mknod function, v3.
 * 
 * The NFS proc mknod function, v3.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]    
 *  @param pres        [OUT]   
 *
 */
int nfs3_remote_Mknod(CLIENT * clnt /* IN  */ ,
                      nfs_arg_t * parg /* IN  */ ,
                      nfs_res_t * pres /* OUT */ )
{
  //printf("REQUEST PROCESSING: Calling nfs3_remote_Mknod\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(MKNOD3res));
  return clnt_call(clnt, NFSPROC3_MKNOD,
                   (xdrproc_t) xdr_MKNOD3args, (caddr_t) parg,
                   (xdrproc_t) xdr_MKNOD3res, (caddr_t) pres, TIMEOUT);
}

/**
 * nfs4_remote_Null: The NFS proc null function, v4.
 * 
 * The NFS proc null function, v4.
 * 
 *  @param clnt        [IN]
 *
 */
int nfs4_remote_Null(CLIENT * clnt /* IN  */ )
{
  COMPOUND4args parg;
  COMPOUND4res pres;
//      printf("REQUEST PROCESSING: Calling nfs4_remote_Null\n");
  if(clnt == NULL)
    {
      return -1;
    }

  return clnt_call(clnt, NFSPROC4_NULL,
                   (xdrproc_t) xdr_void, (caddr_t) & parg,
                   (xdrproc_t) xdr_void, (caddr_t) & pres, TIMEOUT);
}

/**
 * nfs4_remote_COMPOUND: The NFS proc compound function, v4.
 * 
 * The NFS proc compound function, v4.
 * 
 *  @param clnt        [IN]
 *  @param parg        [IN]
 *  @param pres        [OUT]
 *
 */
int nfs4_remote_COMPOUND(CLIENT * clnt /* IN  */ ,
                         COMPOUND4args * parg /* IN  */ ,
                         COMPOUND4res * pres /* OUT */ )
{
//      printf("REQUEST PROCESSING: Calling nfs4_remote_COMPOUND\n");
  if(clnt == NULL)
    {
      return -1;
    }

  memset((char *)pres, 0, sizeof(COMPOUND4res) * parg->argarray.argarray_len);
  return clnt_call(clnt, NFSPROC4_COMPOUND,
                   (xdrproc_t) xdr_COMPOUND4args, (caddr_t) parg,
                   (xdrproc_t) xdr_COMPOUND4res, (caddr_t) pres, TIMEOUT);
}

/* Free functions */

/**
 * mnt1_remote_Null_Free: Frees the result structure allocated for mnt1_remote_Null
 * 
 * Frees the result structure allocated for mnt1_remote_Null. Does Nothing in fact.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_Null_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt1_remote_Mnt_Free: Frees the result structure allocated for mnt1_remote_Mnt.
 * 
 * Frees the result structure allocated for mnt1_remote_Mnt.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_Mnt_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt1_remote_Dump_Free: Frees the result structure allocated for mnt1_remote_Dump.
 * 
 * Frees the result structure allocated for mnt1_remote_Dump.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_Dump_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt1_remote_Export_Free: Frees the result structure allocated for mnt1_remote_Export.
 * 
 * Frees the result structure allocated for mnt1_remote_Export.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_Export_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt1_remote_Umnt_Free: Frees the result structure allocated for mnt1_remote_Umnt.
 * 
 * Frees the result structure allocated for mnt1_remote_Umnt.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_Umnt_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt1_remote_UmntAll_Free: Frees the result structure allocated for mnt1_remote_UmntAll.
 * 
 * Frees the result structure allocated for mnt1_remote_UmntAll.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt1_remote_UmntAll_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_Null_Free: Frees the result structure allocated for mnt3_remote_Null
 * 
 * Frees the result structure allocated for mnt3_remote_Null. Does Nothing in fact.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_Null_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_Mnt_Free: Frees the result structure allocated for mnt3_remote_Mnt.
 * 
 * Frees the result structure allocated for mnt3_remote_Mnt.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_Mnt_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_Dump_Free: Frees the result structure allocated for mnt3_remote_Dump.
 * 
 * Frees the result structure allocated for mnt3_remote_Dump.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_Dump_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_Export_Free: Frees the result structure allocated for mnt3_remote_Export.
 * 
 * Frees the result structure allocated for mnt3_remote_Export.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_Export_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_Umnt_Free: Frees the result structure allocated for mnt3_remote_Umnt.
 * 
 * Frees the result structure allocated for mnt3_remote_Umnt.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_Umnt_Free(nfs_res_t * pres)
{
  return;
}

/**
 * mnt3_remote_UmntAll_Free: Frees the result structure allocated for mnt3_remote_UmntAll.
 * 
 * Frees the result structure allocated for mnt3_remote_UmntAll.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt3_remote_UmntAll_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs2_remote_Null_Free: Frees the result structure allocated for nfs2_remote_Null.
 * 
 * Frees the result structure allocated for nfs2_remote_Null.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Null_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Getattr_Free: Frees the result structure allocated for nfs2_remote_Getattr.
 * 
 * Frees the result structure allocated for nfs2_remote_Getattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Getattr_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Setattr_Free: Frees the result structure allocated for nfs2_remote_Setattr.
 * 
 * Frees the result structure allocated for nfs2_remote_Setattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Setattr_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Lookup_Free: Frees the result structure allocated for nfs2_remote_Lookup.
 * 
 * Frees the result structure allocated for nfs2_remote_Lookup.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Lookup_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Read_Free: Frees the result structure allocated for nfs2_remote_Read.
 * 
 * Frees the result structure allocated for nfs2_remote_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Read_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Write_Free: Frees the result structure allocated for nfs2_remote_Write.
 * 
 * Frees the result structure allocated for nfs2_remote_Write.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Write_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Create_Free: Frees the result structure allocated for nfs2_remote_Create.
 * 
 * Frees the result structure allocated for nfs2_remote_Create.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Create_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Mkdir_Free: Frees the result structure allocated for nfs2_remote_Mkdir.
 * 
 * Frees the result structure allocated for nfs2_remote_Mkdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Mkdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Symlink_Free: Frees the result structure allocated for nfs2_remote_Symlink.
 * 
 * Frees the result structure allocated for nfs2_remote_Symlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Symlink_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Remove_Free: Frees the result structure allocated for nfs2_remote_Remove.
 * 
 * Frees the result structure allocated for nfs2_remote_Remove.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Remove_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Rmdir_Free: Frees the result structure allocated for nfs2_remote_Rmdir.
 * 
 * Frees the result structure allocated for nfs2_remote_Rmdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Rmdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Rename_Free: Frees the result structure allocated for nfs2_remote_Rename.
 * 
 * Frees the result structure allocated for nfs2_remote_Rename.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Rename_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Link_Free: Frees the result structure allocated for nfs2_remote_Link.
 * 
 * Frees the result structure allocated for nfs2_remote_Link.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Link_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Fsstat_Free: Frees the result structure allocated for nfs2_remote_Fsstat.
 * 
 * Frees the result structure allocated for nfs2_remote_Fsstat.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Fsstat_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Root_Free: Frees the result structure allocated for nfs2_remote_Root.
 * 
 * Frees the result structure allocated for nfs2_remote_Root.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Root_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs2_remote_Writecache_Free: Frees the result structure allocated for nfs2_remote_Writecache.
 * 
 * Frees the result structure allocated for nfs2_remote_Writecache.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Writecache_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs2_remote_Readdir_Free: Frees the result structure allocated for nfs2_remote_Readdir.
 * 
 * Frees the result structure allocated for nfs2_remote_Readdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Readdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs2_remote_Readlink_Free: Frees the result structure allocated for nfs2_remote_Readlink.
 * 
 * Frees the result structure allocated for nfs2_remote_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs2_remote_Readlink_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Null_Free: Frees the result structure allocated for nfs3_remote_Null.
 * 
 * Frees the result structure allocated for nfs3_remote_Null.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Null_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Getattr_Free: Frees the result structure allocated for nfs3_remote_Getattr.
 * 
 * Frees the result structure allocated for nfs3_remote_Getattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Getattr_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Setattr_Free: Frees the result structure allocated for nfs3_remote_Setattr.
 * 
 * Frees the result structure allocated for nfs3_remote_Setattr.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Setattr_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Lookup_Free: Frees the result structure allocated for nfs3_remote_Lookup.
 * 
 * Frees the result structure allocated for nfs3_remote_Lookup.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Lookup_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Access_Free: Frees the result structure allocated for nfs3_remote_Access.
 * 
 * Frees the result structure allocated for nfs3_remote_Access.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Access_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs3_remote_Readlink_Free: Frees the result structure allocated for nfs3_remote_Readlink.
 * 
 * Frees the result structure allocated for nfs3_remote_Readlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Readlink_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Write_Free: Frees the result structure allocated for nfs3_remote_Write.
 * 
 * Frees the result structure allocated for nfs3_remote_Write.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Write_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Create_Free: Frees the result structure allocated for nfs3_remote_Create.
 * 
 * Frees the result structure allocated for nfs3_remote_Create.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Create_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Mkdir_Free: Frees the result structure allocated for nfs3_remote_Mkdir.
 * 
 * Frees the result structure allocated for nfs3_remote_Mkdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Mkdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Symlink_Free: Frees the result structure allocated for nfs3_remote_Symlink.
 * 
 * Frees the result structure allocated for nfs3_remote_Symlink.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Symlink_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Mknod_Free: Frees the result structure allocated for nfs3_remote_Mknod.
 * 
 * Frees the result structure allocated for nfs3_remote_Mknod.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Mknod_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs3_remote_Remove_Free: Frees the result structure allocated for nfs3_remote_Remove.
 * 
 * Frees the result structure allocated for nfs3_remote_Remove.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Remove_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Rmdir_Free: Frees the result structure allocated for nfs3_remote_Rmdir.
 * 
 * Frees the result structure allocated for nfs3_remote_Rmdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Rmdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Rename_Free: Frees the result structure allocated for nfs3_remote_Rename.
 * 
 * Frees the result structure allocated for nfs3_remote_Rename.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Rename_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Link_Free: Frees the result structure allocated for nfs3_remote_Link.
 * 
 * Frees the result structure allocated for nfs3_remote_Link.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Link_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Readdir_Free: Frees the result structure allocated for nfs3_remote_Readdir.
 * 
 * Frees the result structure allocated for nfs3_remote_Readdir.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Readdir_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Readdirplus_Free: Frees the result structure allocated for nfs3_remote_Readdirplus.
 * 
 * Frees the result structure allocated for nfs3_remote_Readdirplus.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Readdirplus_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Fsstat_Free: Frees the result structure allocated for nfs3_remote_Fsstat.
 * 
 * Frees the result structure allocated for nfs3_remote_Fsstat.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Fsstat_Free(nfs_res_t * resp)
{
  return;
}

/**
 * nfs3_remote_Fsinfo_Free: Frees the result structure allocated for nfs3_remote_Fsinfo.
 * 
 * Frees the result structure allocated for nfs3_remote_Fsinfo.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Fsinfo_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs3_remote_Pathconf_Free: Frees the result structure allocated for nfs3_remote_Pathconf.
 * 
 * Frees the result structure allocated for nfs3_remote_Pathconf.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Pathconf_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs3_remote_Commit_Free: Frees the result structure allocated for nfs3_remote_Commit.
 * 
 * Frees the result structure allocated for nfs3_remote_Commit.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Commit_Free(nfs_res_t * pres)
{
  return;
}

/**
 * nfs3_remote_Read_Free: Frees the result structure allocated for nfs3_remote_Read.
 * 
 * Frees the result structure allocated for nfs3_remote_Read.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void nfs3_remote_Read_Free(nfs_res_t * resp)
{
  return;
}
