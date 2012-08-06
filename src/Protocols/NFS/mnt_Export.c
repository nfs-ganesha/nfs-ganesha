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
               fsal_op_context_t *pcontext,
               nfs_worker_data_t *pworker,
               struct svc_req *preq,
               nfs_res_t * pres)
{

  exportlist_t *p_current_item;       /* the current export item. */
  struct glist_head * glist;

  exports p_exp_out = NULL;     /* Pointer to the first export entry. */
  exports p_exp_current = NULL; /* Pointer to the last  export entry. */

  unsigned int i = 0;

  LogDebug(COMPONENT_NFSPROTO, "REQUEST PROCESSING: Calling mnt_Export");

  /* paranoid command, to avoid parasites in the result structure. */
  memset(pres, 0, sizeof(nfs_res_t));

  /* for each existing export entry */
  glist_for_each(glist, nfs_param.pexportlist)
    {
      p_current_item = glist_entry(glist, exportlist_t, exp_list);

      exports new_expnode;      /* the export node to be added to the list */

      new_expnode = gsh_calloc(1,sizeof(exportnode));

      /* ---- ex_dir ------ */

      /* we set the export path */

      LogFullDebug(COMPONENT_NFSPROTO,
                   "Export entry: %s, Numclients: %d",
                   p_current_item->fullpath, p_current_item->clients.num_clients);

      new_expnode->ex_dir = gsh_strdup(p_current_item->fullpath);

      /* ---- ex_groups ---- */

      /* we convert the group list */

      if(p_current_item->clients.num_clients > 0)
        {
          struct glist_head * glist_cl;

          /* Alias, to make the code slim... */
          exportlist_client_t *p_clients = &(p_current_item->clients);

          /* allocates the memory for all the groups, once for all */
          new_expnode->ex_groups =
               gsh_calloc(p_clients->num_clients, sizeof(groupnode));

          i = 0;

          glist_for_each(glist_cl, &p_current_item->clients.client_list)
            {
              exportlist_client_entry_t * p_client_entry;
              p_client_entry = glist_entry(glist_cl, exportlist_client_entry_t, cle_list);

              LogClientListEntry(COMPONENT_NFSPROTO, p_client_entry);

              /* ---- gr_next ----- */

              if((i + 1) == p_clients->num_clients)     /* this is the last item */
                new_expnode->ex_groups[i].gr_next = NULL;
              else              /* other items point to the next memory slot */
                new_expnode->ex_groups[i].gr_next = &(new_expnode->ex_groups[i + 1]);

              /* ---- gr_name ----- */

              switch (p_client_entry->type)
                {
                case HOSTIF_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  new_expnode->ex_groups[i].gr_name
                       = gsh_calloc(1, INET_ADDRSTRLEN + 1);

                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }

                  if(inet_ntop
                     (AF_INET, &(p_client_entry->client.hostif.clientaddr),
                      new_expnode->ex_groups[i].gr_name, INET_ADDRSTRLEN) == NULL)
                    {
                      strncpy(new_expnode->ex_groups[i].gr_name, "Invalid Host address",
                              INET_ADDRSTRLEN);
                    }

                  LogFullDebug(COMPONENT_NFSPROTO,
                               "%p HOSTIF_CLIENT=%s",
                               p_client_entry, new_expnode->ex_groups[i].gr_name);

                  break;

                case NETWORK_CLIENT:

                  /* allocates target buffer (+1 for security ) */
                  new_expnode->ex_groups[i].gr_name
                       = gsh_calloc(1, INET_ADDRSTRLEN + 1);

                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }

                  if(inet_ntop
                     (AF_INET, &(p_client_entry->client.network.netaddr),
                      new_expnode->ex_groups[i].gr_name, INET_ADDRSTRLEN) == NULL)
                    {
                      strncpy(new_expnode->ex_groups[i].gr_name,
                              "Invalid Network address", MAXHOSTNAMELEN);
                    }
                  break;

                case NETGROUP_CLIENT:

                  /* allocates target buffer */

                  new_expnode->ex_groups[i].gr_name
                       = gsh_strdup(p_client_entry->client.netgroup.netgroupname);

                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }
                  break;

                case WILDCARDHOST_CLIENT:

                  /* allocates target buffer */
                  new_expnode->ex_groups[i].gr_name
                       = gsh_strdup(p_client_entry->client.wildcard.wildcard);

                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }
                  break;

                case GSSPRINCIPAL_CLIENT:

                  new_expnode->ex_groups[i].gr_name
                       = gsh_strdup(p_client_entry->client.gssprinc.princname);


                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }
                  break;

                default:

                  /* @todo : free allocated resources */

                  LogCrit(COMPONENT_NFSPROTO,
                          "MNT_EXPORT: Unknown export entry type: %d",
                          p_client_entry->type);

                  new_expnode->ex_groups[i].gr_name = gsh_strdup("<unknown>");

                  if(new_expnode->ex_groups[i].gr_name == NULL)
                    {
                      LogCrit(COMPONENT_NFSPROTO,
                              "Could not allocate memory for response");
                      break;
                    }

                  break;
                }

              i++;
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
          if(g->gr_name)
            gsh_free(g->gr_name);
          g = g->gr_next;
        }
      gsh_free(e->ex_groups);
      gsh_free(e->ex_dir);
      gsh_free(e);

      e = n;
  }
}                               /* mnt_Export_Free */
