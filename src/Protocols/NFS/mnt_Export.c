/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
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
#include "log_macros.h"
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

  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Export");

  /* paranoid command, to avoid parasites in the result structure. */
  memset(pres, 0, sizeof(nfs_res_t));

  /* for each existing export entry */
  while(p_current_item)
    {

      exports new_expnode;      /* the export node to be added to the list */
      int buffsize;

      new_expnode = (exports) Mem_Alloc(sizeof(exportnode));

      /* paranoid command, to avoid parasites in the structure. */
      memset(new_expnode, 0, sizeof(exportnode));

      /* ---- ex_dir ------ */

      /* we set the export path */

      LogFullDebug(COMPONENT_NFSPROTO,
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

      if(p_current_item->clients.num_clients > 0)
        {

          /* Alias, to make the code slim... */
          exportlist_client_t *p_clients = &(p_current_item->clients);

          /* allocates the memory for all the groups, once for all */
          new_expnode->ex_groups =
              (groups) Mem_Alloc(p_clients->num_clients * sizeof(groupnode));

          /* paranoid command, to avoid parasites in the allocated strcuture. */
          memset(new_expnode->ex_groups, 0, p_clients->num_clients * sizeof(groupnode));

          for(i = 0; i < p_clients->num_clients; i++)
            {

              /* ---- gr_next ----- */

              if((i + 1) == p_clients->num_clients)     /* this is the last item */
                new_expnode->ex_groups[i].gr_next = NULL;
              else              /* other items point to the next memory slot */
                new_expnode->ex_groups[i].gr_next = &(new_expnode->ex_groups[i + 1]);

              /* ---- gr_name ----- */

              switch (p_clients->clientarray[i].type)
                {
                case HOSTIF_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  new_expnode->ex_groups[i].gr_name = Mem_Alloc(INET_ADDRSTRLEN + 1);

                  /* clears memory : */
                  memset(new_expnode->ex_groups[i].gr_name, 0, INET_ADDRSTRLEN + 1);
                  if(inet_ntop
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
                  if(inet_ntop
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

                  LogCrit(COMPONENT_NFSPROTO,
                          "MNT_EXPORT: Unknown export entry type: %d",
                          p_clients->clientarray[i].type);

                  new_expnode->ex_groups[i].gr_name = NULL;

                  return NFS_REQ_DROP;
                }

            }

        }
      else
        {
          /* There are no groups for this export entry. */
          new_expnode->ex_groups = NULL;

        }

      /* ---- ex_next ----- */

      /* this is the last item in the list */

      new_expnode->ex_next = NULL;

      /* we insert the export node to the export list */

      if(p_exp_out)
        {
          p_exp_current->ex_next = new_expnode;
          p_exp_current = new_expnode;
        }
      else
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
