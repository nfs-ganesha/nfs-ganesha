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
 * \file    fsal_glue_const.h
 * \date    $Date: 2010/07/01 12:45:27 $
 *
 *
 */

#ifndef _FSAL_GLUE_CONST_H
#define _FSAL_GLUE_CONST_H

#define FSAL_INDEX_ARRAY_SIZE 2
#define FSAL_XFS_INDEX 0
#define FSAL_PROXY_INDEX 1

#define FSAL_HANDLE_T_SIZE 148
#define FSAL_OP_CONTEXT_T_SIZE 620
#define FSAL_DIR_T_SIZE 4876
#define FSAL_EXPORT_CONTEXT_T_SIZE 4204
#define FSAL_FILE_T_SIZE 184
#define FSAL_COOKIE_T_SIZE 8
#define FSAL_LOCKDESC_T_SIZE 24
#define FSAL_CRED_T_SIZE 140
#define FSAL_FS_SPECIFIC_INITINFO_T 17208

#endif                          /* _FSAL_GLUE_CONST_H */
