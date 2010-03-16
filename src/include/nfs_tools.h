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
 * Copyright CEA/DAM/DIF (2005)
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
char *nfstype2_to_str(ftype2 code);
char *nfstype3_to_str(ftype3 code);

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
