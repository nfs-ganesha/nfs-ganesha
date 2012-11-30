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
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "cache_inode.h"

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
  char   str[MAXPATHLEN];
  char * remote_part;
  char * server_part;
  char * local_part = str;
  char * ptr = NULL;

  u_int nb_comp_local = 1;
  u_int nb_comp_remote = 1;
  u_int lastoff = 0;
  u_int tmp_int = 0;
  u_int i = 0;
  u_int delta_xdr = 0;
  u_int len;

  if(!plen || !buff || !input_str)
    return 0;

  if(strmaxcpy(str, input_str, sizeof(str)) == -1)
    return 0;

  /* Find the ":" in the string */
  for(remote_part = str; *remote_part != ':'; remote_part++) ;
  *remote_part = '\0';
  remote_part += 1;

  /* Each part should not start with a leading slash */
  if(local_part[0] == '/')
    local_part++;
  
  if(remote_part[0] == '/')
    remote_part++;

  /* Find the "@" in the remote_part */
  for(server_part = remote_part; *server_part != '@'; server_part++) ;
  *server_part = '\0';
  server_part += 1;

  /* Find each component of the local part */
  for(ptr = local_part; *ptr != '\0'; ptr++)
    if(*ptr == '/')
      {
        /* null terminate the previous component */
        *ptr = '\0';
        nb_comp_local += 1;
      }

  /* Find each component of the remote part */
  for(ptr = remote_part; *ptr != '\0'; ptr++)
    if(*ptr == '/')
      {
        /* null terminate the previous component */
        *ptr = '\0';
        nb_comp_remote += 1;
      }

  /* This attributes is equivalent to a "mount" command line,
   * To understand what's follow, imagine that you do kind of "mount refer@server nfs_ref" */

  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "--> %s", input_str);

  /* 1- Number of component in local path */
  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   %u comp local", nb_comp_local);
  tmp_int = htonl(nb_comp_local);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 2- each component in local path */
  ptr = local_part;

  for(i = 0; i < nb_comp_local; i++)
    {
      LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "     \"%s\"", ptr);

      /* The length for the string */
      len = strlen(ptr);
      tmp_int = htonl(len);
      memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
      lastoff += sizeof(u_int);

      /* the string itself */
      memcpy((char *)(buff + lastoff), ptr, len);
      lastoff += len;

      /* The XDR padding  : strings must be aligned to 32bits fields */
      if((len % 4) == 0)
        delta_xdr = 0;
      else
        {
          delta_xdr = 4 - (len % 4);
          memset((char *)(buff + lastoff), 0, delta_xdr);
          lastoff += delta_xdr;
        }

      ptr += len + 1;
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
  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   server = \"%s\"", server_part);

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
  LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "   %u comp remote", nb_comp_remote);

  tmp_int = htonl(nb_comp_remote);
  memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
  lastoff += sizeof(u_int);

  /* 9- each component in remote path */
  for(i = 0; i < nb_comp_remote; i++)
    {
      LogFullDebug(COMPONENT_NFS_V4_REFERRAL, "     \"%s\"", ptr);

      /* The length for the string */
      len = strlen(ptr);
      tmp_int = htonl(len);
      memcpy((char *)(buff + lastoff), &tmp_int, sizeof(u_int));
      lastoff += sizeof(u_int);

      /* the string itself */
      memcpy((char *)(buff + lastoff), ptr, len);
      lastoff += len;

      /* The XDR padding  : strings must be aligned to 32bits fields */
      if((len % 4) == 0)
        delta_xdr = 0;
      else
        {
          delta_xdr = 4 - (len % 4);
          memset((char *)(buff + lastoff), 0, delta_xdr);
          lastoff += delta_xdr;
        }

      ptr += len + 1;
    }

  /* Set the len then return */
  *plen = lastoff;

  return 1;
}                               /* nfs4_referral_str_To_Fattr_fs_location */
