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
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/socket.h>		/* For getsockname */
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

  while (p_expnode)
    {

      groups p_group;

      printf("exportnode.ex_dir = \"%s\"\n", p_expnode->ex_dir);
      printf("exportnode.ex_groups = {\n");

      p_group = p_expnode->ex_groups;
      while (p_group)
	{
	  printf("  \"%s\"\n", p_group->gr_name);
	  p_group = p_group->gr_next;
	}
      printf("}\n\n");

      p_expnode = p_expnode->ex_next;
    }

}

/* Test MNTPROC_NULL */
int test_mnt_Null()
{

  int rc;

  rc = mnt_Null(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
  printf("MNTPROC_NULL()=%d\n", rc);

  /* Must return MNT3_OK */
  if (rc == MNT3_OK)
    {
      printf("TEST MNT_NULL : OK\n");
      return 0;
    } else
    {
      printf("TEST MNT_NULL : ERROR\n");
      return rc;
    }
}

# define NB_EXPORT_ENTRIES 5
int test_mnt_Export()
{

  int rc, i;
  int error = 0;
  struct addrinfo *p_tab;
  exportlist_t export_entries[NB_EXPORT_ENTRIES];
  nfs_res_t result;
  int mysock;
  unsigned long size;
  struct sockaddr_in in_addr;
  struct sockaddr_in addr;

  /* TEST 1 : using a NULL export_list */

  rc = mnt_Export(NULL, NULL, NULL, NULL, NULL, NULL, &result);
  /* rc must be OK and result.res_mntexport must be NULL */
  printf("MNTPROC_EXPORT(NULL)=(%d,%p)\n", rc, result.res_mntexport);

  if ((rc == MNT3_OK) && (result.res_mntexport == NULL))
    {
      printf("TEST MNT_EXPORT : OK\n\n");
    } else
    {
      printf("TEST MNT_EXPORT : ERROR\n\n");
      error++;
    }

  /* TEST 2 : MNT_EXPORT complex */

  if ((mysock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
      printf("socket ERROR %d : %s\n", errno, strerror(errno));
    }

  in_addr.sin_family = AF_INET;
  in_addr.sin_addr.s_addr = INADDR_ANY;
  in_addr.sin_port = htons(5100);

  if (bind(mysock, (struct sockaddr *)&in_addr, sizeof(in_addr)) == -1)
    {
      printf("bind ERROR %d : %s\n", errno, strerror(errno));
    }

  size = sizeof(struct sockaddr);
  if (getsockname(mysock, (struct sockaddr *)&addr, (socklen_t *) & size) == -1)
    {
      printf("getsockname ERROR %d : %s\n", errno, strerror(errno));
    }
  /* we don't use the resource, only its adress. */
  close(mysock);

  /* Building an export list */

  for (i = 0; i < NB_EXPORT_ENTRIES; i++)
    {

      /* pour alleger les notations */
      exportlist_client_entry_t p_cli[5];

      /* setting paths */
      snprintf(export_entries[i].dirname, MAXPATHLEN, "/dirname-%d", i);
      snprintf(export_entries[i].fsname, MAXPATHLEN, "/fsname-%d", i);
      snprintf(export_entries[i].pseudopath, MAXPATHLEN, "/pseudopath-%d", i);
      snprintf(export_entries[i].fullpath, MAXPATHLEN, "/fullpath-%d", i);

      /* linking to the next element. */
      if ((i + 1) < NB_EXPORT_ENTRIES)
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
	  printf("!!!!!***** TEST ERROR *****!!!!!\n");
	  return -1;
	}

    }

  rc = mnt_Export(NULL, export_entries, NULL, NULL, NULL, NULL, &result);
  /* rc must be OK and result.res_mntexport must be NULL */
  printf("MNTPROC_EXPORT(entries)=(%d,%p)\n", rc, result.res_mntexport);

  if ((rc == MNT3_OK) && (result.res_mntexport != NULL))
    {
      printf("TEST MNT_EXPORT : OK\n\n");
    } else
    {
      printf("TEST MNT_EXPORT : ERROR\n\n");
      error++;
    }

  /* printing the export_list */
  print_export_list(result.res_mntexport);

  return (error);

}

#define Maketest(func,name) do {                      \
  int rc;                                             \
  printf("\n======== TEST %s =========\n\n",name);    \
  rc = func();                                        \
  if (rc)                                             \
    printf("\n-------- %s : %d ---------\n",name,rc); \
  else                                                \
    printf("\n-------- %s : OK ---------\n",name); \
  } while (0)

int main(int argc, char **argv)
{

  SetNameFileLog("/dev/tty");
  SetNamePgm("test_mnt_proto");

/*    InitDebug( NIV_FULL_DEBUG ) ;*/
  InitDebug(NIV_DEBUG);

  Maketest(test_mnt_Null, "test_mnt_Null");
  Maketest(test_mnt_Export, "test_mnt_Export");

  exit(0);

}
