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
 * \file    nfs_mnt_list.c
 * \author  $Author: deniel $
 * \date    $Date: 2005/11/28 17:03:03 $
 * \version $Revision: 1.7 $
 * \brief   routines for managing the mount list.
 *
 * nfs_mnt_list.c : routines for managing the mount list.
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/nfs_mnt_list.c,v 1.7 2005/11/28 17:03:03 deniel Exp $
 *
 * $Log: nfs_mnt_list.c,v $
 * Revision 1.7  2005/11/28 17:03:03  deniel
 * Added CeCILL headers
 *
 * Revision 1.6  2005/11/24 13:44:29  deniel
 * ajout du remove dans ipname
 * track list in nfs_mnt_listc.
 *
 * Revision 1.5  2005/10/07 09:33:22  leibovic
 * Correcting bug in nfs_Add_MountList_Entry function
 * ( pnew_mnt_list_entry->ml_next not initialized ).
 *
 * Revision 1.4  2005/10/05 14:03:31  deniel
 * DEBUG ifdef are now much cleaner
 *
 * Revision 1.3  2005/08/11 12:37:28  deniel
 * Added statistics management
 *
 * Revision 1.2  2005/08/03 13:13:59  deniel
 * memset to zero before building the filehandles
 *
 * Revision 1.1  2005/08/03 06:57:54  deniel
 * Added a libsupport for miscellaneous service functions
 *
 * Revision 1.1  2005/07/20 11:49:47  deniel
 * Mount protocol V1/V3 support almost complete
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
#include <sys/types.h>
#include <ctype.h>              /* for having isalnum */
#include <stdlib.h>             /* for having atoi */
#include <dirent.h>             /* for having MAXNAMLEN */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>           /* for having FNDELAY */
#include <pwd.h>
#include <grp.h>
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
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"

/* The server's mount list (we use the native mount v3 structure) */
mountlist MNT_List_head = NULL;
mountlist MNT_List_tail = NULL;

/**
 *
 * nfs_Add_MountList_Entry: Adds a client to the mount list.
 *
 * Adds a client to the mount list.
 *
 * @param hostname [IN] the hostname for the client
 * @param dirpath [IN] the mounted path 
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs_Add_MountList_Entry(char *hostname, char *dirpath)
{
#ifndef _NO_MOUNT_LIST
  mountlist pnew_mnt_list_entry;
#endif

  /* Sanity check */
  if (hostname == NULL || dirpath == NULL)
    return 0;

#ifndef _NO_MOUNT_LIST

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("struct mountbody");
#endif
  /* Allocate the new entry */
  if ((pnew_mnt_list_entry = (mountlist) Mem_Alloc(sizeof(struct mountbody))) == NULL)
    return 0;

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  memset(pnew_mnt_list_entry, 0, sizeof(struct mountbody));

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("ml_hostname");
#endif
  if ((pnew_mnt_list_entry->ml_hostname = (char *)Mem_Alloc(MAXHOSTNAMELEN)) == NULL)
    {
      Mem_Free(pnew_mnt_list_entry);
      return 0;
    }
  memset(pnew_mnt_list_entry->ml_hostname, 0, MAXHOSTNAMELEN);

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("ml_directory");
#endif
  if ((pnew_mnt_list_entry->ml_directory = (char *)Mem_Alloc(MAXPATHLEN)) == NULL)
    {
      Mem_Free(pnew_mnt_list_entry->ml_hostname);
      Mem_Free(pnew_mnt_list_entry);
      return 0;
    }
  memset(pnew_mnt_list_entry->ml_directory, 0, MAXPATHLEN);

#ifdef _DEBUG_MEMLEAKS
  /* For debugging memory leaks */
  BuddySetDebugLabel("N/A");
#endif

  /* Copy the data */
  strncpy(pnew_mnt_list_entry->ml_hostname, hostname, MAXHOSTNAMELEN);
  strncpy(pnew_mnt_list_entry->ml_directory, dirpath, MAXPATHLEN);

  /* initialize next pointer */
  pnew_mnt_list_entry->ml_next = NULL;

  /* This should occur only for the first mount */
  if (MNT_List_head == NULL)
    {
      MNT_List_head = pnew_mnt_list_entry;
    }

  /* Append to the tail of the list */
  if (MNT_List_tail == NULL)
    MNT_List_tail = pnew_mnt_list_entry;
  else
    {
      MNT_List_tail->ml_next = pnew_mnt_list_entry;
      MNT_List_tail = pnew_mnt_list_entry;
    }

#ifdef _DEBUG_NFSPROTO
  nfs_Print_MountList();
#endif

#ifdef _DETECT_MEMCORRUPT
  if (!BuddyCheck(MNT_List_head) || !BuddyCheck(MNT_List_tail))
    {
      fprintf(stderr,
              "Memory corruption in nfs_Add_MountList_Entry. Head = %p, Tail = %p.\n",
              MNT_List_head, MNT_List_tail);
    }
  if (!BuddyCheck(pnew_mnt_list_entry->ml_hostname)
      || !BuddyCheck(pnew_mnt_list_entry->ml_directory))
    {
      fprintf(stderr,
              "Memory corruption in nfs_Add_MountList_Entry. Hostname = %p, Directory = %p.\n",
              pnew_mnt_list_entry->ml_hostname, pnew_mnt_list_entry->ml_directory);
    }
#endif

#endif

  return 1;
}                               /* nfs_Add_MountList_Entry */

/**
 *
 * nfs_Remove_MountList_Entry: Removes a client to the mount list.
 *
 * Removes a client to the mount list.
 *
 * @param hostname [IN] the hostname for the client
 * @param path [IN] the mounted path 
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs_Remove_MountList_Entry(char *hostname, char *dirpath)
{
#ifndef _NO_MOUNT_LIST
  mountlist piter_mnt_list_entry;
  mountlist piter_mnt_list_entry_prev;
  int found = 0;
#endif

  if (hostname == NULL)
    return 0;

#ifndef _NO_MOUNT_LIST

  piter_mnt_list_entry_prev = NULL;

  for (piter_mnt_list_entry = MNT_List_head;
       piter_mnt_list_entry != NULL; piter_mnt_list_entry = piter_mnt_list_entry->ml_next)
    {

#    ifdef _DETECT_MEMCORRUPT
      if (!BuddyCheck(piter_mnt_list_entry)
          || !BuddyCheck(piter_mnt_list_entry->ml_hostname)
          || !BuddyCheck(piter_mnt_list_entry->ml_directory))
        {
          fprintf(stderr,
                  "Memory corruption in nfs_Remove_MountList_Entry. Current = %p, Head = %p, Tail = %p.\n",
                  piter_mnt_list_entry, MNT_List_head, MNT_List_tail);
        }
#    endif

      /* BUGAZOMEU: pas de verif sur le path */
      if (!strncmp(piter_mnt_list_entry->ml_hostname, hostname, MAXHOSTNAMELEN)
          /*  && !strncmp( piter_mnt_list_entry->ml_directory, dirpath, MAXPATHLEN ) */ )
        {
          found = 1;
          break;
        }

      piter_mnt_list_entry_prev = piter_mnt_list_entry;

    }

  if (found)
    {
      /* remove head item ? */
      if (piter_mnt_list_entry_prev == NULL)
        MNT_List_head = MNT_List_head->ml_next;
      else
        piter_mnt_list_entry_prev->ml_next = piter_mnt_list_entry->ml_next;

      /* remove tail item ? */
      if (MNT_List_tail == piter_mnt_list_entry)
        MNT_List_tail = piter_mnt_list_entry_prev;

      Mem_Free(piter_mnt_list_entry->ml_hostname);
      Mem_Free(piter_mnt_list_entry->ml_directory);
      Mem_Free(piter_mnt_list_entry);

    }
#ifdef _DEBUG_NFSPROTO
  nfs_Print_MountList();
#endif

#endif

  return 1;
}                               /* nfs_Remove_MountList_Entry */

/**
 *
 * nfs_Purge_MountList: Purges the whole mount list.
 *
 * Purges the whole mount list.
 *
 * @param none (take no argument)
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs_Purge_MountList(void)
{
  mountlist piter_mnt_list_entry, piter_mnt_list_entry_next;

  piter_mnt_list_entry = MNT_List_head;
  piter_mnt_list_entry_next = MNT_List_head;

#ifndef _NO_MOUNT_LIST

  while (piter_mnt_list_entry_next != NULL)
    {
      piter_mnt_list_entry_next = piter_mnt_list_entry->ml_next;
      Mem_Free(piter_mnt_list_entry->ml_hostname);
      Mem_Free(piter_mnt_list_entry->ml_directory);
      Mem_Free(piter_mnt_list_entry);
      piter_mnt_list_entry = piter_mnt_list_entry_next;
    }

  MNT_List_head = NULL;
  MNT_List_tail = NULL;

#ifdef _DEBUG_NFSPROTO
  nfs_Print_MountList();
#endif

#endif

  return 1;
}                               /* nfs_Purge_MountList */

/**
 *
 * nfs_Init_MountList: Initializes the mount list.
 *
 * Initializes the mount list.
 *
 * @param none (take no argument)
 *
 * @return 1 if successful, 0 otherwise
 *
 */
int nfs_Init_MountList(void)
{
  MNT_List_head = NULL;
  MNT_List_tail = NULL;

#ifdef _DEBUG_NFSPROTO
  nfs_Print_MountList();
#endif

  return 1;
}                               /* nfs_Init_MountList */

/**
 *
 * nfs_Get_MountList: Returns the mount list.
 *
 * Returns the mount list.
 *
 * @param none (take no argument)
 *
 * @return the mount list (NULL if mount list is empty)
 *
 */
mountlist nfs_Get_MountList(void)
{
#ifdef _DEBUG_NFSPROTO
  nfs_Print_MountList();
#endif

  return MNT_List_head;
}                               /* nfs_Get_MountList */

/**
 *
 * nfs_Print_MountList: Prints the mount list (for debugging purpose).
 *
 * Prints the mount list (for debugging purpose).
 *
 * @param none (take no argument)
 *
 * @return nothing (void function)
 *
 */
void nfs_Print_MountList(void)
{
  mountlist piter_mnt_list_entry = NULL;

  if (MNT_List_head == NULL)
    DisplayLog("Mount List Entry is empty");

  for (piter_mnt_list_entry = MNT_List_head;
       piter_mnt_list_entry != NULL; piter_mnt_list_entry = piter_mnt_list_entry->ml_next)
    DisplayLog("Mount List Entry : ml_hostname=%s   ml_directory=%s",
               piter_mnt_list_entry->ml_hostname, piter_mnt_list_entry->ml_directory);

  return;
}                               /* nfs_Print_MountList */
