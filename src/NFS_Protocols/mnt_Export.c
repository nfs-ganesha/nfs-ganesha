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
 * \file    mnt_Export.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/18 17:01:40 $
 * \version $Revision: 1.19 $
 * \brief   MOUNTPROC_EXPORT for Mount protocol v1 and v3.
 *
 * mnt_Null.c : MOUNTPROC_EXPORT in V1, V3.
 *
 * Exporting client hosts and networks OK.
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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

/* BUGAZOMEU: /!\ Free is used as 'Mem_Free'. BuddyMalloc stuff is to be put here */
#define Free(a ) Mem_Free( a )

/**
 * mnt_Export: The Mount proc export function, for all versions.
 * 
 * The MOUNT proc null function, for all versions.
 * 
 *  @param parg        [IN]    ignored
 *  @param pexportlist [IN]    The export list to be return to the client.
 *	@param pcontextp      [IN]    ignored
 *  @param pclient     [INOUT] ignored
 *  @param ht          [INOUT] ignored
 *  @param preq        [IN]    ignored 
 *	@param pres        [OUT]   Pointer to the export list.
 *
 */

int mnt_Export(nfs_arg_t * parg /* IN     */ ,
               exportlist_t * pexport /* IN     */ ,
               fsal_op_context_t * pcontext /* IN     */ ,
               cache_inode_client_t * pclient /* INOUT  */ ,
               hash_table_t * ht /* INOUT  */ ,
               struct svc_req *preq /* IN     */ ,
               nfs_res_t * pres /* OUT    */ )
{

  /* @todo : memset after Mem_Alloc */

  exportlist_t *p_current_item = pexport;       /* the current export item. */

  exports p_exp_out = NULL;     /* Pointer to the first export entry. */
  exports p_exp_current = NULL; /* Pointer to the last  export entry. */

  unsigned int i;

  DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                    "REQUEST PROCESSING: Calling mnt_Export");

  /* paranoid command, to avoid parasites in the result structure. */
  memset(pres, 0, sizeof(nfs_res_t));

  /* for each existing export entry */
  while (p_current_item)
    {

      exports new_expnode;      /* the export node to be added to the list */
      int buffsize;

      new_expnode = (exports) Mem_Alloc(sizeof(exportnode));

      /* paranoid command, to avoid parasites in the structure. */
      memset(new_expnode, 0, sizeof(exportnode));

      /* ---- ex_dir ------ */

      /* we set the export path */

      DisplayLogJdLevel(pclient->log_outputs, NIV_FULL_DEBUG,
                        "MNT_EXPORT: Export entry: %s | Numclients: %d | PtrClients: %p",
                        p_current_item->fullpath, p_current_item->clients.num_clients,
                        p_current_item->clients.clientarray);

      buffsize = strlen(p_current_item->fullpath) + 1;

      new_expnode->ex_dir = Mem_Alloc(buffsize);

      /* Buffers Init */
      memset(new_expnode->ex_dir, 0, buffsize);

      strncpy(new_expnode->ex_dir, p_current_item->fullpath, buffsize);

      /* ---- ex_groups ---- */

      /* we convert the group list */

      if (p_current_item->clients.num_clients > 0)
        {

          /* Alias, to make the code slim... */
          exportlist_client_t *p_clients = &(p_current_item->clients);

          /* allocates the memory for all the groups, once for all */
          new_expnode->ex_groups =
              (groups) Mem_Alloc(p_clients->num_clients * sizeof(groupnode));

          /* paranoid command, to avoid parasites in the allocated strcuture. */
          memset(new_expnode->ex_groups, 0, p_clients->num_clients * sizeof(groupnode));

          for (i = 0; i < p_clients->num_clients; i++)
            {

              /* ---- gr_next ----- */

              if ((i + 1) == p_clients->num_clients)    /* this is the last item */
                new_expnode->ex_groups[i].gr_next = NULL;
                else            /* other items point to the next memory slot */
                new_expnode->ex_groups[i].gr_next = &(new_expnode->ex_groups[i + 1]);

              /* ---- gr_name ----- */

              switch (p_clients->clientarray[i].type)
                {
                case HOSTIF_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(INET_ADDRSTRLEN + 1);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, INET_ADDRSTRLEN + 1);
                  if (inet_ntop
                      (AF_INET, &(p_clients->clientarray[i].client.hostif.clientaddr),
                       new_expnode->ex_groups[i].gr_name, INET_ADDRSTRLEN) == NULL)
                    {
                      strncpy(new_expnode->ex_groups[i].gr_name, "Invalid Host address",
                              MAXHOSTNAMELEN);
                    }

                  break;

                case NETWORK_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(INET_ADDRSTRLEN + 1);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, INET_ADDRSTRLEN + 1);
                  if (inet_ntop
                      (AF_INET, &(p_clients->clientarray[i].client.network.netaddr),
                       new_expnode->ex_groups[i].gr_name, INET_ADDRSTRLEN) == NULL)
                    {
                      strncpy(new_expnode->ex_groups[i].gr_name,
                              "Invalid Network address", MAXHOSTNAMELEN);
                    }

                  break;

                case NETGROUP_CLIENT:

                  /* allocates target buffer */

                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(MAXHOSTNAMELEN);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, MAXHOSTNAMELEN);

                  strncpy(new_expnode->ex_groups[i].gr_name,
                          p_clients->clientarray[i].client.netgroup.netgroupname,
                          MAXHOSTNAMELEN);

                  break;

                case WILDCARDHOST_CLIENT:

                  /* allocates target buffer */
                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(MAXHOSTNAMELEN);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, MAXHOSTNAMELEN);

                  strncpy(new_expnode->ex_groups[i].gr_name,
                          p_clients->clientarray[i].client.wildcard.wildcard,
                          MAXHOSTNAMELEN);
                  break;

                case GSSPRINCIPAL_CLIENT:

                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(MAXHOSTNAMELEN);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, MAXHOSTNAMELEN);

                  strncpy(new_expnode->ex_groups[i].gr_name,
                          p_clients->clientarray[i].client.gssprinc.princname,
                          MAXHOSTNAMELEN);

                  break;

                default:

                  /* Mem_Free resources and returns an error. */

                  /* @todo : Mem_Free allocated resources */

                  DisplayLogJdLevel(pclient->log_outputs, NIV_CRIT,
                                    "MNT_EXPORT: Unknown export entry type: %d",
                                    p_clients->clientarray[i].type);

                  new_expnode->ex_groups[i].gr_name = NULL;

                  return NFS_REQ_DROP;
                }

            }

        } else
        {
          /* There are no groups for this export entry. */
          new_expnode->ex_groups = NULL;

        }

      /* ---- ex_next ----- */

      /* this is the last item in the list */

      new_expnode->ex_next = NULL;

      /* we insert the export node to the export list */

      if (p_exp_out)
        {
          p_exp_current->ex_next = new_expnode;
          p_exp_current = new_expnode;
        } else
        {
          /* This is the first item in the list */
          p_exp_out = new_expnode;
          p_exp_current = new_expnode;
        }

      p_current_item = (exportlist_t *) (p_current_item->next);

    }

  /* return the pointer to the export list */

  pres->res_mntexport = p_exp_out;

  return NFS_REQ_OK;

}                               /* mnt_Export */

/**
 * mnt_Export_Free: Frees the result structure allocated for mnt_Export.
 * 
 * Frees the result structure allocated for mnt_Dump.
 * 
 * @param pres        [INOUT]   Pointer to the result structure.
 *
 */
void mnt_Export_Free(nfs_res_t * pres)
{

  /** @todo: BUGAZOMEU end this function */

  return;
}                               /* mnt_Export_Free */
