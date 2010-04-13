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
 * \file    nfs_stat.h
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/24 11:43:15 $
 * \version $Revision: 1.6 $
 * \brief   Functions to be used for nfs and mount statistics
 *
 * nfs_stat.h :  Functions to be used for nfs and mount statistics.
 *
 *
 */

#ifndef _NFS_STAT_H
#define _NFS_STAT_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <pthread.h>

#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/svc.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"

#define NFS_V2_NB_COMMAND 18
static char *nfsv2_function_names[] = {
  "NFSv2_null", "NFSv2_getattr", "NFSv2_setattr", "NFSv2_root",
  "NFSv2_lookup", "NFSv2_readlink", "NFSv2_read", "NFSv2_writecache",
  "NFSv2_write", "NFSv2_create", "NFSv2_remove", "NFSv2_rename",
  "NFSv2_link", "NFSv2_symlink", "NFSv2_mkdir", "NFSv2_rmdir",
  "NFSv2_readdir", "NFSv2_statfs"
};

#define NFS_V3_NB_COMMAND 22
static char *nfsv3_function_names[] = {
  "NFSv3_null", "NFSv3_getattr", "NFSv3_setattr", "NFSv3_lookup",
  "NFSv3_access", "NFSv3_readlink", "NFSv3_read", "NFSv3_write",
  "NFSv3_create", "NFSv3_mkdir", "NFSv3_symlink", "NFSv3_mknod",
  "NFSv3_remove", "NFSv3_rmdir", "NFSv3_rename", "NFSv3_link",
  "NFSv3_readdir", "NFSv3_readdirplus", "NFSv3_fsstat",
  "NFSv3_fsinfo", "NFSv3_pathconf", "NFSv3_commit"
};

#define NFS_V4_NB_COMMAND 2
static char *nfsv4_function_names[] = {
  "NFSv4_null", "NFSv4_compound"
};

#define MNT_V1_NB_COMMAND 6
#define MNT_V3_NB_COMMAND 6
static char *mnt_function_names[] = {
  "MNT_null", "MNT_mount", "MNT_dump", "MNT_umount", "MNT_umountall", "MNT_export"
};

#define NFS_V40_NB_OPERATION 39
#define NFS_V41_NB_OPERATION 58

typedef enum nfs_stat_type__
{ GANESHA_STAT_SUCCESS = 0,
  GANESHA_STAT_DROP = 1
} nfs_stat_type_t;

/* we support only upto NLMPROC4_UNLOCK */
#define NLM_V4_NB_OPERATION 5

typedef struct nfs_request_stat_item__
{
  unsigned int total;
  unsigned int success;
  unsigned int dropped;
} nfs_request_stat_item_t;

typedef struct nfs_request_stat__
{
  unsigned int nb_mnt1_req;
  unsigned int nb_mnt3_req;
  unsigned int nb_nfs2_req;
  unsigned int nb_nfs3_req;
  unsigned int nb_nfs4_req;
  unsigned int nb_nlm4_req;
  nfs_request_stat_item_t stat_req_mnt1[MNT_V1_NB_COMMAND];
  nfs_request_stat_item_t stat_req_mnt3[MNT_V3_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs2[NFS_V2_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs3[NFS_V3_NB_COMMAND];
  nfs_request_stat_item_t stat_req_nfs4[NFS_V4_NB_COMMAND];
  nfs_request_stat_item_t stat_op_nfs40[NFS_V40_NB_OPERATION];
  nfs_request_stat_item_t stat_op_nfs41[NFS_V41_NB_OPERATION];
  nfs_request_stat_item_t stat_req_nlm4[NLM_V4_NB_OPERATION];
} nfs_request_stat_t;

void nfs_stat_update(nfs_stat_type_t type,
                     nfs_request_stat_t * pstat_req, struct svc_req *preq);

#endif                          /* _NFS_STAT_H */
