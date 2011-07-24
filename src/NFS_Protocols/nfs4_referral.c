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
 * \file    nfs4_referral.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/02/08 12:49:32 $
 * \version $Revision: 1.24 $
 * \brief   Routines used for managing NFSv4 referrals.
 *
 * nfs4_pseudo.c: Routines used for managing NFSv4 referrals.
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
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"
#include "cache_content.h"

int nfs4_Set_Fh_Referral(nfs_fh4 * pfh)
{
  file_handle_v4_t *pfhandle4;

  if(pfh == NULL)
    return 0;

  pfhandle4 = (file_handle_v4_t *) (pfh->nfs_fh4_val);

  pfhandle4->refid = 1;

  return 1;
}

int nfs4_referral_str_To_Fattr_fs_location(char *input_str, char *buff, u_int *plen)
{
  char str[MAXPATHLEN];
  char local_part[MAXPATHLEN];
  char *local_comp[MAXNAMLEN];
  char remote_part[MAXPATHLEN];
  char *remote_comp[MAXNAMLEN];
  char server_part[MAXPATHLEN];

  u_int nb_comp_local = 1;
  u_int nb_comp_remote = 1;
  u_int lastoff = 0;
  u_int tmp_int = 0;
  u_int i = 0;
  u_int delta_xdr = 0;

  char *ptr = NULL;

  if(!plen || !buff || !input_str)
    return 0;

  strncpy(str, input_str, MAXPATHLEN);

  /* Find the ":" in the string */
  for(ptr = str; *ptr != ':'; ptr++) ;
  *ptr = '\0';
  ptr += 1;

  memset( local_part, 0, MAXPATHLEN ) ;
  strncpy(local_part, str, MAXPATHLEN);

  memset( remote_part, 0, MAXPATHLEN ) ;
  strncpy(remote_part, ptr, MAXPATHLEN);

  /* Each part should not start with a leading slash */
  if(local_part[0] == '/')
    strncpy(local_part, str + 1, MAXPATHLEN);

  if(remote_part[0] == '/')
    strncpy(remote_part, ptr + 1, MAXPATHLEN);

  /* Find the "@" in the remote_part */
  for(ptr = remote_part; *ptr != '@'; ptr++) ;
  *ptr = '\0';
  ptr += 1;

  memset( server_part, 0 , MAXPATHLEN ) ;
  strncpy(server_part, ptr, MAXPATHLEN);

  local_comp[0] = local_part;
  for(ptr = local_part; *ptr != '\0'; ptr++)
    if(*ptr == '/')
      {
        local_comp[nb_comp_local] = ptr + 1;
        nb_comp_local += 1;
      }
  for(tmp_int = 0; tmp_int < nb_comp_local; tmp_int++)
    {
      ptr = local_comp[tmp_int] - 1;
      *ptr = '\0';
    }

  remote_comp[0] = remote_part;
  for(ptr = remote_part; *ptr != '\0'; ptr++)
    if(*ptr == '/')
      {
        remote_comp[nb_comp_remote] = ptr + 1;
        nb_comp_remote += 1;
      }
  for(tmp_int = 0; tmp_int < nb_comp_remote; tmp_int++)
    {
      ptr = remote_comp[tmp_int] - 1;
      *ptr = '\0';
    }

  /* This attributes is equivalent to a "mount" command line,
   * To understand what's follow, imagine that you do kind of "mount refer@server nfs_ref" */

  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "--> %s", input_str);

  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   %u comp local", nb_comp_local);
  for(tmp_int = 0; tmp_int < nb_comp_local; tmp_int++)
    LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "     #%s#", local_comp[tmp_int]);

  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   %u comp remote", nb_comp_remote);
  for(tmp_int = 0; tmp_int < nb_comp_remote; tmp_int++)
    LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "     #%s#", remote_comp[tmp_int]);

  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   server = #%s#", server_part);

  /* 1- Number of component in local path */
  tmp_int = htonl(nb_comp_local);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 2- each component in local path */
  for(i = 0; i < nb_comp_local; i++)
    {
      /* The length for the string */
      tmp_int = htonl(strlen(local_comp[i]));
      memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
      lastoff += sizeof(u_int);

      /* the string itself */
      memcpy((char *)(buff + lastoff), local_comp[i], strlen(local_comp[i]));
      lastoff += strlen(local_comp[i]);

      /* The XDR padding  : strings must be aligned to 32bits fields */
      if((strlen(local_comp[i]) % 4) == 0)
        delta_xdr = 0;
      else
        {
          delta_xdr = 4 - (strlen(local_comp[i]) % 4);
          memset((char *)(buff + lastoff), 0, delta_xdr);
          lastoff += delta_xdr;
        }
    }

  /* 3- there is only one fs_location in the fs_locations array */
  tmp_int = htonl(1);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 4- Only ine server in fs_location entry */
  tmp_int = htonl(1);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 5- the len for the server's adress */
  tmp_int = htonl(strlen(server_part));
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 6- the server's string */
  memcpy((char *)(buff + lastoff), server_part, strlen(server_part));
  lastoff += strlen(server_part);

  /* 7- XDR padding for server's string */
  if((strlen(server_part) % 4) == 0)
    delta_xdr = 0;
  else
    {
      delta_xdr = 4 - (strlen(server_part) % 4);
      memset((char *)(buff + lastoff), 0, delta_xdr);
      lastoff += delta_xdr;
    }

  /* 8- Number of component in remote path */
  tmp_int = htonl(nb_comp_remote);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 9- each component in local path */
  for(i = 0; i < nb_comp_remote; i++)
    {
      /* The length for the string */
      tmp_int = htonl(strlen(remote_comp[i]));
      memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
      lastoff += sizeof(u_int);

      /* the string itself */
      memcpy((char *)(buff + lastoff), remote_comp[i], strlen(remote_comp[i]));
      lastoff += strlen(remote_comp[i]);

      /* The XDR padding  : strings must be aligned to 32bits fields */
      if((strlen(remote_comp[i]) % 4) == 0)
        delta_xdr = 0;
      else
        {
          delta_xdr = 4 - (strlen(remote_comp[i]) % 4);
          memset((char *)(buff + lastoff), 0, delta_xdr);
          lastoff += delta_xdr;
        }
    }

  /* Set the len then return */
  *plen = lastoff;

  return 1;
}                               /* nfs4_referral_str_To_Fattr_fs_location */
