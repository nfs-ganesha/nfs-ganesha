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
 * \file    exports.c
 * \author  $Author$
 * \date    $Date: 2006/02/08 12:50:40 $
 * \version $Revision: 1.33 $
 * \brief   What is needed to parse the exports file.
 *
 * exports.c : What is needed to parse the exports file.
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/support/exports.c,v 1.33 2006/02/08 12:50:40 leibovic Exp $
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#define USHRT_MAX       6553
#endif

#include "cidr.h"
#include "rpc.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_tools.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"
#include "common_utils.h"
#include "nodelist.h"
#include <stdlib.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <ctype.h>

/* Structures to manage a client to cache inode located in the 'main' thread
 * this cache_inode_client will be used to handle the root of each entry (created when reading export file) */
cache_inode_client_t small_client;
cache_inode_client_parameter_t small_client_param;
cache_content_client_t recover_datacache_client;

#define STRCMP strcasecmp

#define CONF_LABEL_EXPORT "EXPORT"

/* Labels in the export file */
#define CONF_EXPORT_ID                 "Export_id"
#define CONF_EXPORT_PATH               "Path"
#define CONF_EXPORT_ROOT               "Root_Access"
#define CONF_EXPORT_ACCESS             "Access"
#define CONF_EXPORT_READ_ACCESS        "R_Access"
#define CONF_EXPORT_READWRITE_ACCESS   "RW_Access"
#define CONF_EXPORT_MD_ACCESS          "MDONLY_Access"
#define CONF_EXPORT_MD_RO_ACCESS       "MDONLY_RO_Access"
#define CONF_EXPORT_PSEUDO             "Pseudo"
#define CONF_EXPORT_ACCESSTYPE         "Access_Type"
#define CONF_EXPORT_ANON_USER          "Anonymous_uid"
#define CONF_EXPORT_ANON_ROOT          "Anonymous_root_uid"
#define CONF_EXPORT_ALL_ANON           "Make_All_Users_Anonymous"
#define CONF_EXPORT_ANON_GROUP         "Anonymous_gid"
#define CONF_EXPORT_NFS_PROTO          "NFS_Protocols"
#define CONF_EXPORT_TRANS_PROTO        "Transport_Protocols"
#define CONF_EXPORT_SECTYPE            "SecType"
#define CONF_EXPORT_MAX_READ           "MaxRead"
#define CONF_EXPORT_MAX_WRITE          "MaxWrite"
#define CONF_EXPORT_PREF_READ          "PrefRead"
#define CONF_EXPORT_PREF_WRITE         "PrefWrite"
#define CONF_EXPORT_PREF_READDIR       "PrefReaddir"
#define CONF_EXPORT_FSID               "Filesystem_id"
#define CONF_EXPORT_NOSUID             "NOSUID"
#define CONF_EXPORT_NOSGID             "NOSGID"
#define CONF_EXPORT_PRIVILEGED_PORT    "PrivilegedPort"
#define CONF_EXPORT_USE_DATACACHE      "Cache_Data"
#define CONF_EXPORT_FS_SPECIFIC        "FS_Specific"
#define CONF_EXPORT_FS_TAG             "Tag"
#define CONF_EXPORT_CACHE_POLICY       "Cache_Inode_Policy"
#define CONF_EXPORT_MAX_OFF_WRITE      "MaxOffsetWrite"
#define CONF_EXPORT_MAX_OFF_READ       "MaxOffsetRead"
#define CONF_EXPORT_MAX_CACHE_SIZE     "MaxCacheSize"
#define CONF_EXPORT_REFERRAL           "Referral"
#define CONF_EXPORT_FSALID             "FSALID"
#define CONF_EXPORT_PNFS               "Use_pNFS"
#define CONF_EXPORT_USE_COMMIT                  "Use_NFS_Commit"
#define CONF_EXPORT_USE_GANESHA_WRITE_BUFFER    "Use_Ganesha_Write_Buffer"
#define CONF_EXPORT_USE_FSAL_UP        "Use_FSAL_UP"
#define CONF_EXPORT_FSAL_UP_FILTERS    "FSAL_UP_Filters"
#define CONF_EXPORT_FSAL_UP_TIMEOUT    "FSAL_UP_Timeout"
#define CONF_EXPORT_FSAL_UP_TYPE       "FSAL_UP_Type"

/** @todo : add encrypt handles option */

/* Internal identifiers */
#define FLAG_EXPORT_ID            0x00000001
#define FLAG_EXPORT_PATH          0x00000002

#define FLAG_EXPORT_ROOT_OR_ACCESS 0x00000004

#define FLAG_EXPORT_PSEUDO          0x00000010
#define FLAG_EXPORT_ACCESSTYPE      0x00000020
#define FLAG_EXPORT_ANON_ROOT       0x00000040
#define FLAG_EXPORT_NFS_PROTO       0x00000080
#define FLAG_EXPORT_TRANS_PROTO     0x00000100
#define FLAG_EXPORT_SECTYPE         0x00000200
#define FLAG_EXPORT_MAX_READ        0x00000400
#define FLAG_EXPORT_MAX_WRITE       0x00000800
#define FLAG_EXPORT_PREF_READ       0x00001000
#define FLAG_EXPORT_PREF_WRITE      0x00002000
#define FLAG_EXPORT_PREF_READDIR    0x00004000
#define FLAG_EXPORT_FSID            0x00008000
#define FLAG_EXPORT_NOSUID          0x00010000
#define FLAG_EXPORT_NOSGID          0x00020000
#define FLAG_EXPORT_PRIVILEGED_PORT 0x00040000
#define FLAG_EXPORT_USE_DATACACHE   0x00080000
#define FLAG_EXPORT_FS_SPECIFIC     0x00100000
#define FLAG_EXPORT_FS_TAG          0x00200000
#define FLAG_EXPORT_MAX_OFF_WRITE   0x00400000
#define FLAG_EXPORT_MAX_OFF_READ    0x00800000
#define FLAG_EXPORT_MAX_CACHE_SIZE  0x01000000
#define FLAG_EXPORT_USE_PNFS        0x02000000
#define FLAG_EXPORT_ACCESS_LIST     0x04000000
#define FLAG_EXPORT_ACCESSTYPE_LIST 0x08000000
#define FLAG_EXPORT_ANON_GROUP      0x10000000
#define FLAG_EXPORT_ALL_ANON        0x20000000
#define FLAG_EXPORT_ANON_USER       0x40000000
#define FLAG_EXPORT_CACHE_POLICY    0x80000000

/* limites for nfs_ParseConfLine */
/* Used in BuildExportEntry() */
#define EXPORT_MAX_CLIENTS   EXPORTS_NB_MAX_CLIENTS     /* number of clients */
#define EXPORT_MAX_CLIENTLEN 256        /* client name len */

static int local_lru_inode_entry_to_str(LRU_data_t data, char *str)
{
  return sprintf(str, "N/A ");
}                               /* local_lru_inode_entry_to_str */

static int local_lru_inode_clean_entry(LRU_entry_t * entry, void *adddata)
{
  return 0;
}                               /* lru_clean_entry */

/**
 * nfs_ParseConfLine: parse a line with a settable separator and  end of line
 *
 * parse a line with a settable separator and  end of line .
 *
 * @param Argv               [OUT] result array
 * @param nbArgv             [IN]  allocated number of entries in the Argv
 * @param line               [IN]  input line
 * @param separator_function [IN]  function used to identify a separator
 * @param endLine_func       [IN]  function used to identify an end of line
 *
 * @return the number of object found
 *
 */
int nfs_ParseConfLine(char *Argv[],
                      int nbArgv,
                      char *line,
                      int (*separator_function) (char), int (*endLine_func) (char))
{
  int output_value = 0;
  int endLine = FALSE;

  char *p1 = line;              /* Pointeur sur le debut du token */
  char *p2 = NULL;              /* Pointeur sur la fin du token   */

  /* iteration and checking for array bounds */
  for(; output_value < nbArgv;)
    {

      if(*p1 == '\0')
        return output_value;

      /* Je recherche le premier caractere valide */
      for(; *p1 == ' ' || *p1 == '\t'; p1++) ;

      /* p1 pointe sur un debut de token, je cherche la fin */
      /* La fin est un blanc, une fin de chaine ou un CR    */
      for(p2 = p1; !separator_function(*p2) && !endLine_func(*p2); p2++) ;

      /* Possible arret a cet endroit */
      if(endLine_func(*p2))
        endLine = TRUE;

      /* je valide la lecture du token */
      *p2 = '\0';
      strcpy(Argv[output_value++], p1);

      /* Je me prepare pour la suite */
      if(!endLine)
        {
          p2 += 1;
          p1 = p2;
        }
      else
        return output_value;

    }                           /* for( ; ; ) */

  /* out of bounds */
  if(output_value >= nbArgv)
    return -1;

  return -2;

}                               /* nfs_ParseConfLine */

/**
 *
 * nfs_LookupHostAddr: determine host address from string.
 *
 * This routine is converting a valid host name is both literal or dotted
 *  format into a valid netdb structure. If it could not successfull, NULL is
 *  returned by the function.
 *
 * Assumptions:
 *  Dotted host address are 4 hex, decimal, or octal numbers in
 *  base 256 each separated by a period
 *
 * @param host [IN] hostname or dotted address, within a string literal.
 *
 * @return the netdb structure related to this client.
 *
 * @see inet_addr
 * @see gethostbyname
 * @see gethostbyaddr
 *
 */
#if 0
static struct hostent *nfs_LookupHostAddr(char *host)
{
  struct hostent *output;
  unsigned long hostaddr;
  int length = sizeof(hostaddr);

#ifdef _USE_TIRPC_IPV6
  struct sockaddr_storage addrv6;
  struct sockaddr_in6 *paddrv6 = (struct sockaddr_in6 *)&addrv6;
#endif

  hostaddr = inet_addr(host);
  /* is it a dotted IP? */
  if (hostaddr == INADDR_NONE)
  {
     /* Nope. Try to resolve name */
     output = gethostbyname(host);
  }
  else
  {
      /* yes, this is an IP, try to get hostent */
      output = gethostbyaddr((char *)&hostaddr, length, AF_INET);
  }

#ifdef _USE_TIRPC_IPV6
  /* if output == NULL it may be an IPv6 address */
  if(output == NULL)
    {
      if((output = gethostbyname2(host, AF_INET6)) == NULL)
        {
          /* Maybe an address in the ASCII format */
          if(inet_pton(AF_INET6, host, paddrv6->sin6_addr.s6_addr))
            {
              output = gethostbyaddr(paddrv6->sin6_addr.s6_addr,
                                     sizeof(paddrv6->sin6_addr.s6_addr), AF_INET6);
            }
        }
    }
#endif

  return output;
}                               /* nfs_LookupHostAddr */
#endif

/**
 *
 * nfs_LookupNetworkAddr: determine network address from string.
 *
 * This routine is converting a valid host name is both literal or dotted
 *  format into a valid netdb structure. If it could not successfull, NULL is
 *  returned by the function.
 *
 * Assumptions:
 *  Dotted host address are 4 hex, decimal, or octal numbers in
 *  base 256 each separated by a period
 *
 * @param host [IN] hostname or dotted address, within a string literal.
 * @param netAddr [OUT] return address
 * @param netMask [OUT] return address mask
 *
 * @return 0 if successfull, other values show an error
 *
 * @see inet_addr
 * @see gethostbyname
 * @see gethostbyaddr
 *
 */

inline static int string_contains_slash( char* host )
{
  char * c ;

  for( c = host ; *c != '\0' ; c++ ) 
    if( *c == '/' ) 
      return 1 ;

  return 0 ;
}

int nfs_LookupNetworkAddr(char *host,   /* [IN] host/address specifier */
                          unsigned long *netAddr,       /* [OUT] return address       */
                          unsigned long *netMask)       /* [OUT] return address mask  */
{
  CIDR * pcidr = NULL ;
  unsigned int i = 0 ;

  if( ( pcidr = cidr_from_str( host ) ) == NULL )
    return 1 ;

  memcpy( netAddr, pcidr->addr, sizeof( unsigned long ) ) ;
  memcpy( netMask, pcidr->mask, sizeof( unsigned long ) ) ; 
  
  /* BE CAREFUL !! The following lines are specific to IPv4. The libcidr support IPv6 as well */
  memset( netAddr, 0, sizeof( unsigned long ) ) ;
  memcpy( netAddr, &pcidr->addr[12], 4 ) ;
  *netAddr = ntohl( *netAddr ) ;

  memset( netMask, 0, sizeof( unsigned long ) ) ;
  memcpy( netMask, &pcidr->mask[12], 4 ) ;
  *netMask = ntohl( *netMask ) ;

  return 0 ; 
} /* nfs_LookupNetworkAddr */

int nfs_AddClientsToClientArray(exportlist_client_t *clients,
				int new_clients_number,
				char **new_clients_name, int option)
{
  int i = 0;
  int j = 0;
  unsigned int l = 0;
  char *client_hostname;
  struct addrinfo *info;
  exportlist_client_entry_t *p_clients;
  int is_wildcarded_host = FALSE;
  unsigned long netMask;
  unsigned long netAddr;

  /* How many clients are there already? */
  j = (*clients).num_clients;

  p_clients = (*clients).clientarray;

  if(p_clients == NULL)
    return ENOMEM;

  /* It's now time to set the information related to the new clients */
  for(i = j; i < j + new_clients_number; i++)
    {
      /* cleans the export entry */
      memset(&p_clients[i], 0, sizeof(exportlist_client_entry_t));

      netMask = 0;              /* default value for a host */
      client_hostname = new_clients_name[i - j];

      /* Set client options */
      p_clients[i].options |= option;

      /* using netdb to get information about the hostname */
      if(client_hostname[0] == '@')
        {

          /* Entry is a netgroup definition */
          strncpy(p_clients[i].client.netgroup.netgroupname,
                  (char *)(client_hostname + 1), MAXHOSTNAMELEN);

          p_clients[i].options |= EXPORT_OPTION_NETGRP;
          p_clients[i].type = NETGROUP_CLIENT;

          LogDebug(COMPONENT_CONFIG,
                   "----------------- %s to netgroup %s",
                   (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                   p_clients[i].client.netgroup.netgroupname);
        }
      else if( string_contains_slash( client_hostname ) &&
               ( nfs_LookupNetworkAddr( (char *)( client_hostname ),
                                         (unsigned long *)&netAddr,
                                         (unsigned long *)&netMask) == 0 ) )
        {
          /* Entry is a network definition */
          p_clients[i].client.network.netaddr = netAddr;
          p_clients[i].options |= EXPORT_OPTION_NETENT;
          p_clients[i].client.network.netmask = netMask;
          p_clients[i].type = NETWORK_CLIENT;

          LogDebug(COMPONENT_CONFIG,
                   "----------------- %s to network %s = %d.%d.%d.%d netmask=%x",
                   (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                   client_hostname,
                   (unsigned int)(p_clients[i].client.network.netaddr >> 24),
                   (unsigned int)((p_clients[i].client.network.netaddr >> 16) & 0xFF),
                   (unsigned int)((p_clients[i].client.network.netaddr >> 8) & 0xFF),
                   (unsigned int)(p_clients[i].client.network.netaddr & 0xFF),
                   (unsigned int)(p_clients[i].client.network.netmask));
        }
      else if( getaddrinfo(client_hostname, NULL, NULL, &info) == 0)
        {
          /* Entry is a hostif */
          if(info->ai_family == AF_INET)
            {
              struct in_addr infoaddr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;
              memcpy(&(p_clients[i].client.hostif.clientaddr), &infoaddr,
                     sizeof(struct in_addr));
              p_clients[i].type = HOSTIF_CLIENT;
              LogDebug(COMPONENT_CONFIG,
                       "----------------- %s to client %s = %d.%d.%d.%d",
                       (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                       client_hostname, 
                       (unsigned int)(p_clients[i].client.hostif.clientaddr >> 24),
                       (unsigned int)((p_clients[i].client.hostif.clientaddr >> 16) & 0xFF),
                       (unsigned int)((p_clients[i].client.hostif.clientaddr >> 8) & 0xFF),
                       (unsigned int)(p_clients[i].client.hostif.clientaddr & 0xFF));
            }
       else /* AF_INET6 */
            {
              struct in6_addr infoaddr = ((struct sockaddr_in6 *)info->ai_addr)->sin6_addr;
              /* IPv6 address */
              memcpy(&(p_clients[i].client.hostif.clientaddr6), &infoaddr,
                     sizeof(struct in6_addr));
              p_clients[i].type = HOSTIF_CLIENT_V6;
            }
          freeaddrinfo(info);
        }
     else
        {
          /* this may be  a wildcarded host */
          /* Lookup into the string to see if it contains '*' or '?' */
          is_wildcarded_host = FALSE;
          for(l = 0; l < strlen(client_hostname); l++)
            {
              if((client_hostname[l] == '*') || (client_hostname[l] == '?'))
                {
                  is_wildcarded_host = TRUE;
                  break;
                }
            }

          if(is_wildcarded_host == TRUE)
            {
              p_clients[i].type = WILDCARDHOST_CLIENT;
              strncpy(p_clients[i].client.wildcard.wildcard, client_hostname,
                      MAXHOSTNAMELEN);

              LogFullDebug(COMPONENT_CONFIG,
                           "----------------- %s to wildcard %s",
                           (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                           client_hostname);
            }
          else
            {
              p_clients[i].type = BAD_CLIENT;
              /* Last case: type for client could not be identified. This should not occur */
              LogCrit(COMPONENT_CONFIG,
                      "Unsupported type for client %s", client_hostname);
            }
        }
    }                           /* for i */

  /* Before we finish, do not forget to set the new number of clients
   * and the new pointer to client array.
   */
  (*clients).num_clients += new_clients_number;

  return 0;                     /* success !! */
}                               /* nfs_AddClientsToClientArray */


/**
 *
 * nfs_AddClientsToExportList : Adds a client to an export list
 *
 * Adds a client to an export list (temporary function ?).
 *
 * @todo BUGAZOMEU : handling wildcards.
 *
 */
static int nfs_AddClientsToExportList(exportlist_t * ExportEntry,
                                      int new_clients_number,
                                      char **new_clients_name, int option)
{
  /*
   * Notifying the export list structure that another option is to be
   * handled
   */
  ExportEntry->options |= option;
  nfs_AddClientsToClientArray( &ExportEntry->clients, new_clients_number,
			       new_clients_name, option);
  return 0;
}                               /* nfs_AddClientsToExportList */

#define DEFINED_TWICE_WARNING( _str_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ_EXPORT: WARNING: %s defined twice !!! (ignored)", _str_ )

int parseAccessParam(char *var_name, char *var_value,
			    exportlist_t *p_entry, int access_option) {
  int rc, err_flag = FALSE;
  char *expended_node_list;

  /* temp array of clients */
  char *client_list[EXPORT_MAX_CLIENTS];
  int idx;
  int count;

  /* expends host[n-m] notations */
  count =
    nodelist_common_condensed2extended_nodelist(var_value, &expended_node_list);

  if(count <= 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ_EXPORT: ERROR: Invalid format for client list in EXPORT::%s definition",
	      var_name);

      return -1;
    }
  else if(count > EXPORT_MAX_CLIENTS)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Client list too long (%d>%d)",
	      count, EXPORT_MAX_CLIENTS);
      return -1;
    }

  /* allocate clients strings  */
  for(idx = 0; idx < count; idx++)
    {
      client_list[idx] = (char *)Mem_Alloc(EXPORT_MAX_CLIENTLEN);
      client_list[idx][0] = '\0';
    }

  /*
   * Search for coma-separated list of hosts, networks and netgroups
   */
  rc = nfs_ParseConfLine(client_list, count,
			 expended_node_list, find_comma, find_endLine);

  /* free the buffer the nodelist module has allocated */
  free(expended_node_list);

  if(rc < 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Client list too long (>%d)", count);

      /* free client strings */
      for(idx = 0; idx < count; idx++)
	Mem_Free((caddr_t) client_list[idx]);

      return rc;
    }

  rc = nfs_AddClientsToExportList(p_entry,
				  rc, (char **)client_list, access_option);

  if(rc != 0)
    {
      err_flag = TRUE;
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Invalid client found in \"%s\"",
	      var_value);

      /* free client strings */
      for(idx = 0; idx < count; idx++)
	Mem_Free((caddr_t) client_list[idx]);

      return rc;
    }

  /* everything is OK */

  /* free client strings */
  for(idx = 0; idx < count; idx++)
    Mem_Free((caddr_t) client_list[idx]);

  return rc;
}

/**
 * BuildExportEntry : builds an export entry from configutation file.
 * Don't stop immediately on error,
 * continue parsing the file, for listing other errors.
 */
static int BuildExportEntry(config_item_t block, exportlist_t ** pp_export)
{
  exportlist_t *p_entry;
  int i, rc;
  char *var_name;
  char *var_value;

  /* the mandatory options */

  unsigned int mandatory_options =
      (FLAG_EXPORT_ID | FLAG_EXPORT_PATH |
       FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_PSEUDO);

  /* the given options */

  unsigned int set_options = 0;

  int err_flag   = FALSE;
  int err_policy = FALSE;

  /* allocates export entry */
  p_entry = (exportlist_t *) Mem_Alloc(sizeof(exportlist_t));

  if(p_entry == NULL)
    return Mem_Errno;

  memset(p_entry, 0, sizeof(exportlist_t));

  /** @todo set default values here */
  memset(p_entry, 0, sizeof(exportlist_t));

  p_entry->status = EXPORTLIST_OK;
  p_entry->access_type = ACCESSTYPE_RW;
  p_entry->anonymous_uid = (uid_t) ANON_UID;
  p_entry->anonymous_gid = (gid_t) ANON_GID;
  p_entry->use_commit = TRUE;

  /* Defaults for FSAL_UP. It is ok to leave the filter list NULL
   * even if we enable the FSAL_UP. */
  #ifdef _USE_FSAL_UP
  p_entry->use_fsal_up = FALSE;
  p_entry->fsal_up_filter_list = NULL;
  p_entry->fsal_up_timeout.seconds = 30;
  p_entry->fsal_up_timeout.nseconds = 0;
  strncpy(p_entry->fsal_up_type,"DUMB", 4);
  /* We don't create the thread until all exports are parsed. */
  memset(&p_entry->fsal_up_thr, 0, sizeof(pthread_t));
#endif /* _USE_FSAL_UP */

  /* by default, we support auth_none and auth_sys */
  p_entry->options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and both transport protocols */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV2;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV4;
  p_entry->options |= EXPORT_OPTION_UDP | EXPORT_OPTION_TCP;

  p_entry->filesystem_id.major = (fsal_u64_t) 666;
  p_entry->filesystem_id.minor = (fsal_u64_t) 666;

  p_entry->MaxWrite = (fsal_size_t) 16384;
  p_entry->MaxRead = (fsal_size_t) 16384;
  p_entry->PrefWrite = (fsal_size_t) 16384;
  p_entry->PrefRead = (fsal_size_t) 16384;
  p_entry->PrefReaddir = (fsal_size_t) 16384;
  p_entry->cache_inode_policy = CACHE_INODE_POLICY_FULL_WRITE_THROUGH ;

  strcpy(p_entry->FS_specific, "");
  strcpy(p_entry->FS_tag, "");
  strcpy(p_entry->fullpath, "/");
  strcpy(p_entry->dirname, "/");
  strcpy(p_entry->fsname, "");
  strcpy(p_entry->pseudopath, "/");
  strcpy(p_entry->referral, "");

  unsigned int fsalid_is_set= FALSE ;

  /* parse options for this export entry */

  for(i = 0; i < config_GetNbItems(block); i++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, i);

      /* get var name and value */
      rc = config_GetKeyValue(item, &var_name, &var_value);

      if((rc != 0) || (var_value == NULL))
        {
          Mem_Free(p_entry);
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ_EXPORT: ERROR: internal error %d", rc);
          /* free the entry before exiting */
          return -1;
        }

      if(!STRCMP(var_name, CONF_EXPORT_ID))
        {

          long int export_id;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ID) == FLAG_EXPORT_ID)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ID);
              continue;
            }

          /* parse and check export_id */
          errno = 0;
          export_id = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid export_id: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(export_id <= 0 || export_id > USHRT_MAX)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Export_id out of range: \"%ld\"",
                      export_id);
              err_flag = TRUE;
              continue;
            }

          /* set export_id */

          p_entry->id = (unsigned short)export_id;
          set_options |= FLAG_EXPORT_ID;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_PATH))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PATH) == FLAG_EXPORT_PATH)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PATH);
              continue;
            }

          if(*var_value == '\0')
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Empty export path");
              err_flag = TRUE;
              continue;
            }

      /** @todo What variable must be set ? */

          strncpy(p_entry->fullpath, var_value, MAXPATHLEN);

      /** @todo : change to MAXPATHLEN in exports.h */
          strncpy(p_entry->dirname, var_value, MAXNAMLEN);
          strncpy(p_entry->fsname, "", MAXNAMLEN);

          set_options |= FLAG_EXPORT_PATH;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ROOT))
        {
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_ROOT);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
          set_options |= FLAG_EXPORT_ROOT_OR_ACCESS;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESS))
        {
          parseAccessParam(var_name, var_value, p_entry,
                           EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_ACCESS))
        {
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_MD_WRITE_ACCESS | EXPORT_OPTION_MD_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_RO_ACCESS))
        {
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_MD_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READ_ACCESS))
        {
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READWRITE_ACCESS))
        {
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PSEUDO))
        {

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PSEUDO) == FLAG_EXPORT_PSEUDO)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PSEUDO);
              continue;
            }

          if(*var_value != '/')
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Pseudo path must begin with a slash (invalid pseudo path: %s).",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          strncpy(p_entry->pseudopath, var_value, MAXPATHLEN);

          set_options |= FLAG_EXPORT_PSEUDO;
          p_entry->options |= EXPORT_OPTION_PSEUDO;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_REFERRAL))
        {
          strncpy(p_entry->referral, var_value, MAXPATHLEN);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESSTYPE))
        {
          // check if it has not already been set
          if((set_options & FLAG_EXPORT_ACCESSTYPE) == FLAG_EXPORT_ACCESSTYPE)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ACCESSTYPE);
              continue;
            }

          if(!STRCMP(var_value, "RW"))
            {
              p_entry->access_type = ACCESSTYPE_RW;
            }
          else if(!STRCMP(var_value, "RO"))
            {
              p_entry->access_type = ACCESSTYPE_RO;
            }
          else if(!STRCMP(var_value, "MDONLY"))
            {
              p_entry->access_type = ACCESSTYPE_MDONLY;
            }
          else if(!STRCMP(var_value, "MDONLY_RO"))
            {
              p_entry->access_type = ACCESSTYPE_MDONLY_RO;
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid access type \"%s\". Values can be: RW, RO, MDONLY, MDONLY_RO.",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          set_options |= FLAG_EXPORT_ACCESSTYPE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_NFS_PROTO))
        {

#     define MAX_NFSPROTO      10       /* large enough !!! */
#     define MAX_NFSPROTO_LEN  256      /* so is it !!! */

          char *nfsvers_list[MAX_NFSPROTO];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NFS_PROTO) == FLAG_EXPORT_NFS_PROTO)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_NFS_PROTO);
              continue;
            }

          /* reset nfs proto flags (clean defaults) */
          p_entry->options &= ~(EXPORT_OPTION_NFSV2
                                | EXPORT_OPTION_NFSV3 | EXPORT_OPTION_NFSV4);

          /* allocate nfs vers strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            nfsvers_list[idx] = (char *)Mem_Alloc(MAX_NFSPROTO_LEN);

          /*
           * Search for coma-separated list of nfsprotos
           */
          count = nfs_ParseConfLine(nfsvers_list, MAX_NFSPROTO,
                                    var_value, find_comma, find_endLine);

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: NFS protocols list too long (>%d)",
                      MAX_NFSPROTO);

              /* free sec strings */
              for(idx = 0; idx < MAX_NFSPROTO; idx++)
                Mem_Free((caddr_t) nfsvers_list[idx]);

              continue;
            }

          /* add each Nfs protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(nfsvers_list[idx], "2"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
                    p_entry->options |= EXPORT_OPTION_NFSV2;
                  else
                    {
                      LogCrit(COMPONENT_CONFIG,
                              "NFS READ_EXPORT: ERROR: NFS version 2 is disabled in NFS_Core_Param.");
                      err_flag = TRUE;
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "3"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
                    p_entry->options |= EXPORT_OPTION_NFSV3;
                  else
                    {
                      LogCrit(COMPONENT_CONFIG,
                              "NFS READ_EXPORT: ERROR: NFS version 3 is disabled in NFS_Core_Param.");
                      err_flag = TRUE;
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "4"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
                    p_entry->options |= EXPORT_OPTION_NFSV4;
                  else
                    {
                      LogCrit(COMPONENT_CONFIG,
                              "NFS READ_EXPORT: ERROR: NFS version 4 is disabled in NFS_Core_Param.");
                      err_flag = TRUE;
                    }
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ_EXPORT: ERROR: Invalid NFS version \"%s\". Values can be: 2, 3, 4.",
                          nfsvers_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            Mem_Free((caddr_t) nfsvers_list[idx]);

          /* check that at least one nfs protocol has been specified */
          if((p_entry->options & (EXPORT_OPTION_NFSV2
                                  | EXPORT_OPTION_NFSV3 | EXPORT_OPTION_NFSV4)) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: WARNING: /!\\ Empty NFS_protocols list");
              err_flag = TRUE;
            }

          set_options |= FLAG_EXPORT_NFS_PROTO;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_TRANS_PROTO))
        {

#     define MAX_TRANSPROTO      10     /* large enough !!! */
#     define MAX_TRANSPROTO_LEN  256    /* so is it !!! */

          char *transproto_list[MAX_TRANSPROTO];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_TRANS_PROTO) == FLAG_EXPORT_TRANS_PROTO)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_TRANS_PROTO);
              continue;
            }

          /* reset TRANS proto flags (clean defaults) */
          p_entry->options &= ~(EXPORT_OPTION_UDP | EXPORT_OPTION_TCP);

          /* allocate TRANS vers strings */
          for(idx = 0; idx < MAX_TRANSPROTO; idx++)
            transproto_list[idx] = (char *)Mem_Alloc(MAX_TRANSPROTO_LEN);

          /*
           * Search for coma-separated list of TRANSprotos
           */
          count = nfs_ParseConfLine(transproto_list, MAX_TRANSPROTO,
                                    var_value, find_comma, find_endLine);

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Protocol list too long (>%d)",
                      MAX_TRANSPROTO);

              /* free sec strings */
              for(idx = 0; idx < MAX_TRANSPROTO; idx++)
                Mem_Free((caddr_t) transproto_list[idx]);

              continue;
            }

          /* add each TRANS protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(transproto_list[idx], "UDP"))
                {
                  p_entry->options |= EXPORT_OPTION_UDP;
                }
              else if(!STRCMP(transproto_list[idx], "TCP"))
                {
                  p_entry->options |= EXPORT_OPTION_TCP;
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ_EXPORT: ERROR: Invalid protocol \"%s\". Values can be: UDP, TCP.",
                          transproto_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_TRANSPROTO; idx++)
            Mem_Free((caddr_t) transproto_list[idx]);

          /* check that at least one TRANS protocol has been specified */
          if((p_entry->options & (EXPORT_OPTION_UDP | EXPORT_OPTION_TCP)) == 0)
            LogCrit(COMPONENT_CONFIG,
                    "TRANS READ_EXPORT: WARNING: /!\\ Empty protocol list");

          set_options |= FLAG_EXPORT_TRANS_PROTO;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ALL_ANON))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ALL_ANON) == FLAG_EXPORT_ALL_ANON)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ALL_ANON);
              continue;
            }

          if (StrToBoolean(var_value))
            p_entry->all_anonymous = TRUE;

          set_options |= FLAG_EXPORT_ANON_USER;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_ROOT))
        {
          long int anon_uid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_USER);
              continue;
            }

          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_ROOT);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid Anonymous_uid: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */
          p_entry->anonymous_uid = (uid_t) anon_uid;

          set_options |= FLAG_EXPORT_ANON_ROOT;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_USER))
        {
          long int anon_uid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_USER);
              continue;
            }

          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_ROOT);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid Anonymous_uid: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */

          p_entry->anonymous_uid = (uid_t) anon_uid;

          set_options |= FLAG_EXPORT_ANON_USER;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_GROUP))
        {

          long int anon_gid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_GROUP) == FLAG_EXPORT_ANON_GROUP)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_GROUP);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_gid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid Anonymous_gid: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */

          p_entry->anonymous_gid = (gid_t) anon_gid;

          set_options |= FLAG_EXPORT_ANON_GROUP;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_SECTYPE))
        {
#     define MAX_SECTYPE      10        /* large enough !!! */
#     define MAX_SECTYPE_LEN  256       /* so is it !!! */

          char *sec_list[MAX_SECTYPE];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_SECTYPE) == FLAG_EXPORT_SECTYPE)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_SECTYPE);
              continue;
            }

          /* reset security flags (clean defaults) */
          p_entry->options &= ~(EXPORT_OPTION_AUTH_NONE
                                | EXPORT_OPTION_AUTH_UNIX
                                | EXPORT_OPTION_RPCSEC_GSS_NONE
                                | EXPORT_OPTION_RPCSEC_GSS_INTG
                                | EXPORT_OPTION_RPCSEC_GSS_PRIV);

          /* allocate sec strings */
          for(idx = 0; idx < MAX_SECTYPE; idx++)
            sec_list[idx] = (char *)Mem_Alloc(MAX_SECTYPE_LEN);

          /*
           * Search for coma-separated list of sectypes
           */
          count = nfs_ParseConfLine(sec_list, MAX_SECTYPE,
                                    var_value, find_comma, find_endLine);

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: SecType list too long (>%d)",
                      MAX_SECTYPE);

              /* free sec strings */
              for(idx = 0; idx < MAX_SECTYPE; idx++)
                Mem_Free((caddr_t) sec_list[idx]);

              continue;
            }

          /* add each sectype flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(sec_list[idx], "none"))
                {
                  p_entry->options |= EXPORT_OPTION_AUTH_NONE;
                }
              else if(!STRCMP(sec_list[idx], "sys"))
                {
                  p_entry->options |= EXPORT_OPTION_AUTH_UNIX;
                }
              else if(!STRCMP(sec_list[idx], "krb5"))
                {
                  p_entry->options |= EXPORT_OPTION_RPCSEC_GSS_NONE;
                }
              else if(!STRCMP(sec_list[idx], "krb5i"))
                {
                  p_entry->options |= EXPORT_OPTION_RPCSEC_GSS_INTG;
                }
              else if(!STRCMP(sec_list[idx], "krb5p"))
                {
                  p_entry->options |= EXPORT_OPTION_RPCSEC_GSS_PRIV;
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ_EXPORT: ERROR: Invalid SecType \"%s\". Values can be: none, sys, krb5, krb5i, krb5p.",
                          sec_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_SECTYPE; idx++)
            Mem_Free((caddr_t) sec_list[idx]);

          /* check that at least one sectype has been specified */
          if((p_entry->options & (EXPORT_OPTION_AUTH_NONE
                                  | EXPORT_OPTION_AUTH_UNIX
                                  | EXPORT_OPTION_RPCSEC_GSS_NONE
                                  | EXPORT_OPTION_RPCSEC_GSS_INTG
                                  | EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0)
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ_EXPORT: WARNING: /!\\ Empty SecType");

          set_options |= FLAG_EXPORT_SECTYPE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_READ))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_MAX_READ) == FLAG_EXPORT_MAX_READ)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_MAX_READ);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxRead: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: MaxRead out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxRead = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_MAXREAD;

          set_options |= FLAG_EXPORT_MAX_READ;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_WRITE))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_MAX_WRITE) == FLAG_EXPORT_MAX_WRITE)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_MAX_WRITE);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxWrite: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: MaxWrite out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxWrite = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_MAXWRITE;

          set_options |= FLAG_EXPORT_MAX_WRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READ))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_READ) == FLAG_EXPORT_PREF_READ)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PREF_READ);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid PrefRead: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefRead out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefRead = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_PREFREAD;

          set_options |= FLAG_EXPORT_PREF_READ;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_WRITE))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_WRITE) == FLAG_EXPORT_PREF_WRITE)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PREF_WRITE);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid PrefWrite: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefWrite out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefWrite = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_PREFWRITE;

          set_options |= FLAG_EXPORT_PREF_WRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READDIR))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_READDIR) == FLAG_EXPORT_PREF_READDIR)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PREF_READDIR);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid PrefReaddir: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefReaddir out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefReaddir = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_PREFRDDIR;

          set_options |= FLAG_EXPORT_PREF_READDIR;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_WRITE))
        {
          long long int size;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_WRITE) == FLAG_EXPORT_PREF_WRITE)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_PREF_WRITE);
              continue;
            }

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid PrefWrite: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefWrite out of range: %lld",
                      size);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefWrite = (fsal_size_t) size;
          p_entry->options |= EXPORT_OPTION_PREFWRITE;

          set_options |= FLAG_EXPORT_PREF_WRITE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSID))
        {
          long long int major, minor;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FSID) == FLAG_EXPORT_FSID)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_FSID);
              continue;
            }

          /* parse and check filesystem id */
          errno = 0;
          major = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '.' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid filesystem_id: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          end_ptr++;            /* the first character after the dot */

          errno = 0;
          minor = strtoll(end_ptr, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid filesystem_id: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          if(major < 0 || minor < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: filesystem_id out of range: %lld.%lld",
                      major, minor);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->filesystem_id.major = (fsal_u64_t) major;
          p_entry->filesystem_id.minor = (fsal_u64_t) minor;

          set_options |= FLAG_EXPORT_FSID;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSUID))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NOSUID) == FLAG_EXPORT_NOSUID)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_NOSUID);
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_NOSUID;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): TRUE or FALSE expected.",
                        var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }

          set_options |= FLAG_EXPORT_NOSUID;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSGID))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NOSGID) == FLAG_EXPORT_NOSGID)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_NOSGID);
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_NOSGID;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): TRUE or FALSE expected.",
                      var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          set_options |= FLAG_EXPORT_NOSGID;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PRIVILEGED_PORT))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PRIVILEGED_PORT) == FLAG_EXPORT_PRIVILEGED_PORT)
            {
              DEFINED_TWICE_WARNING("FLAG_EXPORT_PRIVILEGED_PORT");
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_PRIVILEGED_PORT;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      var_name, var_value);
              err_flag = TRUE;
              continue;
            }
          set_options |= FLAG_EXPORT_PRIVILEGED_PORT;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_DATACACHE))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_DATACACHE) == FLAG_EXPORT_USE_DATACACHE)
            {
              DEFINED_TWICE_WARNING("FLAG_EXPORT_USE_DATACACHE");
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_USE_DATACACHE;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      var_name, var_value);
              err_flag = TRUE;
              continue;
            }
          set_options |= FLAG_EXPORT_USE_DATACACHE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PNFS))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_PNFS) == FLAG_EXPORT_USE_PNFS)
            {
              DEFINED_TWICE_WARNING("FLAG_EXPORT_USE_PNFS");
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_USE_PNFS;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      var_name, var_value);
              err_flag = TRUE;
              continue;
            }
          set_options |= EXPORT_OPTION_USE_PNFS;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_SPECIFIC))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_SPECIFIC) == FLAG_EXPORT_FS_SPECIFIC)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_FS_SPECIFIC);
              continue;
            }

          strncpy(p_entry->FS_specific, var_value, MAXPATHLEN);

          set_options |= FLAG_EXPORT_FS_SPECIFIC;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_TAG))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_TAG) == FLAG_EXPORT_FS_TAG)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_FS_TAG);
              continue;
            }

          strncpy(p_entry->FS_tag, var_value, MAXPATHLEN);

          set_options |= FLAG_EXPORT_FS_TAG;

        }
      else if( !STRCMP(var_name, CONF_EXPORT_CACHE_POLICY ))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_CACHE_POLICY) == FLAG_EXPORT_CACHE_POLICY)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_CACHE_POLICY);
              continue;
            }
          else if( !STRCMP( var_value, "WriteThrough" ) )
           {
              p_entry->cache_inode_policy = CACHE_INODE_POLICY_FULL_WRITE_THROUGH ; 
              err_policy = FALSE  ;
           } 
          else if( !STRCMP( var_value, "WriteBack" ) )         
           {
              p_entry->cache_inode_policy = CACHE_INODE_POLICY_FULL_WRITE_BACK ; 
              err_policy = FALSE  ;
           } 
          else if( !STRCMP( var_value, "AttrsOnlyWriteThrough" ) )
           {
              p_entry->cache_inode_policy = CACHE_INODE_POLICY_ATTRS_ONLY_WRITE_THROUGH ; 
              err_policy = FALSE  ;
           } 
          else if( !STRCMP( var_value, "NoCache" ) )             
           {
              p_entry->cache_inode_policy = CACHE_INODE_POLICY_NO_CACHE ; 
              err_policy = FALSE  ;
           } 
          else
             err_policy = TRUE ;

        
          if( err_policy == TRUE ) 
           {
             err_flag = TRUE ;
             
             LogCrit(COMPONENT_CONFIG, "Invalid Cache_Inode_Policy value : %s", var_value ) ;
           }

          set_options |=  FLAG_EXPORT_CACHE_POLICY ;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_WRITE))
        {
          long long int offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxOffsetWrite: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxOffsetWrite = (fsal_size_t) offset;
          p_entry->options |= EXPORT_OPTION_MAXOFFSETWRITE;

          set_options |= FLAG_EXPORT_MAX_OFF_WRITE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_CACHE_SIZE))
        {
          long long int offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxCacheSize: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxCacheSize = (fsal_size_t) offset;
          p_entry->options |= EXPORT_OPTION_MAXCACHESIZE;

          set_options |= FLAG_EXPORT_MAX_CACHE_SIZE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_READ))
        {
          long long int offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxOffsetRead: \"%s\"",
                      var_value);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxOffsetRead = (fsal_size_t) offset;
          p_entry->options |= EXPORT_OPTION_MAXOFFSETREAD;

          set_options |= FLAG_EXPORT_MAX_OFF_READ;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COMMIT))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->use_commit = TRUE;
              break;

            case 0:
              p_entry->use_commit = FALSE;
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): TRUE or FALSE expected.",
                        var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_GANESHA_WRITE_BUFFER))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->use_ganesha_write_buffer = TRUE;
              break;

            case 0:
              p_entry->use_ganesha_write_buffer = FALSE;
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): TRUE or FALSE expected.",
                        var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
#ifdef _USE_FSAL_UP
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL_UP_TYPE))
	{
          strncpy(p_entry->fsal_up_type,var_value,sizeof(var_value));
	}
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL_UP_TIMEOUT))
        {
	  /* Right now we are expecting seconds ... we should support
	   * nseconds as well! */
          p_entry->fsal_up_timeout.seconds = atoi(var_value);
          if (p_entry->fsal_up_timeout.seconds < 0
	      || p_entry->fsal_up_timeout.nseconds < 0)
	    {
	      p_entry->fsal_up_timeout.seconds = 0;
	      p_entry->fsal_up_timeout.nseconds = 0;
	    }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL_UP_FILTERS))
        {
          /* TODO: Parse the strings and form a list.
           * Later each name will match a predefined filter
           * in the FSAL UP interface. */
          p_entry->fsal_up_filter_list = NULL;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_FSAL_UP))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->use_fsal_up = TRUE;
              break;
            case 0:
              p_entry->use_fsal_up = FALSE;
              break;
            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "USR_FSAL_UP: ERROR: Invalid value for %s (%s): TRUE or FALSE expected.",
                        var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
#endif /* _USE_FSAL_UP */
      else if(!STRCMP(var_name, CONF_EXPORT_FSALID))
        {
           if( ( p_entry->fsalid = FSAL_name2fsalid( var_value ) ) == -1 )
            {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s)", var_name, var_value ) ;
#ifdef _USE_SHARED_FSAL
		/* Critacal only for the FSAL-less daemon */
                err_flag= TRUE ;
#endif
		continue;
            }
          else
            fsalid_is_set = TRUE ;
        }
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ_EXPORT: WARNING: Unknown option: %s",
                  var_name);
        }

    }

  /** check for mandatory options */
  if((set_options & mandatory_options) != mandatory_options)
    {
      if((set_options & FLAG_EXPORT_ID) != FLAG_EXPORT_ID)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s",
                CONF_EXPORT_ID );

      if((set_options & FLAG_EXPORT_PATH) != FLAG_EXPORT_PATH)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s",
                CONF_EXPORT_PATH);

      if((set_options & FLAG_EXPORT_ROOT_OR_ACCESS) != FLAG_EXPORT_ROOT_OR_ACCESS)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s or %s or %s",
                CONF_EXPORT_ROOT, CONF_EXPORT_READ_ACCESS,
                CONF_EXPORT_READWRITE_ACCESS);

      if((set_options & FLAG_EXPORT_PSEUDO) != FLAG_EXPORT_PSEUDO)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s",
                CONF_EXPORT_PSEUDO);

      err_flag = TRUE;
    }

  if (
      ((set_options & FLAG_EXPORT_ACCESSTYPE) || (set_options & FLAG_EXPORT_ACCESS_LIST)) &&
      (set_options & FLAG_EXPORT_ACCESSTYPE_LIST))
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: %s list cannot be used when %s and/or %s are used in the same export entry config.",
              CONF_EXPORT_READWRITE_ACCESS, CONF_EXPORT_ACCESSTYPE,
              CONF_EXPORT_ACCESS);
      err_flag = TRUE;
    }

  if ((set_options & FLAG_EXPORT_ACCESSTYPE) || (set_options & FLAG_EXPORT_ACCESS_LIST))
    p_entry->new_access_list_version = FALSE;
  else
    p_entry->new_access_list_version = TRUE;

  /* check if there had any error.
   * if so, free the p_entry and return an error.
   */
  if(err_flag)
    {
      Mem_Free(p_entry);
      return -1;
    }

#ifdef _USE_SHARED_FSAL
  if( fsalid_is_set == FALSE ) 
   {
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s",
                CONF_EXPORT_FSALID);
        return -1 ;
   }

  /* Check is FSAL is loaded */
  if( !FSAL_Is_Loaded( p_entry->fsalid ) ) 
   {
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: FSAL library not loaded for fsalid=%s in export entry (id=%u)", 
                FSAL_fsalid2name( p_entry->fsalid ), p_entry->id ) ;
        return -1 ;
   }
#endif


  *pp_export = p_entry;

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return 0;

}

/**
 * BuildDefaultExport : builds an export entry for '/'
 * with default parameters.
 */

static char *client_root_access[] = { "*" };

exportlist_t *BuildDefaultExport()
{
  exportlist_t *p_entry;
  int rc;

  /* allocates new export entry */
  p_entry = (exportlist_t *) Mem_Alloc(sizeof(exportlist_t));

  if(p_entry == NULL)
    return NULL;

  /** @todo set default values here */

  p_entry->next = NULL;
  p_entry->options = 0;
  p_entry->status = EXPORTLIST_OK;
  p_entry->clients.num_clients = 0;
  p_entry->access_type = ACCESSTYPE_RW;
  p_entry->anonymous_uid = (uid_t) ANON_UID;
  p_entry->MaxOffsetWrite = (fsal_off_t) 0;
  p_entry->MaxOffsetRead = (fsal_off_t) 0;
  p_entry->MaxCacheSize = (fsal_off_t) 0;

  /* by default, we support auth_none and auth_sys */
  p_entry->options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and both transport protocols */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV2;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV4;
  p_entry->options |= EXPORT_OPTION_UDP | EXPORT_OPTION_TCP;

  p_entry->filesystem_id.major = (fsal_u64_t) 101;
  p_entry->filesystem_id.minor = (fsal_u64_t) 101;

  p_entry->MaxWrite = (fsal_size_t) 16384;
  p_entry->MaxRead = (fsal_size_t) 16384;
  p_entry->PrefWrite = (fsal_size_t) 16384;
  p_entry->PrefRead = (fsal_size_t) 16384;
  p_entry->PrefReaddir = (fsal_size_t) 16384;

  strcpy(p_entry->FS_specific, "");
  strcpy(p_entry->FS_tag, "ganesha");

  p_entry->id = 1;

  strcpy(p_entry->fullpath, "/");
  strcpy(p_entry->dirname, "/");
  strcpy(p_entry->fsname, "");
  strcpy(p_entry->pseudopath, "/");
  strcpy(p_entry->referral, "");

  p_entry->UseCookieVerifier = FALSE;

  /**
   * Grant root access to all clients
   */
  rc = nfs_AddClientsToExportList(p_entry, 1, client_root_access, EXPORT_OPTION_ROOT);

  if(rc != 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Invalid client \"%s\"",
              (char *)client_root_access);
      return NULL;
    }

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return p_entry;

}                               /* BuildDefaultExport */

/**
 * ReadExports:
 * Read the export entries from the parsed configuration file.
 * \return A negative value on error,
 *         the number of export entries else.
 */
int ReadExports(config_file_t in_config,        /* The file that contains the export list */
                exportlist_t ** ppexportlist)   /* Pointer to the export list */
{

  int nb_blk, rc, i;
  char *blk_name;
  int err_flag = FALSE;

  exportlist_t *p_export_item = NULL;
  exportlist_t *p_export_last = NULL;

  int nb_entries = 0;

  if(!ppexportlist)
    return -EFAULT;

  *ppexportlist = NULL;

  /* get the number of blocks in the configuration file */
  nb_blk = config_GetNbBlocks(in_config);

  if(nb_blk < 0)
    return -1;

  /* Iteration on config file blocks. */
  for(i = 0; i < nb_blk; i++)
    {
      config_item_t block;

      block = config_GetBlockByIndex(in_config, i);

      if(block == NULL)
        return -1;

      /* get the name of the block */
      blk_name = config_GetBlockName(block);

      if(blk_name == NULL)
        return -1;

      if(!STRCMP(blk_name, CONF_LABEL_EXPORT))
        {

          rc = BuildExportEntry(block, &p_export_item);

          /* If the entry is errorneous, ignore it
           * and continue checking syntax of other entries.
           */
          if(rc != 0)
            {
              err_flag = TRUE;
              continue;
            }

          p_export_item->next = NULL;

          if(*ppexportlist == NULL)
            {
              *ppexportlist = p_export_item;
            }
          else
            {
              p_export_last->next = p_export_item;
            }
          p_export_last = p_export_item;

          nb_entries++;

        }

    }

  if(err_flag)
    {
      return -1;
    }
  else
    return nb_entries;
}

/**
 * function for matching a specific option in the client export list.
 */
int export_client_match(sockaddr_t *hostaddr,
			char *ipstring,
			exportlist_client_t *clients,
			exportlist_client_entry_t * pclient_found,
			unsigned int export_option)
{
  unsigned int i;
  int rc;
  char hostname[MAXHOSTNAMELEN];
  in_addr_t addr = get_in_addr(hostaddr);

  if(export_option & EXPORT_OPTION_ROOT)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for root access entries");

  if(export_option & EXPORT_OPTION_READ_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                  "Looking for nonroot access read entries");

  if(export_option & EXPORT_OPTION_WRITE_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for nonroot access write entries");

  for(i = 0; i < clients->num_clients; i++)
    {
      /* Make sure the client entry has the permission flags we're looking for
       * Also make sure we aren't looking at a root client entry when we're not root. */
      if(((clients->clientarray[i].options & export_option) == 0) ||
         ((clients->clientarray[i].options & EXPORT_OPTION_ROOT) != (export_option & EXPORT_OPTION_ROOT)))
        continue;

      switch (clients->clientarray[i].type)
        {
        case HOSTIF_CLIENT:
          if(clients->clientarray[i].client.hostif.clientaddr == addr)
            {
              LogFullDebug(COMPONENT_DISPATCH, "This matches host address");
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }
          break;

        case NETWORK_CLIENT:
          LogDebug( COMPONENT_DISPATCH, "test NETWORK_CLIENT: addr=%#.08X, netmask=%#.08X, match with %#.08X",
                    clients->clientarray[i].client.network.netaddr,
                    clients->clientarray[i].client.network.netmask, ntohl(addr));
          LogFullDebug(COMPONENT_DISPATCH,
                       "Test net %d.%d.%d.%d in %d.%d.%d.%d ??",
                       (unsigned int)(clients->clientarray[i].client.network.netaddr >> 24),
                       (unsigned int)((clients->clientarray[i].client.network.netaddr >> 16) & 0xFF),
                       (unsigned int)((clients->clientarray[i].client.network.netaddr >> 8) & 0xFF),
                       (unsigned int)(clients->clientarray[i].client.network.netaddr & 0xFF),
                       (unsigned int)(addr >> 24),
                       (unsigned int)(addr >> 16) & 0xFF,
                       (unsigned int)(addr >> 8) & 0xFF,
                       (unsigned int)(addr & 0xFF));

          if((clients->clientarray[i].client.network.netmask & ntohl(addr)) ==
             clients->clientarray[i].client.network.netaddr)
            {
              LogFullDebug(COMPONENT_DISPATCH, "This matches network address");
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }
          break;

        case NETGROUP_CLIENT:
          /* Try to get the entry from th IP/name cache */
          if((rc = nfs_ip_name_get(hostaddr, hostname)) != IP_NAME_SUCCESS)
            {
              if(rc == IP_NAME_NOT_FOUND)
                {
                  /* IPaddr was not cached, add it to the cache */
                  if(nfs_ip_name_add(hostaddr, hostname) != IP_NAME_SUCCESS)
                    {
                      /* Major failure, name could not be resolved */
                      break;
                    }
                }
            }

          /* At this point 'hostname' should contain the name that was found */
          if(innetgr
             (clients->clientarray[i].client.netgroup.netgroupname, hostname,
              NULL, NULL) == 1)
            {
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }
          break;

        case WILDCARDHOST_CLIENT:
          /* Now checking for IP wildcards */
          if(fnmatch
             (clients->clientarray[i].client.wildcard.wildcard, ipstring,
              FNM_PATHNAME) == 0)
            {
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }

          LogFullDebug(COMPONENT_DISPATCH,
                       "Did not match the ip address with a wildcard.");

          /* Try to get the entry from th IP/name cache */
          if((rc = nfs_ip_name_get(hostaddr, hostname)) != IP_NAME_SUCCESS)
            {
              if(rc == IP_NAME_NOT_FOUND)
                {
                  /* IPaddr was not cached, add it to the cache */
                  if(nfs_ip_name_add(hostaddr, hostname) != IP_NAME_SUCCESS)
                    {
                      /* Major failure, name could not be resolved */
                      LogFullDebug(COMPONENT_DISPATCH,
                                   "Could not resolve hostame for addr %u.%u.%u.%u ... not checking if a hostname wildcard matches",
                                   (unsigned int)(addr & 0xFF),
                                   (unsigned int)(addr >> 8) & 0xFF,
                                   (unsigned int)(addr >> 16) & 0xFF,
                                   (unsigned int)(addr >> 24));
                      break;
                    }
                }
            }
          LogFullDebug(COMPONENT_DISPATCH,
                       "Wildcarded hostname: testing if '%s' matches '%s'",
                       hostname, clients->clientarray[i].client.wildcard.wildcard);

          /* At this point 'hostname' should contain the name that was found */
          if(fnmatch
             (clients->clientarray[i].client.wildcard.wildcard, hostname,
              FNM_PATHNAME) == 0)
            {
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }
          LogFullDebug(COMPONENT_DISPATCH, "'%s' not matching '%s'",
                       hostname, clients->clientarray[i].client.wildcard.wildcard);
          break;

        case GSSPRINCIPAL_CLIENT:
          /** @toto BUGAZOMEU a completer lors de l'integration de RPCSEC_GSS */
          LogFullDebug(COMPONENT_DISPATCH,
                       "----------> Unsupported type GSS_PRINCIPAL_CLIENT");
          return FALSE;
          break;

       case BAD_CLIENT:
          LogDebug(COMPONENT_DISPATCH,
                  "Bad client in position %u seen in export list", i );
	  continue ;

        default:
           LogCrit(COMPONENT_DISPATCH,
                   "Unsupported client in position %u in export list with type %u", i, clients->clientarray[i].type);
	   continue ;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;

}                               /* export_client_match */

int export_client_matchv6(struct in6_addr *paddrv6,
			  exportlist_client_t *clients,
			  exportlist_client_entry_t * pclient_found,
			  unsigned int export_option)
{
  unsigned int i;

  if(export_option & EXPORT_OPTION_ROOT)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for root access entries");

  if(export_option & EXPORT_OPTION_READ_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for nonroot access read entries");

  if(export_option & EXPORT_OPTION_WRITE_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for nonroot access write entries");

  for(i = 0; i < clients->num_clients; i++)
    {
      /* Make sure the client entry has the permission flags we're looking for
       * Also make sure we aren't looking at a root client entry when we're not root. */
      if(((clients->clientarray[i].options & export_option) == 0) ||
         ((clients->clientarray[i].options & EXPORT_OPTION_ROOT) != (export_option & EXPORT_OPTION_ROOT)))
        continue;

      switch (clients->clientarray[i].type)
        {
        case HOSTIF_CLIENT:
        case NETWORK_CLIENT:
        case NETGROUP_CLIENT:
        case WILDCARDHOST_CLIENT:
        case GSSPRINCIPAL_CLIENT:
          break;

        case HOSTIF_CLIENT_V6:
          if(!memcmp(clients->clientarray[i].client.hostif.clientaddr6.s6_addr, paddrv6->s6_addr, 16))  /* Remember that IPv6 address are 128 bits = 16 bytes long */
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches host adress in IPv6");
              *pclient_found = clients->clientarray[i];
              return TRUE;
            }
          break;

        default:
          return FALSE;         /* Should never occurs */
          break;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;
}                               /* export_client_matchv6 */

/**
 * nfs_export_check_access: checks if a machine is authorized to access an export entry.
 *
 * Checks if a machine is authorized to access an export entry.
 *
 * @param ssaddr        [IN]    the complete remote address (as a sockaddr_storage to be IPv6 compliant)
 * @param ptr_req       [IN]    pointer to the related RPC request.
 * @param pexpprt       [IN]    related export entry (if found, NULL otherwise).
 * @param nfs_prog      [IN]    number for the NFS program.
 * @param mnt_program   [IN]    number for the MOUNT program.
 * @param ht_ip_stats   [INOUT] IP/stats hash table
 * @param ip_stats_pool [INOUT] IP/stats pool
 * @param pclient_found [OUT]   pointer to client entry found in export list, NULL if nothing was found.
 *
 * @return EXPORT_PERMISSION_GRANTED on success and EXPORT_PERMISSION_DENIED, EXPORT_WRITE_ATTEMPT_WHEN_RO, or EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO on failure.
 *
 */

int nfs_export_check_access(sockaddr_t *hostaddr,
                            struct svc_req *ptr_req,
                            exportlist_t * pexport,
                            unsigned int nfs_prog,
                            unsigned int mnt_prog,
                            hash_table_t * ht_ip_stats,
                            struct prealloc_pool *ip_stats_pool,
                            exportlist_client_entry_t * pclient_found,
                            struct user_cred *user_credentials,
                            bool_t proc_makes_write)
{
  int rc;
  char ipstring[SOCK_NAME_MAX];
  int ipvalid;

  if (pexport != NULL)
    {
      if (pexport->new_access_list_version)
        pexport->access_type = ACCESSTYPE_RW;
      else if(proc_makes_write && (pexport->access_type == ACCESSTYPE_RO))
        return EXPORT_WRITE_ATTEMPT_WHEN_RO;
      else if(proc_makes_write && (pexport->access_type == ACCESSTYPE_MDONLY_RO))
        return EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO;
    }

  ipstring[0] = '\0';
  ipvalid = sprint_sockip(hostaddr, ipstring, sizeof(ipstring));
  LogFullDebug(COMPONENT_DISPATCH,
               "nfs_export_check_access for address %s", ipstring);

  /* For now, no matching client is found */
  memset(pclient_found, 0, sizeof(exportlist_client_entry_t));

  /* PROC NULL is always authorized, in all protocols */
  if(ptr_req->rq_proc == 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Granted NULL proc");
      return EXPORT_PERMISSION_GRANTED;
    }

  /* If mount protocol is called, just check that AUTH_NONE is not used */
  if(ptr_req->rq_prog == mnt_prog)
    {
      if(ptr_req->rq_cred.oa_flavor != AUTH_NONE)
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Granted mnt_prog");
          return EXPORT_PERMISSION_GRANTED;
        }
      else
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Denied mnt_prog because it used AUTH_NONE");
          return EXPORT_PERMISSION_DENIED;
        }
    }
#ifdef _USE_TIPRC_IPV6
  if(hostaddr->ss_family == AF_INET)
#endif
    /* Increment the stats per client address (for IPv4 Only) */
    if((rc =
        nfs_ip_stats_incr(ht_ip_stats, hostaddr, nfs_prog, mnt_prog,
                          ptr_req)) == IP_STATS_NOT_FOUND)
      {
        if(nfs_ip_stats_add(ht_ip_stats, hostaddr, ip_stats_pool) == IP_STATS_SUCCESS)
          rc = nfs_ip_stats_incr(ht_ip_stats, hostaddr, nfs_prog, mnt_prog, ptr_req);
      }

#ifdef _USE_TIRPC_IPV6
  if(hostaddr->ss_family == AF_INET)
    {
#endif                          /* _USE_TIRPC_IPV6 */

      /* Use IP address as a string for wild character access checks. */
      if(!ipvalid)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Could not convert the IPv4 address to a character string.");
          return EXPORT_PERMISSION_DENIED;
        }

      if(pexport == NULL)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Error: no export to verify permissions against.");
          return EXPORT_PERMISSION_DENIED;
        }

      /* check if any root access export matches this client */
      if(user_credentials->caller_uid == 0)
        {
          if(export_client_match(hostaddr,
                                 ipstring,
                                 &(pexport->clients),
                                 pclient_found,
                                 EXPORT_OPTION_ROOT))
            {
              if(pexport->access_type == ACCESSTYPE_MDONLY_RO ||
                 pexport->access_type == ACCESSTYPE_MDONLY)
                {
                  LogFullDebug(COMPONENT_DISPATCH,
                               "Root granted MDONLY export permission");
                  return EXPORT_MDONLY_GRANTED;
                }
              else
                {
                  LogFullDebug(COMPONENT_DISPATCH,
                               "Root granted export permission");
                  return EXPORT_PERMISSION_GRANTED;
                }
            }
        }
      /* else, check if any access only export matches this client */
      if(proc_makes_write)
        {
          if(export_client_match(hostaddr,
                                 ipstring,
                                 &(pexport->clients),
                                 pclient_found,
                                 EXPORT_OPTION_WRITE_ACCESS))
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "Write permission to export granted");
              return EXPORT_PERMISSION_GRANTED;
            }
          else if(pexport->new_access_list_version &&
                  export_client_match(hostaddr,
                                      ipstring,
                                      &(pexport->clients),
                                      pclient_found,
                                      EXPORT_OPTION_MD_WRITE_ACCESS))
            {
              pexport->access_type = ACCESSTYPE_MDONLY;
              LogFullDebug(COMPONENT_DISPATCH,
                           "MDONLY export permission granted");
              return EXPORT_MDONLY_GRANTED;
            }
        }
      else
        {
          /* request will not write anything */
          if(export_client_match(hostaddr,
                                 ipstring,
                                 &(pexport->clients),
                                 pclient_found,
                                 EXPORT_OPTION_READ_ACCESS))
            {
              if(pexport->access_type == ACCESSTYPE_MDONLY_RO ||
                 pexport->access_type == ACCESSTYPE_MDONLY)
                {
                  LogFullDebug(COMPONENT_DISPATCH,
                               "MDONLY export permission granted - no write");
                  return EXPORT_MDONLY_GRANTED;
                }
              else
                {
                  LogFullDebug(COMPONENT_DISPATCH,
                               "Read export permission granted");
                  return EXPORT_PERMISSION_GRANTED;
                }
            }
          else if(pexport->new_access_list_version &&
                  export_client_match(hostaddr,
                                      ipstring,
                                      &(pexport->clients),
                                      pclient_found,
                                      EXPORT_OPTION_MD_READ_ACCESS))
            {
              pexport->access_type = ACCESSTYPE_MDONLY_RO;
              LogFullDebug(COMPONENT_DISPATCH,
                           "MDONLY export permission granted new access list");
              return EXPORT_MDONLY_GRANTED;
            }
        }
      LogFullDebug(COMPONENT_DISPATCH,
                   "export permission denied");
      return EXPORT_PERMISSION_DENIED;

#ifdef _USE_TIRPC_IPV6
    }
  else if(hostaddr->ss_family == AF_INET6)
    {
      static char ten_bytes_all_0[10];
      static unsigned two_bytes_all_1 = 0xFFFF;
      memset(ten_bytes_all_0, 0, 10);
      struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *)hostaddr;

      // if(isFulldebug(COMPONENT_DISPATCH))
        {
          char txtaddrv6[100];

          inet_ntop(psockaddr_in6->sin6_family,
                    psockaddr_in6->sin6_addr.s6_addr, txtaddrv6, 100);
          LogFullDebug(COMPONENT_DISPATCH,
                       "Client has IPv6 adress = %s", txtaddrv6);
        }

      /* If the client socket is IPv4, then it is wrapped into a   ::ffff:a.b.c.d IPv6 address. We check this here
       * This kind of adress is shaped like this:
       * |---------------------------------------------------------------|
       * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
       * |---------------------------------------------------------------|
       * |            0          |        FFFF       |    IPv4 address   |
       * |---------------------------------------------------------------|   */
      if(!memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
         !memcmp((char *)(psockaddr_in6->sin6_addr.s6_addr + 10),
                 (char *)&two_bytes_all_1, 2))
        {
          /* Use IP address as a string for wild character access checks. */
          if(!ipvalid)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Error: Could not convert the IPv6 address to a character string.");
              return EXPORT_PERMISSION_DENIED;
            }

          /* This is an IPv4 address mapped to an IPv6 one. Extract the IPv4 address and proceed with IPv4 autentication */
          memcpy((char *)&addr, (char *)(psockaddr_in6->sin6_addr.s6_addr + 12), 4);

          /* Proceed with IPv4 dedicated function */
          /* check if any root access export matches this client */
          if((user_credentials->caller_uid == 0) &&
             export_client_match(addr, ipstring, &(pexport->clients),
                                 pclient_found, EXPORT_OPTION_ROOT))
            return EXPORT_PERMISSION_GRANTED;
          /* else, check if any access only export matches this client */
          if(proc_makes_write)
            {
              if (export_client_match(addr, ipstring, &(pexport->clients), pclient_found, EXPORT_OPTION_WRITE_ACCESS))
                return EXPORT_PERMISSION_GRANTED;
              else if (pexport->new_access_list_version && export_client_match(addr, ipstring,
                                           &(pexport->clients), pclient_found, EXPORT_OPTION_MD_WRITE_ACCESS))
                {
                  pexport->access_type = ACCESSTYPE_MDONLY;
                  return EXPORT_MDONLY_GRANTED;
                }
            } else { /* request will not write anything */
            if (export_client_match(addr, ipstring, &(pexport->clients), pclient_found, EXPORT_OPTION_READ_ACCESS))
              return EXPORT_PERMISSION_GRANTED;
            else if (pexport->new_access_list_version && export_client_match(addr, ipstring,
                                         &(pexport->clients), pclient_found, EXPORT_OPTION_MD_READ_ACCESS))
              {
                pexport->access_type = ACCESSTYPE_MDONLY_RO;
                return EXPORT_MDONLY_GRANTED;
              }
          }
        }

      if((user_credentials->caller_uid == 0) &&
         export_client_matchv6(&(psockaddr_in6->sin6_addr), &(pexport->clients),
                               pclient_found, EXPORT_OPTION_ROOT))
        return EXPORT_PERMISSION_GRANTED;
      /* else, check if any access only export matches this client */
      if(proc_makes_write)
        {
          if (export_client_matchv6(&(psockaddr_in6->sin6_addr), &(pexport->clients), pclient_found, EXPORT_OPTION_WRITE_ACCESS))
            return EXPORT_PERMISSION_GRANTED;
          else if (pexport->new_access_list_version && export_client_matchv6(&(psockaddr_in6->sin6_addr),
                                       &(pexport->clients), pclient_found, EXPORT_OPTION_MD_WRITE_ACCESS))
            {
              pexport->access_type = ACCESSTYPE_MDONLY;
              return EXPORT_MDONLY_GRANTED;
            }
        }
      else
        { /* request will not write anything */
          if (export_client_matchv6(&(psockaddr_in6->sin6_addr), &(pexport->clients), pclient_found, EXPORT_OPTION_READ_ACCESS))
            return EXPORT_PERMISSION_GRANTED;
          else if (pexport->new_access_list_version && export_client_matchv6(&(psockaddr_in6->sin6_addr),
                                         &(pexport->clients), pclient_found, EXPORT_OPTION_MD_READ_ACCESS))
            {
              pexport->access_type = ACCESSTYPE_MDONLY_RO;
              return EXPORT_MDONLY_GRANTED;
            }
        }
    }
#endif                          /* _USE_TIRPC_IPV6 */

  /* If this point is reached, no matching entry was found */
  LogFullDebug(COMPONENT_DISPATCH,
               "export permission denied - no matching entry");
  return EXPORT_PERMISSION_DENIED;

}                               /* nfs_export_check_access */

/**
 *
 * nfs_export_create_root_entry: create the root entries for the cached entries.
 *
 * Create the root entries for the cached entries.
 *
 * @param pexportlist [IN]    the export list to be parsed
 * @param ht          [INOUT] the hash table to be used to the cache inode
 *
 * @return TRUE is successfull, FALSE if something wrong occured.
 *
 */
int nfs_export_create_root_entry(exportlist_t * pexportlist, hash_table_t * ht)
{
      exportlist_t *pcurrent = NULL;
      cache_inode_status_t cache_status;
#ifdef _CRASH_RECOVERY_AT_STARTUP
      cache_content_status_t cache_content_status;
#endif
      fsal_status_t fsal_status;
      cache_inode_fsal_data_t fsdata;
      fsal_handle_t fsal_handle;
      fsal_path_t exportpath_fsal;
      fsal_mdsize_t strsize = MNTPATHLEN + 1;
      cache_entry_t *pentry = NULL;

#ifdef _USE_SHARED_FSAL
      unsigned int i = 0 ;
      unsigned int fsalid = 0 ;
      fsal_op_context_t context[NB_AVAILABLE_FSAL];
#else
      fsal_op_context_t context;
#endif

      /* setting the 'small_client' structure */
      small_client_param.lru_param.nb_entry_prealloc = 10;
      small_client_param.lru_param.entry_to_str = local_lru_inode_entry_to_str;
      small_client_param.lru_param.clean_entry = local_lru_inode_clean_entry;
      small_client_param.nb_prealloc_entry = 10;
      small_client_param.nb_pre_parent = 10;
      small_client_param.nb_pre_state_v4 = 10;
      small_client_param.grace_period_link = 0;
      small_client_param.grace_period_attr = 0;
      small_client_param.grace_period_dirent = 0;
      small_client_param.grace_period_attr   = 0;
      small_client_param.grace_period_link   = 0;
      small_client_param.grace_period_dirent = 0;
      small_client_param.expire_type_attr    = CACHE_INODE_EXPIRE_NEVER;
      small_client_param.expire_type_link    = CACHE_INODE_EXPIRE_NEVER;
      small_client_param.expire_type_dirent  = CACHE_INODE_EXPIRE_NEVER;
      small_client_param.use_test_access = 1;
#ifdef _USE_NFS4_ACL
      small_client_param.attrmask = FSAL_ATTR_MASK_V4;
#else
      small_client_param.attrmask = FSAL_ATTR_MASK_V2_V3;
#endif

      /* creating the 'small_client' */
      if(cache_inode_client_init(&small_client, small_client_param, SMALL_CLIENT_INDEX, NULL))
        {
          LogFatal(COMPONENT_INIT,
                   "small cache inode client could not be allocated");
        }
      else
        LogInfo(COMPONENT_INIT,
                "small cache inode client successfully initialized");

      /* creating the datacache client for recovering data cache */
      if(cache_content_client_init
         (&recover_datacache_client,
          nfs_param.cache_layers_param.cache_content_client_param,
          "recovering"))
        {
          LogFatal(COMPONENT_INIT,
                   "cache content client (for datacache recovery) could not be allocated");
        }


      /* Link together the small client and the recover_datacache_client */
      small_client.pcontent_client = (void *)&recover_datacache_client;

      /* Get the context for FSAL super user */
#ifdef _USE_SHARED_FSAL
      for( i = 0 ; i < nfs_param.nb_loaded_fsal ; i++ )
       {
         fsalid = nfs_param.loaded_fsal[i] ;

         FSAL_SetId( fsalid ) ;

           
         fsal_status = FSAL_InitClientContext(&context[fsalid]);
         if(FSAL_IS_ERROR(fsal_status))
          {
            LogCrit(COMPONENT_INIT,
                    "Couldn't get the context for FSAL super user on FSAL %s", FSAL_fsalid2name( fsalid ) );
            return FALSE;
          }
       }
#else
      fsal_status = FSAL_InitClientContext(&context);
      if(FSAL_IS_ERROR(fsal_status))
        {
          LogCrit(COMPONENT_INIT,
                  "Couldn't get the context for FSAL super user");
          return FALSE;
        }
#endif

      /* loop the export list */

      for(pcurrent = pexportlist; pcurrent != NULL; pcurrent = pcurrent->next)
        {
#ifdef _USE_SHARED_FSAL
         FSAL_SetId( pcurrent->fsalid ) ;
#endif 

#ifdef _USE_MFSL_ASYNC
          if(!(pcurrent->options & EXPORT_OPTION_USE_DATACACHE))
            {
              LogFatal(COMPONENT_INIT,
                       "ERROR : the export entry iId=%u, Export Path=%s must have datacache enabled...",
                       pcurrent->id, pcurrent->fullpath);
            }
#endif
 
          /* Build the FSAL path */
          if(FSAL_IS_ERROR((fsal_status = FSAL_str2path(pcurrent->fullpath,
                                                        strsize, &exportpath_fsal))))
            return FALSE;

          /* inits context for the current export entry */

          fsal_status =
              FSAL_BuildExportContext(&pcurrent->FS_export_context, &exportpath_fsal,
                                      pcurrent->FS_specific);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't build export context for %s",
                      pcurrent->fullpath);
              return FALSE;
            }

          /* get the related client context */
#ifdef _USE_SHARED_FSAL
          fsal_status = FSAL_GetClientContext(&context[pcurrent->fsalid], &pcurrent->FS_export_context, 0, 0, NULL, 0 ) ;
#else
          fsal_status = FSAL_GetClientContext(&context, &pcurrent->FS_export_context, 0, 0, NULL, 0 ) ;
#endif

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't get the credentials for FSAL super user");
              return FALSE;
            }

          /* Lookup for the FSAL Path */
#ifdef _USE_SHARED_FSAL
          if(FSAL_IS_ERROR((fsal_status = FSAL_lookupPath(&exportpath_fsal, &context[pcurrent->fsalid], &fsal_handle, NULL))))
#else
          if(FSAL_IS_ERROR((fsal_status = FSAL_lookupPath(&exportpath_fsal, &context, &fsal_handle, NULL))))
#endif
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't access the root of the exported namespace, ExportId=%u Path=%s FSAL_ERROR=(%u,%u)",
                      pcurrent->id, pcurrent->fullpath, fsal_status.major,
                      fsal_status.minor);
              return FALSE;
            }

          /* stores handle to the export entry */

          pcurrent->proot_handle = (fsal_handle_t *) Mem_Alloc(sizeof(fsal_handle_t));

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't allocate memory");
              return FALSE;
            }

          *pcurrent->proot_handle = fsal_handle;

          /* Add this entry to the Cache Inode as a "root" entry */
          fsdata.handle = fsal_handle;
          fsdata.cookie = 0;

          if((pentry = cache_inode_make_root(&fsdata,
                                             pcurrent->cache_inode_policy,
                                             ht,
                                             &small_client,
#ifdef _USE_SHARED_FSAL
                                             &context[pcurrent->fsalid], 
#else
                                             &context, 
#endif
                                             &cache_status)) == NULL)
            {
              LogCrit(COMPONENT_INIT,
                      "Error when creating root cached entry for %s, export_id=%d, cache_status=%d",
                      pcurrent->fullpath, pcurrent->id, cache_status);
              return FALSE;
            }
          else
            LogInfo(COMPONENT_INIT,
                    "Added root entry for path %s on export_id=%d",
                    pcurrent->fullpath, pcurrent->id);

          /* Set the pentry as a referral if needed */
          if(strcmp(pcurrent->referral, ""))
            {
              /* Set the cache_entry object as a referral by setting the 'referral' field */
              pentry->object.dir.referral = pcurrent->referral;
              LogInfo(COMPONENT_INIT, "A referral is set : %s",
                      pentry->object.dir.referral);
            }
#ifdef _CRASH_RECOVERY_AT_STARTUP
          /* Recover the datacache from a previous crah */
          if(pcurrent->options & EXPORT_OPTION_USE_DATACACHE)
            {
              LogEvent(COMPONENT_INIT, "Recovering Data Cache for export id %u",
                       pcurrent->id);
#ifdef _USE_SHARED_FSAL
              if(cache_content_crash_recover
                 (pcurrent->id, &recover_datacache_client, &small_client, ht, &context,
                  &cache_content_status) != CACHE_CONTENT_SUCCESS)
#else
              if(cache_content_crash_recover
                 (pcurrent->id, &recover_datacache_client, &small_client, ht, &context[pcurrent->fsalid],
                  &cache_content_status) != CACHE_CONTENT_SUCCESS)
#endif
                {
                  LogWarn(COMPONENT_INIT,
                          "Datacache for export id %u is not recoverable: error = %d",
                          pcurrent->id, cache_content_status);
                }
            }
#endif
        }

  return TRUE;
}                               /* nfs_export_create_root_entry */

/* cleans up the export content */
int CleanUpExportContext(fsal_export_context_t * p_export_context)
{

  FSAL_CleanUpExportContext(p_export_context);

  return TRUE;
}


/* Frees current export entry and returns next export entry. */
exportlist_t *RemoveExportEntry(exportlist_t * exportEntry)
{
  exportlist_t *next;

  if (exportEntry == NULL)
    return NULL;

  next = exportEntry->next;

  if (exportEntry->proot_handle != NULL)
    Mem_Free(exportEntry->proot_handle);

  Mem_Free(exportEntry);
  return next;
}
