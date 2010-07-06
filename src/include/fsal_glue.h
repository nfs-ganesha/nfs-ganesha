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

/**
 * \file    fsal_glue.h
 * \date    $Date: 2010/07/01 12:45:27 $
 *
 *
 */

#ifndef _FSAL_GLUE_H
#define _FSAL_GLUE_H


#define FSAL_INDEX_ARRAY_SIZE 2 
#define FSAL_XFS_INDEX 0 
#define FSAL_PROXY_INDEX 1

#define fsal_cred_t xfsfsal_cred_t
#define fsal_export_context_t xfsfsal_export_context_t
#define fs_specific_initinfo_t xfsfs_specific_initinfo_t
#define fsal_lockdesc_t xfsfsal_lockdesc_t
#define fsal_dir_t xfsfsal_dir_t
#define fsal_file_t xfsfsal_file_t
#define fsal_cookie_t xfsfsal_cookie_t

#define FSAL_HANDLE_T_SIZE 44 
typedef struct {
 char data[FSAL_HANDLE_T_SIZE] ;
} fsal_handle_t ;

#define FSAL_OP_CONTEXT_T_SIZE 144
typedef struct {
  char data[FSAL_OP_CONTEXT_T_SIZE] ;
} fsal_op_context_t ; 

#endif /* _FSAL_GLUE_H */

