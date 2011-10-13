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

#define FSAL_HANDLE_T_SIZE            152  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_OP_CONTEXT_T_SIZE        680  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_FILE_T_SIZE              192  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_DIR_T_SIZE              4880  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_EXPORT_CONTEXT_T_SIZE   4208  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_COOKIE_T_SIZE             16  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_FS_SPECIFIC_INITINFO_T 17216  /* Has to be a multiple of 8 for alignement reasons */
#define FSAL_CRED_T_SIZE              144  /* Has to be a multiple of 8 for alignement reasons */

/* Const related to multiple FSAL support */
#ifdef _USE_SHARED_FSAL
#define NB_AVAILABLE_FSAL 11
#else
#define NB_AVAILABLE_FSAL 1 /* No need to allocate more than once in the static case */
#endif

#define FSAL_CEPH_ID     0
#define FSAL_HPSS_ID     1 
#define FSAL_SNMP_ID     2   
#define FSAL_ZFS_ID      3    
#define FSAL_FUSELIKE_ID 4  
#define FSAL_LUSTRE_ID   5  
#define FSAL_POSIX_ID    6 
#define FSAL_VFS_ID      7   
#define FSAL_GPFS_ID     8   
#define FSAL_PROXY_ID    9
#define FSAL_XFS_ID      10

#endif                          /* _FSAL_GLUE_CONST_H */
