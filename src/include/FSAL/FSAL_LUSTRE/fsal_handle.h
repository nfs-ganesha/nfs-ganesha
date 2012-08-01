/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef LUSTRE_HANDLE_H
#define LUSTRE_HANDLE_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stddef.h> /* For having offsetof defined */

/* For llapi_quotactl */
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>

#ifndef AT_FDCWD
#error "Very old kernel and/or glibc"
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH           0x1000
#endif

#ifndef O_PATH
#define O_PATH 010000000
#endif

#ifndef O_NOACCESS
#define O_NOACCESS O_ACCMODE
#endif


typedef  struct lustre_file_handle 
{
    lustre_fid fid;
    /* used for FSAL_DIGEST_FILEID */
    unsigned long long inode;
} lustre_file_handle_t;  /**< FS object handle */

static inline int lustre_handle_to_path(  struct lustre_file_handle * out_handle, char * path )
{
  abort() ;
  return 1 ;
}

static inline int lustre_path_to_handle( const char * path, struct lustre_file_handle * out_handle )
{
  abort() ;
  return 1 ;
}

static inline int lustre_name_to_handle_at(struct lustre_file_handle * at_handle, const char *name,
                                           struct lustre_file_handle * out_handle, int flags)
{
  abort() ;
  return 1 ;
}

static inline int lustre_open_by_handle( struct lustre_file_handle * handle,
                                         int flags)
{
  abort() ;
  return 1 ;
}

static inline size_t lustre_sizeof_handle(struct lustre_file_handle *hdl)
{
	return (size_t)sizeof( struct lustre_file_handle ) ;
}

#endif /* LUSTRE_HANDLE_H */
