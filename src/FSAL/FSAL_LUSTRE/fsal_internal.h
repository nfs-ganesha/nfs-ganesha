/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 *
 * \file    fsal_internal.h
 * \version $Revision: 1.12 $
 * \brief   Extern definitions for variables that are
 *          defined in fsal_internal.c.
 * 
 */

#include "fsal.h"
#include <sys/stat.h>

/* defined the set of attributes supported with POSIX */
#define LUSTRE_SUPPORTED_ATTRIBUTES (                                       \
          ATTR_SUPPATTR | ATTR_TYPE     | ATTR_SIZE      | \
          ATTR_FSID     | ATTR_FILEID  | \
          ATTR_MODE     | ATTR_NUMLINKS | ATTR_OWNER     | \
          ATTR_GROUP    | ATTR_ATIME    | ATTR_RAWDEV    | \
          ATTR_CTIME    | ATTR_MTIME    | ATTR_SPACEUSED | \
          ATTR_CHGTIME  )

/* the following variables must not be defined in fsal_internal.c */
#ifndef FSAL_INTERNAL_C

/* static filesystem info.
 * read access only.
 */
extern struct fsal_staticfsinfo_t global_fs_info;

/* export_context_t is not given to every function, but
 * most functions need to use the open-by-handle funcionality.
 */
extern char open_by_handle_path[MAXPATHLEN];
extern int open_by_handle_fd;

#endif

/**
 *  Increments the number of calls for a function.
 */
void fsal_increment_nbcall(int function_index, fsal_status_t status);


#if 0
/**
 * Gets a fd from a handle 
 */
fsal_status_t fsal_internal_handle2fd(fsal_op_context_t * p_context,
                                      fsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_handle2fd_at(int dirfd,
                                         fsal_handle_t * phandle, int *pfd, int oflags);

fsal_status_t fsal_internal_Path2Handle(fsal_op_context_t * p_context,       /* IN */
                                        fsal_path_t * p_fsalpath,       /* IN */
                                        fsal_handle_t * p_handle /* OUT */ ) ;

/**
 * Access a link by a file handle.
 */
fsal_status_t fsal_readlink_by_handle(fsal_op_context_t * p_context,
                                      fsal_handle_t * p_handle, char *__buf, int maxlen);

/**
 * Get the handle for a path (posix or fid path)
 */
fsal_status_t fsal_internal_get_handle_at(int dfd, const char *name,    /* IN */
                                          fsal_handle_t * p_handle /* OUT */ );

fsal_status_t fsal_internal_fd2handle(  fsal_op_context_t * p_context,
					int fd,   /* IN */
                                        fsal_handle_t * p_handle  /* OUT */
    );

fsal_status_t fsal_internal_link_at(int srcfd, int dfd, char *name);

fsal_status_t fsal_stat_by_handle(fsal_op_context_t * p_context,
                                  fsal_handle_t * p_handle, struct stat64 *buf);

#endif
