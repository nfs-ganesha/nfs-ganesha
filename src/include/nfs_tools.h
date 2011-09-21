/*
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
 */

/**
 * \file    nfs_tools.h
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:53:08 $
 * \version $Revision: 1.21 $
 * \brief   Prototypes for miscellaneous service routines.
 *
 * nfs_tools.h :  Prototypes for miscellaneous service routines.
 *
 *
 */

#ifndef _NFS_TOOLS_H
#define _NFS_TOOLS_H

#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_exports.h"
#include "config_parsing.h"

#define  ATTRVALS_BUFFLEN  1024

int nfs_ParseConfLine(char *Argv[],
                      int nbArgv,
                      char *line,
                      int (*separator_function) (char), int (*endLine_func) (char));

int ReadExports(config_file_t in_config, exportlist_t ** pEx);

exportlist_t *BuildDefaultExport();

/* Mount list management */
int nfs_Add_MountList_Entry(char *hostname, char *path);
int nfs_Remove_MountList_Entry(char *hostname, char *path);
int nfs_Init_MountList(void);
int nfs_Purge_MountList(void);
mountlist nfs_Get_MountList(void);
void nfs_Print_MountList(void);

char *nfsstat2_to_str(nfsstat2 code);
char *nfsstat3_to_str(nfsstat3 code);
char *nfsstat4_to_str(nfsstat4 code);
char *nfstype2_to_str(ftype2 code);
char *nfstype3_to_str(ftype3 code);

void nfs4_sprint_fhandle(nfs_fh4 * fh4p, char *outstr) ;

/* Hash and LRU functions */
unsigned long decimal_simple_hash_func(hash_parameter_t * p_hparam,
                                       hash_buffer_t * buffclef);
unsigned long decimal_rbt_hash_func(hash_parameter_t * p_hparam,
                                    hash_buffer_t * buffclef);

int display_cache(hash_buffer_t * pbuff, char *str);
int compare_cache(hash_buffer_t * buff1, hash_buffer_t * buff2);
int print_cache(LRU_data_t data, char *str);
int clean_cache(LRU_entry_t * pentry, void *addparam);

int lru_data_entry_to_str(LRU_data_t data, char *str);
int lru_inode_clean_entry(LRU_entry_t * entry, void *adddata);
int lru_data_clean_entry(LRU_entry_t * entry, void *adddata);
int lru_inode_entry_to_str(LRU_data_t data, char *str);

#endif                          /* _NFS_TOOLS_H */
