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

/* In the "static" case, original types are used, this is safer */
#ifdef _USE_SHARED_FSAL 

#define FSAL_HANDLE_T_SIZE 44 
//#define FSAL_HANDLE_T_SIZE 148 
typedef struct {
 char data[FSAL_HANDLE_T_SIZE] ;
} fsal_handle_t ;

#define FSAL_OP_CONTEXT_T_SIZE 144
//#define FSAL_OP_CONTEXT_T_SIZE 620
typedef struct {
  void * export_context ;
  char data[FSAL_OP_CONTEXT_T_SIZE] ;
} fsal_op_context_t ; 

#define FSAL_DIR_T_SIZE 4296
typedef struct {
  char data[FSAL_DIR_T_SIZE] ;
} fsal_dir_t ;

#define FSAL_EXPORT_CONTEXT_T_SIZE 4204
typedef struct {
  char data[FSAL_EXPORT_CONTEXT_T_SIZE] ;
} fsal_export_context_t ;

#define FSAL_FILE_T_SIZE 8
//#define FSAL_FILE_T_SIZE 184
typedef struct {
  char data[FSAL_FILE_T_SIZE] ;
} fsal_file_t ;

#define FSAL_COOKIE_T_SIZE 8
typedef struct {
  char data[FSAL_COOKIE_T_SIZE] ;
} fsal_cookie_t ;

#define FSAL_LOCKDESC_T_SIZE 24
typedef struct {
  char data[FSAL_LOCKDESC_T_SIZE] ;
} fsal_lockdesc_t ;

#define FSAL_CRED_T_SIZE 140
typedef struct {
  char data[FSAL_CRED_T_SIZE] ;
} fsal_cred_t ;

#define FSAL_FS_SPECIFIC_INITINFO_T 4096
//#define FSAL_FS_SPECIFIC_INITINFO_T 17208
typedef struct {
  char data[FSAL_FS_SPECIFIC_INITINFO_T] ;
} fs_specific_initinfo_t ;

#endif /* USE_SHARED_FSAL */

#endif /* _FSAL_GLUE_H */

