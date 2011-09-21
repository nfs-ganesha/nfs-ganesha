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
 * \file    test_mnt_proto.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/12/20 10:52:15 $
 * \version $Revision: 1.8 $
 * \brief   Tests for testing the mount protocol routines.
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
#include <sys/socket.h>         /* For getsockname */
#include "HashData.h"
#include "HashTable.h"
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"

/* prints an export list */
void print_export_list(exports export_list)
{

  exports p_expnode = export_list;

  while(p_expnode)
    {

      groups p_group;

      LogTest("exportnode.ex_dir = \"%s\"", p_expnode->ex_dir);
      LogTest("exportnode.ex_groups = {");

      p_group = p_expnode->ex_groups;
      while(p_group)
        {
          LogTest("  \"%s\"", p_group->gr_name);
          p_group = p_group->gr_next;
        }
      LogTest("}");

      p_expnode = p_expnode->ex_next;
    }

}

/* Test MNTPROC_NULL */
int test_mnt_Null()
{

  int rc;

  rc = mnt_Null(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  LogTest("MNTPROC_NULL()=%d", rc);

  /* Must return MNT3_OK */
  if(rc == MNT3_OK)
    {
      LogTest("TEST MNT_NULL : OK");
      return 0;
    }
  else
    {
      LogTest("TEST MNT_NULL : ERROR");
      return rc;
    }
}

# define NB_EXPORT_ENTRIES 5
int test_mnt_Export()
{

  int rc, i;
  int error = 0;
  exportlist_t export_entries[NB_EXPORT_ENTRIES];
  nfs_res_t result;
  int mysock;
  unsigned long size;
  struct sockaddr_in in_addr;
  struct sockaddr_in addr;

  /* TEST 1 : using a NULL export_list */

  rc = mnt_Export(NULL, NULL, NULL, NULL, NULL, NULL, &result);
  /* rc must be OK and result.res_mntexport must be NULL */
  LogTest("MNTPROC_EXPORT(NULL)=(%d,%p)", rc, result.res_mntexport);

  if((rc == MNT3_OK) && (result.res_mntexport == NULL))
    {
      LogTest("TEST MNT_EXPORT : OK");
    }
  else
    {
      LogTest("TEST MNT_EXPORT : ERROR");
      error++;
    }

  /* TEST 2 : MNT_EXPORT complex */

  if((mysock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
      LogTest("socket ERROR %d : %s", errno, strerror(errno));
    }

  in_addr.sin_family = AF_INET;
  in_addr.sin_addr.s_addr = INADDR_ANY;
  in_addr.sin_port = htons(5100);

  if(bind(mysock, (struct sockaddr *)&in_addr, sizeof(in_addr)) == -1)
    {
      LogTest("bind ERROR %d : %s", errno, strerror(errno));
    }

  size = sizeof(struct sockaddr);
  if(getsockname(mysock, (struct sockaddr *)&addr, (socklen_t *) & size) == -1)
    {
      LogTest("getsockname ERROR %d : %s", errno, strerror(errno));
    }
  /* we don't use the resource, only its adress. */
  close(mysock);

  /* Building an export list */

  for(i = 0; i < NB_EXPORT_ENTRIES; i++)
    {

      /* pour alleger les notations */
      exportlist_client_entry_t p_cli[5];

      /* setting paths */
      snprintf(export_entries[i].dirname, sizeof(export_entries[i].dirname), "/dirname-%d", i);
      snprintf(export_entries[i].fsname, sizeof(export_entries[i].fsname), "/fsname-%d", i);
      snprintf(export_entries[i].pseudopath, MAXPATHLEN, "/pseudopath-%d", i);
      snprintf(export_entries[i].fullpath, MAXPATHLEN, "/fullpath-%d", i);

      /* linking to the next element. */
      if((i + 1) < NB_EXPORT_ENTRIES)
        export_entries[i].next = &(export_entries[i + 1]);
      else
        export_entries[i].next = NULL;

      /* tests several clients list type */
      switch (i % 4)
        {

        case 0:
          /* empty list */
          export_entries[i].clients.num_clients = 0;
          break;

        case 1:

          /* one element list */
          export_entries[i].clients.num_clients = 1;
          p_cli[0].type = HOSTIF_CLIENT;
          p_cli[0].client.hostif.clientaddr = addr.sin_addr.s_addr;
          break;

        case 2:

          /* two elements list */
          export_entries[i].clients.num_clients = 2;

          p_cli[0].type = HOSTIF_CLIENT;
          p_cli[0].client.hostif.clientaddr = addr.sin_addr.s_addr;

          p_cli[1].type = NETGROUP_CLIENT;
          strcpy(p_cli[1].client.netgroup.netgroupname, "netgroup");

          break;

        case 3:
          /* several elements list */

          export_entries[i].clients.num_clients = 5;

          p_cli[0].type = HOSTIF_CLIENT;
          p_cli[0].client.hostif.clientaddr = addr.sin_addr.s_addr;

          p_cli[1].type = NETGROUP_CLIENT;
          strcpy(p_cli[1].client.netgroup.netgroupname, "netgroup");

          p_cli[2].type = WILDCARDHOST_CLIENT;
          strcpy(p_cli[2].client.wildcard.wildcard, "wilcard");

          p_cli[3].type = GSSPRINCIPAL_CLIENT;
          strcpy(p_cli[3].client.gssprinc.princname, "gssprincipal");

          p_cli[4].type = NETWORK_CLIENT;
          p_cli[4].client.network.netaddr = addr.sin_addr.s_addr;
          p_cli[4].client.network.netmask = 0xFFFFFF00;

          break;
        default:
          LogTest("!!!!!***** TEST ERROR *****!!!!!");
          return -1;
        }

    }

  rc = mnt_Export(NULL, export_entries, NULL, NULL, NULL, NULL, &result);
  /* rc must be OK and result.res_mntexport must be NULL */
  LogTest("MNTPROC_EXPORT(entries)=(%d,%p)", rc, result.res_mntexport);

  if((rc == MNT3_OK) && (result.res_mntexport != NULL))
    {
      LogTest("TEST MNT_EXPORT : OK");
    }
  else
    {
      LogTest("TEST MNT_EXPORT : ERROR");
      error++;
    }

  /* printing the export_list */
  print_export_list(result.res_mntexport);

  return (error);

}

#define Maketest(func,name) do {                      \
  int rc;                                             \
  LogTest("\n======== TEST %s =========",name);    \
  rc = func();                                        \
  if (rc)                                             \
    LogTest("\n-------- %s : %d ---------",name,rc); \
  else                                                \
    LogTest("\n-------- %s : OK ---------",name); \
  } while (0)

int main(int argc, char **argv)
{
  
  SetDefaultLogging("TEST");
  SetNamePgm("test_mnt_proto");

  Maketest(test_mnt_Null, "test_mnt_Null");
  Maketest(test_mnt_Export, "test_mnt_Export");

  exit(0);

}
