/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
 * \file    mnt_Umnt.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:52:14 $
 * \version $Revision: 1.7 $
 * \brief   MOUNTPROC_UMNT for Mount protocol v1 and v3.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include "HashData.h"
#include "HashTable.h"
#ifdef _USE_GSSRPC
#include <gssrpc/types.h>
#include <gssrpc/rpc.h>
#include <gssrpc/auth.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/auth.h>
#include <rpc/pmap_clnt.h>
#endif

#include "log_functions.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"

/**
 * mnt_Umnt: The Mount proc umount function, for all versions.
 *  
 *  @param parg        [IN]
 *  @param pexportlist [IN]
 *	@param pcontextp      [IN]
 *  @param pclient     [INOUT]
 *  @param ht          [INOUT] 
 *  @param preq        [IN] 
 *	@param pres        [OUT]
 *
 */

int mnt_Umnt(nfs_arg_t * parg /* IN     */ ,
             exportlist_t * pexport /* IN     */ ,
             fsal_op_context_t * pcontext /* IN     */ ,
             cache_inode_client_t * pclient /* INOUT  */ ,
             hash_table_t * ht /* INOUT  */ ,
             struct svc_req *preq /* IN     */ ,
             nfs_res_t * pres /* OUT    */ )
{
  char *hostname;

  DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                    "REQUEST PROCESSING: Calling mnt_Umnt");

  /* @todo: BUGAZOMEU; seul AUTHUNIX est supporte */
  hostname = ((struct authunix_parms *)(preq->rq_clntcred))->aup_machname;

  if (hostname == NULL)
    {
      DisplayLogJdLevel(pclient->log_outputs, NIV_CRIT,
                        "/!\\ | UMOUNT: NULL passed as Umount argument !!!");
      return NFS_REQ_DROP;
    }

  /* BUGAZOMEU: pas de verif sur le path */
  if (!nfs_Remove_MountList_Entry(hostname, NULL))
    {
      DisplayLogJd(pclient->log_outputs,
                   "UMOUNT: /!\\ | Cannot remove mount entry for client %s", hostname);
    }
  DisplayLogJdLevel(pclient->log_outputs, NIV_EVENT,
                    "UMOUNT: Client %s was removed from mount list", hostname);

  return NFS_REQ_OK;
}                               /* mnt_Umnt */

/**
 * mnt_Umnt_Free: Frees the result structure allocated for mnt_Umnt.
 * 
 * Frees the result structure allocated for mnt_UmntAll.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt_Umnt_Free(nfs_res_t * pres)
{
  /* Nothing to do */
  return;
}                               /* mnt_Umnt_Free */
