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

#include "HashTable.h"
#include "log.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_tools.h"
#include "mount.h"
#include "nfs_proto_functions.h"

/**
 * @brief The Mount proc export function, for all versions.
 *
 * The MOUNT proc null function, for all versions.
 *
 * @param[in]  parg     Ignored
 * @param[in]  pexport  The export list to be return to the client.
 * @param[in]  pcontext Ignored
 * @param[in]  pworker  Ignored
 * @param[in]  preq     Ignored
 * @param[out] pres     Pointer to the export list
 *
 */

int mnt_Export(nfs_arg_t *parg,
               exportlist_t *pexport,
	       struct req_op_context *req_ctx,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t * pres)
{

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

      new_expnode = gsh_calloc(1,sizeof(exportnode));

      /* ---- ex_dir ------ */

      /* we set the export path */

      LogFullDebug(COMPONENT_NFSPROTO,
                   "MNT_EXPORT: Export entry: %s | Numclients: %d | PtrClients: %p",
                   p_current_item->fullpath, p_current_item->clients.num_clients,
                   p_current_item->clients.clientarray);

      new_expnode->ex_dir = gsh_strdup(p_current_item->fullpath);

      /* ---- ex_groups ---- */

      /* we convert the group list */

      if(p_current_item->clients.num_clients > 0)
        {
          /* Alias, to make the code slim... */
          exportlist_client_t *p_clients = &(p_current_item->clients);
	  struct groupnode *grp;

          /* allocates the memory for all the groups, once for all */
          new_expnode->ex_groups = grp =
               gsh_calloc(p_clients->num_clients, sizeof(groupnode));

          for(i = 0; i < p_clients->num_clients; i++)
            {
              exportlist_client_entry_t * excl = p_clients->clientarray + i;
              char *grnam;

              switch (p_clients->clientarray[i].type)
                {
                case HOSTIF_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  grnam = gsh_malloc(INET_ADDRSTRLEN + 1);
                  if(grnam == NULL)
                    break;
                  if(inet_ntop(AF_INET, &excl->client.hostif.clientaddr,
                               grnam, INET_ADDRSTRLEN) == NULL)
                    {
                      strncpy(grnam, "???", INET_ADDRSTRLEN);
                    }

                  break;

                case NETWORK_CLIENT:

                  grnam = gsh_malloc(2*INET_ADDRSTRLEN + 2);
                  if(grnam == NULL)
                    break;

                  if(inet_ntop(AF_INET, &excl->client.network.netaddr,
                               grnam, INET_ADDRSTRLEN) == NULL)
                    {
                      strcpy(grnam, "???");
                    }
                  strcat(grnam, "/");
                  if(inet_ntop(AF_INET, &excl->client.network.netmask,
                               grnam + strlen(grnam), INET_ADDRSTRLEN) == NULL)
                    {
                      strcat(grnam, "???");
                    }

                  break;

                case NETGROUP_CLIENT:
                  grnam = gsh_strdup(excl->client.netgroup.netgroupname);
                  break;

                case WILDCARDHOST_CLIENT:
                  grnam = gsh_strdup(excl->client.wildcard.wildcard);
                  break;

                case GSSPRINCIPAL_CLIENT:
                  grnam = gsh_strdup(excl->client.gssprinc.princname);
                  break;

                default:
                  /* @todo : free allocated resources */
                  LogCrit(COMPONENT_NFSPROTO,
                          "MNT_EXPORT: Unknown export entry type: %d",
                          excl->type);
                  grnam = gsh_strdup("<unknown>");
                  break;
                }

              if(grnam == NULL)
                {
                  LogWarn(COMPONENT_NFSPROTO,
                          "Could not allocate memory for group name of %s, "
			  "skipping", new_expnode->ex_dir);
                }
              else
                {
                  grp->gr_name = grnam;
                  grp->gr_next = grp + 1;
		  grp++;
                }
            }
          if(grp != new_expnode->ex_groups) {
              grp[-1].gr_next = NULL; /* -1 is correct - we need the last one */
	  } else {
	      gsh_free(grp);
	      new_expnode->ex_groups = NULL;
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
        }
      else
        {
          /* This is the first item in the list */
          p_exp_out = new_expnode;
        }
      p_exp_current = new_expnode;
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

  exports e = pres->res_mntexport;

  while (e)
    {
      struct groupnode *g = e->ex_groups;
      exports n = e->ex_next;

      while (g)
        {
          gsh_free(g->gr_name);
          g = g->gr_next;
        }
      gsh_free(e->ex_groups);
      gsh_free(e->ex_dir);
      gsh_free(e);

      e = n;
  }
}                               /* mnt_Export_Free */
