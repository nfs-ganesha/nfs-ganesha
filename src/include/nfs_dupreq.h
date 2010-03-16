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
 * \file    nfs_dupreq.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:36:19 $
 * \version $Revision: 1.9 $
 * \brief   Prototypes for duplicate requsts cache management.
 *
 * nfs_dupreq.h : Prototypes for duplicate requsts cache management.
 *
 *
 */

#ifndef _NFS_DUPREQ_H
#define _NFS_DUPREQ_H

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#ifdef _SOLARIS
#ifndef _USE_SNMP
typedef unsigned long u_long;
#endif
#endif				/* _SOLARIS */

#include "HashData.h"
#include "HashTable.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"

typedef struct dupreq_entry__ {
  long xid;
  nfs_res_t res_nfs;
  u_long rq_prog;		/* service program number        */
  u_long rq_vers;		/* service protocol version      */
  u_long rq_proc;
  time_t timestamp;
  struct dupreq_entry__ *next_alloc;
} dupreq_entry_t;

unsigned int get_rpc_xid(struct svc_req *reqp);

int compare_xid(hash_buffer_t * buff1, hash_buffer_t * buff2);
int print_entry_dupreq(LRU_data_t data, char *str);
int clean_entry_dupreq(LRU_entry_t * pentry, void *addparam);
int nfs_dupreq_gc_function(LRU_entry_t * pentry, void *addparam);

nfs_res_t nfs_dupreq_get(long xid, int *pstatus);

int nfs_dupreq_add(long xid,
		   struct svc_req *ptr_req,
		   nfs_res_t * p_res_nfs,
		   LRU_list_t * lru_dupreq, dupreq_entry_t ** dupreq_pool);

unsigned long dupreq_value_hash_func(hash_parameter_t * p_hparam,
				     hash_buffer_t * buffclef);
unsigned long dupreq_rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
void nfs_dupreq_get_stats(hash_stat_t * phstat);

#define DUPREQ_SUCCESS             0
#define DUPREQ_INSERT_MALLOC_ERROR 1
#define DUPREQ_NOT_FOUND           2

#endif				/* _NFS_DUPREQ_H */
