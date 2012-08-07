/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * \file    exports.c
 * \brief   What is needed to parse the exports file.
 *
 * What is needed to parse the exports file.
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
#include "ganesha_rpc.h"
#include "log.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
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

#define LASTDEFAULT 1048576

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
#define CONF_EXPORT_MAX_OFF_WRITE      "MaxOffsetWrite"
#define CONF_EXPORT_MAX_OFF_READ       "MaxOffsetRead"
#define CONF_EXPORT_MAX_CACHE_SIZE     "MaxCacheSize"
#define CONF_EXPORT_REFERRAL           "Referral"
#define CONF_EXPORT_PNFS               "Use_pNFS"
#define CONF_EXPORT_UQUOTA             "User_Quota"
#define CONF_EXPORT_USE_COMMIT                  "Use_NFS_Commit"
#define CONF_EXPORT_USE_GANESHA_WRITE_BUFFER    "Use_Ganesha_Write_Buffer"
#define CONF_EXPORT_USE_FSAL_UP        "Use_FSAL_UP"
#define CONF_EXPORT_FSAL_UP_FILTERS    "FSAL_UP_Filters"
#define CONF_EXPORT_FSAL_UP_TIMEOUT    "FSAL_UP_Timeout"
#define CONF_EXPORT_FSAL_UP_TYPE       "FSAL_UP_Type"
#define CONF_EXPORT_USE_COOKIE_VERIFIER "UseCookieVerifier"
#define CONF_EXPORT_SQUASH             "Squash"

/** @todo : add encrypt handles option */

/* Internal identifiers */
#define FLAG_EXPORT_ID              0x000000001
#define FLAG_EXPORT_PATH            0x000000002
#define FLAG_EXPORT_PSEUDO          0x000000010
#define FLAG_EXPORT_ACCESSTYPE      0x000000020
#define FLAG_EXPORT_ANON_ROOT       0x000000040
#define FLAG_EXPORT_NFS_PROTO       0x000000080
#define FLAG_EXPORT_TRANS_PROTO     0x000000100
#define FLAG_EXPORT_SECTYPE         0x000000200
#define FLAG_EXPORT_MAX_READ        0x000000400
#define FLAG_EXPORT_MAX_WRITE       0x000000800
#define FLAG_EXPORT_PREF_READ       0x000001000
#define FLAG_EXPORT_PREF_WRITE      0x000002000
#define FLAG_EXPORT_PREF_READDIR    0x000004000
#define FLAG_EXPORT_FSID            0x000008000
#define FLAG_EXPORT_NOSUID          0x000010000
#define FLAG_EXPORT_NOSGID          0x000020000
#define FLAG_EXPORT_PRIVILEGED_PORT 0x000040000
#define FLAG_EXPORT_FS_SPECIFIC     0x000100000
#define FLAG_EXPORT_FS_TAG          0x000200000
#define FLAG_EXPORT_MAX_OFF_WRITE   0x000400000
#define FLAG_EXPORT_MAX_OFF_READ    0x000800000
#define FLAG_EXPORT_USE_PNFS        0x002000000
#define FLAG_EXPORT_ACCESS_LIST     0x004000000
#define FLAG_EXPORT_ANON_GROUP      0x010000000
#define FLAG_EXPORT_ALL_ANON        0x020000000
#define FLAG_EXPORT_ANON_USER       0x040000000
#define FLAG_EXPORT_SQUASH          0x080000000
#define FLAG_EXPORT_USE_UQUOTA      0x100000000

/* limites for nfs_ParseConfLine */
/* Used in BuildExportEntry() */
#define EXPORT_MAX_CLIENTS   128        /* number of clients */

struct glist_head exportlist;

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
      if(Argv[output_value] == NULL)
        return -1;

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
      strncpy(Argv[output_value++], p1, MNTNAMLEN);
                   

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

void StrExportOptions(int    option,
                      char * buffer)
{
  char * buf = buffer;

  if((option & EXPORT_OPTION_ROOT) == EXPORT_OPTION_ROOT)
    buf += sprintf(buf, "ROOT ");

  if((option & EXPORT_OPTION_ALL_ANONYMOUS) == EXPORT_OPTION_ALL_ANONYMOUS)
    buf += sprintf(buf, "ALL SQUASH ");

  if((option & EXPORT_OPTION_ACCESS_LIST) == EXPORT_OPTION_ACCESS_LIST)
    buf += sprintf(buf, "ACCESS LIST ");

  if((option & EXPORT_OPTION_RW_ACCESS) == EXPORT_OPTION_RW_ACCESS)
    buf += sprintf(buf, "RW");
  else if((option & EXPORT_OPTION_READ_ACCESS) == EXPORT_OPTION_READ_ACCESS)
    buf += sprintf(buf, "RO");
  else if((option & EXPORT_OPTION_WRITE_ACCESS) == EXPORT_OPTION_WRITE_ACCESS)
    buf += sprintf(buf, "WO");
  else if((option & EXPORT_OPTION_MD_ACCESS) == EXPORT_OPTION_MD_ACCESS)
    buf += sprintf(buf, "MD RW");
  else if((option & EXPORT_OPTION_MD_READ_ACCESS) == EXPORT_OPTION_MD_READ_ACCESS)
    buf += sprintf(buf, "MD RO");
  else if((option & EXPORT_OPTION_MD_WRITE_ACCESS) == EXPORT_OPTION_MD_WRITE_ACCESS)
    buf += sprintf(buf, "MD WO");
  else if((option & EXPORT_OPTION_ACCESS_TYPE) != 0)
    buf += sprintf(buf, "%08x", option & EXPORT_OPTION_ACCESS_TYPE);
  else
    buf += sprintf(buf, "NONE");

  if((option & EXPORT_OPTION_NOSUID) == EXPORT_OPTION_NOSUID)
    buf += sprintf(buf, ", NOSUID");
  if((option & EXPORT_OPTION_NOSGID) == EXPORT_OPTION_NOSGID)
    buf += sprintf(buf, ", NOSUID");

  if((option & EXPORT_OPTION_AUTH_NONE) == EXPORT_OPTION_AUTH_NONE)
    buf += sprintf(buf, ", AUTH_NONE");
  if((option & EXPORT_OPTION_AUTH_UNIX) == EXPORT_OPTION_AUTH_UNIX)
    buf += sprintf(buf, ", AUTH_SYS");
  if((option & EXPORT_OPTION_RPCSEC_GSS_NONE) == EXPORT_OPTION_RPCSEC_GSS_NONE)
    buf += sprintf(buf, ", RPCSEC_GSS_NONE");
  if((option & EXPORT_OPTION_RPCSEC_GSS_INTG) == EXPORT_OPTION_RPCSEC_GSS_INTG)
    buf += sprintf(buf, ", RPCSEC_GSS_INTG");
  if((option & EXPORT_OPTION_RPCSEC_GSS_PRIV) == EXPORT_OPTION_RPCSEC_GSS_PRIV)
    buf += sprintf(buf, ", RPCSEC_GSS_PRIV");

  buf += sprintf(buf, ", ");

  if((option & EXPORT_OPTION_NFSV2) == EXPORT_OPTION_NFSV2)
    buf += sprintf(buf, "2");
  if((option & EXPORT_OPTION_NFSV3) == EXPORT_OPTION_NFSV3)
    buf += sprintf(buf, "3");
  if((option & EXPORT_OPTION_NFSV4) == EXPORT_OPTION_NFSV4)
    buf += sprintf(buf, "4");
  if((option & (EXPORT_OPTION_NFSV2 |
                EXPORT_OPTION_NFSV3 |
                EXPORT_OPTION_NFSV4)) == 0)
    buf += sprintf(buf, "NONE");

  if((option & EXPORT_OPTION_UDP) == EXPORT_OPTION_UDP)
    buf += sprintf(buf, ", UDP");
  if((option & EXPORT_OPTION_TCP) == EXPORT_OPTION_TCP)
    buf += sprintf(buf, ", TCP");

  if((option & EXPORT_OPTION_USE_PNFS) == EXPORT_OPTION_USE_PNFS)
    buf += sprintf(buf, ", PNFS");
  if((option & EXPORT_OPTION_USE_UQUOTA) == EXPORT_OPTION_USE_UQUOTA)
    buf += sprintf(buf, ", UQUOTA");
}

void LogClientListEntry(log_components_t            component,
                        exportlist_client_entry_t * entry)
{
  char perms[1024];
  char addr[INET_ADDRSTRLEN];

  StrExportOptions(entry->options, perms);

  switch(entry->type)
    {
      case HOSTIF_CLIENT:
        if(inet_ntop
           (AF_INET, &(entry->client.hostif.clientaddr),
            addr, INET_ADDRSTRLEN) == NULL)
          {
            strncpy(addr, "Invalid Host address",
                    INET_ADDRSTRLEN);
          }
        LogFullDebug(component,
                     "  %p HOSTIF_CLIENT: %s(%s)",
                     entry, addr, perms);
        return;

      case NETWORK_CLIENT:
        if(inet_ntop
           (AF_INET, &(entry->client.network.netaddr),
            addr, INET_ADDRSTRLEN) == NULL)
          {
            strncpy(addr,
                    "Invalid Network address", MAXHOSTNAMELEN);
          }
        LogFullDebug(component,
                     "  %p NETWORK_CLIENT: %s(%s)",
                     entry, addr, perms);
        return;

      case NETGROUP_CLIENT:
        LogFullDebug(component,
                     "  %p NETWORK_CLIENT: %s(%s)",
                     entry, entry->client.netgroup.netgroupname, perms);
        return;

      case WILDCARDHOST_CLIENT:
        LogFullDebug(component,
                     "  %p WILDCARDHOST_CLIENT: %s(%s)",
                     entry, entry->client.wildcard.wildcard, perms);
        return;

      case GSSPRINCIPAL_CLIENT:
        LogFullDebug(component,
                     "  %p NETWORK_CLIENT: %s(%s)",
                     entry, entry->client.gssprinc.princname, perms);
        return;

      case HOSTIF_CLIENT_V6:
        if(inet_ntop
           (AF_INET6, &(entry->client.hostif.clientaddr6),
            addr, INET_ADDRSTRLEN) == NULL)
          {
            strncpy(addr, "Invalid Host address",
                    INET_ADDRSTRLEN);
          }
        LogFullDebug(component,
                     "  %p HOSTIF_CLIENT_V6: %s(%s)",
                     entry, addr, perms);
        return;

      case BAD_CLIENT:
        LogCrit(component,
                "  %p BAD_CLIENT: <unknown>(%s)",
                entry, perms);
        return;
    }

  LogCrit(component,
          "  %p UNKNOWN_CLIENT_TYPE: %08x(%s)",
          entry, entry->type, perms);
}

int nfs_AddClientsToClientList(exportlist_client_t * clients,
                               int                   new_clients_number,
                               char               ** new_clients_name,
                               int                   option,
                               char                * var_name)
{
  int i;
  unsigned int l;
  char *client_hostname;
  struct addrinfo *info;
  exportlist_client_entry_t *p_client;
  int is_wildcarded_host;
  unsigned long netMask;
  unsigned long netAddr;

  /* It's now time to set the information related to the new clients */
  for(i = 0; i < new_clients_number; i++)
    {
      client_hostname = new_clients_name[i];

      /* Allocate a new export client entry */
      p_client = gsh_calloc(1, sizeof(exportlist_client_entry_t));

      if(p_client == NULL)
        {
          LogCrit(COMPONENT_CONFIG,
                  "Unable to allocate memory for export client %s",
                  client_hostname);
          return ENOMEM;
        }

      /* Set client options */
      p_client->options = option;

      /* using netdb to get information about the hostname */
      if(client_hostname[0] == '@')
        {
          /* Entry is a netgroup definition */
          strncpy(p_client->client.netgroup.netgroupname,
                  client_hostname + 1, MAXHOSTNAMELEN);

          p_client->type = NETGROUP_CLIENT;

          LogDebug(COMPONENT_CONFIG,
                   "entry %d %p: %s to netgroup %s",
                   i, p_client, var_name,
                   p_client->client.netgroup.netgroupname);
        }
      else if( string_contains_slash( client_hostname ) &&
               ( nfs_LookupNetworkAddr( client_hostname,
                                        &netAddr,
                                        &netMask) == 0 ) )
        {
          /* Entry is a network definition */
          p_client->client.network.netaddr = netAddr;
          p_client->client.network.netmask = netMask;
          p_client->type = NETWORK_CLIENT;

          LogDebug(COMPONENT_CONFIG,
                   "entry %d %p: %s to network %s = %d.%d.%d.%d netmask = %d.%d.%d.%d",
                   i, p_client, var_name,
                   client_hostname,
                   (int)(ntohl(p_client->client.network.netaddr) >> 24),
                   (int)((ntohl(p_client->client.network.netaddr) >> 16) & 0xFF),
                   (int)((ntohl(p_client->client.network.netaddr) >> 8) & 0xFF),
                   (int)(ntohl(p_client->client.network.netaddr) & 0xFF),
                   (int)(ntohl(p_client->client.network.netmask) >> 24),
                   (int)((ntohl(p_client->client.network.netmask) >> 16) & 0xFF),
                   (int)((ntohl(p_client->client.network.netmask) >> 8) & 0xFF),
                   (int)(ntohl(p_client->client.network.netmask) & 0xFF));
        }
      else if( getaddrinfo(client_hostname, NULL, NULL, &info) == 0)
        {
          /* Entry is a hostif */
          if(info->ai_family == AF_INET)
            {
              struct in_addr infoaddr = ((struct sockaddr_in *)info->ai_addr)->sin_addr;
              memcpy(&(p_client->client.hostif.clientaddr), &infoaddr,
                     sizeof(struct in_addr));
              p_client->type = HOSTIF_CLIENT;
              LogDebug(COMPONENT_CONFIG,
                       "entry %d %p: %s to client %s = %d.%d.%d.%d",
                       i, p_client, var_name,
                       client_hostname, 
                       (int)(ntohl(p_client->client.hostif.clientaddr) >> 24),
                       (int)((ntohl(p_client->client.hostif.clientaddr) >> 16) & 0xFF),
                       (int)((ntohl(p_client->client.hostif.clientaddr) >> 8) & 0xFF),
                       (int)(ntohl(p_client->client.hostif.clientaddr) & 0xFF));
            }
          else /* AF_INET6 */
            {
              struct in6_addr infoaddr = ((struct sockaddr_in6 *)info->ai_addr)->sin6_addr;
              /* IPv6 address */
              memcpy(&(p_client->client.hostif.clientaddr6), &infoaddr,
                     sizeof(struct in6_addr));
              p_client->type = HOSTIF_CLIENT_V6;
              LogDebug(COMPONENT_CONFIG,
                       "entry %d %p: %s to client %s = IPv6",
                       i, p_client, var_name,
                       client_hostname);
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
              p_client->type = WILDCARDHOST_CLIENT;
              strncpy(p_client->client.wildcard.wildcard, client_hostname,
                      MAXHOSTNAMELEN);

              LogFullDebug(COMPONENT_CONFIG,
                           "entry %d %p: %s to wildcard \"%s\"",
                           i, p_client, var_name,
                           client_hostname);
            }
          else
            {
              /* Last case: type for client could not be identified. This should not occur */
              LogCrit(COMPONENT_CONFIG,
                      "Unsupported type for client %s", client_hostname);

              gsh_free(p_client);
              continue;
            }
        }

      glist_add_tail(&clients->client_list, &p_client->cle_list);
      clients->num_clients++;
    }                           /* for i */

  return 0;                     /* success !! */
}                               /* nfs_AddClientsToClientList */

#define DEFINED_TWICE_WARNING( _str_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ_EXPORT: WARNING: %s defined twice !!! (ignored)", _str_ )

#define DEFINED_CONFLICT_WARNING( _opt1_ , _opt2_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ_EXPORT: WARNING: %s defined when %s was already defined (ignored)", _opt1_ , _opt2_ )

int parseAccessParam(char *var_name, char *var_value,
			    exportlist_t *p_entry, int access_option) {
  int rc;
  char *expended_node_list;

  /* temp array of clients */
  char *client_list[EXPORT_MAX_CLIENTS];
  int idx;
  int count;

  LogFullDebug(COMPONENT_CONFIG,
               "Parsing %s=\"%s\"",
               var_name, var_value);

  /* expends host[n-m] notations */
  count =
    nodelist_common_condensed2extended_nodelist(var_value, &expended_node_list);

  if(count <= 0)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ_EXPORT: ERROR: Invalid format for client list in EXPORT::%s=\"%s\"",
	      var_name, var_value);

      return -1;
    }
  else if(count > EXPORT_MAX_CLIENTS)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Client list too long (%d>%d) in EXPORT::%s=\"%s\"",
	      count, EXPORT_MAX_CLIENTS, var_name, var_value);
      return -1;
    }

  /* allocate clients strings  */
  for(idx = 0; idx < count; idx++)
    {
      client_list[idx] = gsh_malloc(MNTNAMLEN+1);
      if(client_list[idx] == NULL)
        {
          int i;
          for(i = 0; i < idx; i++)
            gsh_free(client_list[i]);
          return -1;
        }
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
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Client list too long (>%d)", count);

      goto out;
    }

  rc = nfs_AddClientsToClientList(&p_entry->clients,
                                  rc,
                                  (char **)client_list,
                                  access_option,
                                  var_name);

  if(rc != 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Invalid client found in EXPORT::%s=\"%s\"",
              var_name, var_value);
    }

 out:

  /* free client strings */
  for(idx = 0; idx < count; idx++)
    gsh_free(client_list[idx]);

  return rc;
}

bool_t fsal_specific_checks(exportlist_t *p_entry)
{
  #ifdef _USE_GPFS
  p_entry->use_fsal_up = TRUE;

  if (strncmp(p_entry->fsal_up_type, "DUMB", 4) != 0)
    {
      LogWarn(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: %s must be \"DUMB\" when using GPFS."
              " Setting it to \"DUMB\"", CONF_EXPORT_FSAL_UP_TYPE);
      strncpy(p_entry->fsal_up_type,"DUMB", 4);
    }
  if (p_entry->use_ganesha_write_buffer != FALSE)
    {
      LogWarn(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: %s must be FALSE when using GPFS. "
              "Setting it to FALSE.", CONF_EXPORT_USE_GANESHA_WRITE_BUFFER);
      p_entry->use_ganesha_write_buffer = FALSE;
    }
  #endif

  return TRUE;
}

/**
 * BuildExportEntry : builds an export entry from configutation file.
 * Don't stop immediately on error,
 * continue parsing the file, for listing other errors.
 */
static int BuildExportEntry(config_item_t block,
                            exportlist_t ** pp_export,
                            struct glist_head * pexportlist)
{
  exportlist_t *p_entry;
  char          perms[1024];
  int i, rc;
  char *var_name;
  char *var_value;
  struct glist_head * glist;

  /* the mandatory options */

  unsigned int mandatory_options =
      (FLAG_EXPORT_ID | FLAG_EXPORT_PATH |
       FLAG_EXPORT_ACCESS_LIST | FLAG_EXPORT_PSEUDO);

  /* the given options */

  unsigned int set_options = 0;

  int err_flag   = FALSE;

  /* allocates export entry */
  p_entry = gsh_calloc(1, sizeof(exportlist_t));

  if(p_entry == NULL)
    return ENOMEM;

  p_entry->status = EXPORTLIST_OK;
  p_entry->anonymous_uid = (uid_t) ANON_UID;
  p_entry->anonymous_gid = (gid_t) ANON_GID;
  p_entry->use_commit = TRUE;
  p_entry->use_ganesha_write_buffer = FALSE;
  p_entry->UseCookieVerifier = TRUE;

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

  p_entry->worker_stats = gsh_calloc(nfs_param.core_param.nb_worker,
                                     sizeof(nfs_worker_stat_t));
  if(p_entry->worker_stats == NULL)
    {
      gsh_free(p_entry);
      return ENOMEM;
    }

  /* by default, we support auth_none and auth_sys */
  p_entry->options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and
     both transport protocols */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV2;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV4;
  p_entry->options |= EXPORT_OPTION_UDP | EXPORT_OPTION_TCP;

  p_entry->filesystem_id.major = 666;
  p_entry->filesystem_id.minor = 666;

  p_entry->MaxWrite = 16384;
  p_entry->MaxRead = 16384;
  p_entry->PrefWrite = 16384;
  p_entry->PrefRead = 16384;
  p_entry->PrefReaddir = 16384;

  init_glist(&p_entry->clients.client_list);
  init_glist(&p_entry->exp_state_list);
#ifdef _USE_NLM
  init_glist(&p_entry->exp_lock_list);
#endif

  if(pthread_mutex_init(&p_entry->exp_state_mutex, NULL) == -1)
    {
      RemoveExportEntry(p_entry);
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: could not initialize exp_state_mutex");
      /* free the entry before exiting */
      return -1;
    }

  strcpy(p_entry->FS_specific, "");
  strcpy(p_entry->FS_tag, "");
  strcpy(p_entry->fullpath, "/");
  strcpy(p_entry->dirname, "/");
  strcpy(p_entry->fsname, "");
  strcpy(p_entry->pseudopath, "/");
  strcpy(p_entry->referral, "");

  /* parse options for this export entry */

  for(i = 0; i < config_GetNbItems(block); i++)
    {
      config_item_t item;

      item = config_GetItemByIndex(block, i);

      /* get var name and value */
      rc = config_GetKeyValue(item, &var_name, &var_value);

      if((rc != 0) || (var_value == NULL))
        {
          /* free the entry before exiting */
          RemoveExportEntry(p_entry);
          if(rc == -2)
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ_EXPORT: ERROR: var name \"%s\" was truncated",
                    var_name);
          else if(rc == -3)
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ_EXPORT: ERROR: var value for \"%s\"=\"%s\" was truncated",
                    var_name, var_value);
          else
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ_EXPORT: ERROR: internal error %d", rc);
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

          if(nfs_Get_export_by_id(pexportlist,
                                  p_entry->id) != NULL)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Duplicate Export_id: \"%ld\"",
                      export_id);
              err_flag = TRUE;
              continue;
            }

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
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_ROOT);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
          set_options |= FLAG_EXPORT_ACCESS_LIST;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
          parseAccessParam(var_name, var_value, p_entry,
                           EXPORT_OPTION_ACCESS_LIST);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_MD_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_RO_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_MD_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READ_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READWRITE_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_RW_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;
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

          p_entry->options &= ~EXPORT_OPTION_ACCESS_TYPE;

          if(!STRCMP(var_value, "RW"))
            {
              p_entry->options |= EXPORT_OPTION_RW_ACCESS;
            }
          else if(!STRCMP(var_value, "RO"))
            {
              p_entry->options |= EXPORT_OPTION_READ_ACCESS;
            }
          else if(!STRCMP(var_value, "MDONLY"))
            {
              p_entry->options |= EXPORT_OPTION_MD_ACCESS;
            }
          else if(!STRCMP(var_value, "MDONLY_RO"))
            {
              p_entry->options |= EXPORT_OPTION_MD_READ_ACCESS;
            }
          else if(!STRCMP(var_value, "NONE"))
            {
              LogFullDebug(COMPONENT_CONFIG,
                           "Export access type NONE");
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid access type \"%s\". Values can be: RW, RO, MDONLY, MDONLY_RO, NONE.",
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
            nfsvers_list[idx] = gsh_malloc(MAX_NFSPROTO_LEN);

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
                if(nfsvers_list[idx] != NULL)
                  gsh_free(nfsvers_list[idx]);

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
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ_EXPORT:NFS version 2 is disabled in NFS_Core_Param.");
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "3"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
                    p_entry->options |= EXPORT_OPTION_NFSV3;
                  else
                    {
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ_EXPORT:NFS version 3 is disabled in NFS_Core_Param.");
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "4"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
                    p_entry->options |= EXPORT_OPTION_NFSV4;
                  else
                    {
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ_EXPORT:NFS version 4 is disabled in NFS_Core_Param.");
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

          /* free vers strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            if(nfsvers_list[idx] != NULL)
              gsh_free(nfsvers_list[idx]);

          /* check that at least one nfs protocol has been specified */
          if((p_entry->options & (EXPORT_OPTION_NFSV2
                                  | EXPORT_OPTION_NFSV3 | EXPORT_OPTION_NFSV4)) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Empty NFS_protocols list");
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
            transproto_list[idx] = gsh_malloc(MAX_TRANSPROTO_LEN);

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
                if(transproto_list[idx] != NULL)
                  gsh_free(transproto_list[idx]);

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
                          "NFS READ_EXPORT: ERROR: Invalid transport \"%s\". Values can be: UDP, TCP.",
                          transproto_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_TRANSPROTO; idx++)
            if(transproto_list[idx] != NULL)
              gsh_free(transproto_list[idx]);

          /* check that at least one TRANS protocol has been specified */
          if((p_entry->options & (EXPORT_OPTION_UDP | EXPORT_OPTION_TCP)) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Empty transport list");
              err_flag = TRUE;
            }

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
            p_entry->options |= EXPORT_OPTION_ALL_ANONYMOUS;

          set_options |= FLAG_EXPORT_ANON_USER;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_ROOT))
        {
          long int anon_uid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_ANON_ROOT);
              continue;
            }

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_ANON_ROOT, CONF_EXPORT_ANON_USER);
              continue;
            }

          if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_ANON_ROOT, CONF_EXPORT_SQUASH);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid %s: \"%s\"",
                      var_name, var_value);
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

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_ANON_USER, CONF_EXPORT_ANON_ROOT);
              continue;
            }

          if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_ANON_USER, CONF_EXPORT_SQUASH);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid %s: \"%s\"",
                      var_name, var_value);
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
                      "NFS READ_EXPORT: ERROR: Invalid %s: \"%s\"",
                      var_name, var_value);
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
            sec_list[idx] = gsh_malloc(MAX_SECTYPE_LEN);

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
                if(sec_list[idx] != NULL)
                  gsh_free(sec_list[idx]);

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
            if(sec_list[idx] != NULL)
              gsh_free(sec_list[idx]);

          /* check that at least one sectype has been specified */
          if((p_entry->options & (EXPORT_OPTION_AUTH_NONE
                                  | EXPORT_OPTION_AUTH_UNIX
                                  | EXPORT_OPTION_RPCSEC_GSS_NONE
                                  | EXPORT_OPTION_RPCSEC_GSS_INTG
                                  | EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0)
            LogWarn(COMPONENT_CONFIG,
                    "NFS READ_EXPORT: WARNING: Empty SecType");

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
          LogInfo(COMPONENT_CONFIG,
                  "Deprecated EXPORT option %s ignored",
                  var_name);
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
      else if(!STRCMP(var_name, CONF_EXPORT_UQUOTA ) )
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_UQUOTA) == FLAG_EXPORT_USE_UQUOTA)
            {
              DEFINED_TWICE_WARNING("FLAG_EXPORT_USE_UQUOTA");
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_USE_UQUOTA;
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
          set_options |= EXPORT_OPTION_USE_UQUOTA;

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
          LogInfo(COMPONENT_CONFIG,
                  "Deprecated EXPORT option %s ignored",
                  var_name);
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
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COOKIE_VERIFIER))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->UseCookieVerifier = TRUE;
              break;

            case 0:
              p_entry->UseCookieVerifier = FALSE;
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
      else if(!STRCMP(var_name, CONF_EXPORT_SQUASH))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH)
            {
              DEFINED_TWICE_WARNING(CONF_EXPORT_SQUASH);
              continue;
            }

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_SQUASH, CONF_EXPORT_ANON_ROOT);
              continue;
            }

          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_CONFLICT_WARNING(CONF_EXPORT_SQUASH, CONF_EXPORT_ANON_USER);
              continue;
            }

          if(!STRCMP(var_value, "Root") ||
             !STRCMP(var_value, "Root_Squash") ||
             !STRCMP(var_value, "RootSquash"))
            {
              /* Nothing to do, default is root squash */
            }
          else if(!STRCMP(var_value, "All") ||
                  !STRCMP(var_value, "All_Squash") ||
                  !STRCMP(var_value, "AllSquash"))
            {
              /* Squash all users */
              p_entry->options |= EXPORT_OPTION_ALL_ANONYMOUS;
            }
          else if(!STRCMP(var_value, "No_Root_Squash") ||
                  !STRCMP(var_value, "None") ||
                  !STRCMP(var_value, "NoIdSquash"))
            {
              /* Allow Root access */
              p_entry->options |= EXPORT_OPTION_ROOT;
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): "
                      "Root, Root_Squash, RootSquash,"
                      "All, All_Squash, AllSquash,"
                      "No_Root_Squash, NoIdSquash, or None expected.",
                      var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          set_options |= FLAG_EXPORT_SQUASH;
        }
      else
        {
          LogWarn(COMPONENT_CONFIG,
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

      if((set_options & FLAG_EXPORT_ACCESS_LIST) != FLAG_EXPORT_ACCESS_LIST)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Must have at least one of %s, %s, %s, %s, %s, or %s",
                CONF_EXPORT_ACCESS,      CONF_EXPORT_ROOT,
                CONF_EXPORT_READ_ACCESS, CONF_EXPORT_READWRITE_ACCESS,
                CONF_EXPORT_MD_ACCESS,   CONF_EXPORT_MD_RO_ACCESS);

      if((set_options & FLAG_EXPORT_PSEUDO) != FLAG_EXPORT_PSEUDO)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ_EXPORT: ERROR: Missing mandatory parameter %s",
                CONF_EXPORT_PSEUDO);

      err_flag = TRUE;
    }

  /* check if there had any error.
   * if so, free the p_entry and return an error.
   */
  if(err_flag)
    {
      RemoveExportEntry(p_entry);
      return -1;
    }

  /* Here we can make sure certain options are turned on for specific FSALs */
  if (!fsal_specific_checks(p_entry))
    {
      LogCrit(COMPONENT_CONFIG,
               "NFS READ_EXPORT: ERROR: Found conflicts in export entry.");
      return -1;
    }

  *pp_export = p_entry;

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  StrExportOptions(p_entry->options, perms);
  LogFullDebug(COMPONENT_CONFIG,
               "  Export Perms: %s", perms);

  glist_for_each(glist, &p_entry->clients.client_list)
    {
      exportlist_client_entry_t * p_client_entry;
      p_client_entry = glist_entry(glist, exportlist_client_entry_t, cle_list);

      LogClientListEntry(COMPONENT_CONFIG, p_client_entry);
    }

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
  p_entry = gsh_calloc(1, sizeof(exportlist_t));

  if(p_entry == NULL)
    return NULL;

  /** @todo set default values here */

  p_entry->status = EXPORTLIST_OK;
  p_entry->anonymous_uid = (uid_t) ANON_UID;

  /* By default, export is RW */
  p_entry->options |= EXPORT_OPTION_RW_ACCESS;

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

  p_entry->UseCookieVerifier = TRUE;

  init_glist(&p_entry->clients.client_list);
  init_glist(&p_entry->exp_state_list);
#ifdef _USE_NLM
  init_glist(&p_entry->exp_lock_list);
#endif

  /**
   * Grant root access to all clients
   */
  rc = nfs_AddClientsToClientList(&p_entry->clients,
                                  1,
                                  client_root_access,
                                  EXPORT_OPTION_ROOT,
                                  CONF_EXPORT_ROOT);

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

void RemoveExportEntry(exportlist_t * p_entry)
{
  struct glist_head * glist;
  struct glist_head * glistn;

  glist_for_each_safe(glist, glistn, &p_entry->clients.client_list)
    {
       exportlist_client_entry_t * p_client;
       p_client = glist_entry(glist, exportlist_client_entry_t, cle_list);
       glist_del(&p_client->cle_list);
       gsh_free(p_client);
    }

  if(p_entry->proot_handle != NULL)
    {
      gsh_free(p_entry->proot_handle);
    }

  if(p_entry->worker_stats != NULL)
    {
      gsh_free(p_entry->worker_stats);
    }

  glist_del(&p_entry->exp_list);
 
  gsh_free(p_entry);
}

/**
 * ReadExports:
 * Read the export entries from the parsed configuration file.
 * \return A negative value on error,
 *         the number of export entries else.
 */
int ReadExports(config_file_t in_config,        /* The file that contains the export list */
                struct glist_head * pexportlist)   /* Pointer to the export list */
{

  int nb_blk, rc, i;
  char *blk_name;
  int err_flag = FALSE;

  exportlist_t *p_export_item = NULL;

  int nb_entries = 0;

  if(!pexportlist)
    return -EFAULT;

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

          rc = BuildExportEntry(block, &p_export_item, pexportlist);

          /* If the entry is errorneous, ignore it
           * and continue checking syntax of other entries.
           */
          if(rc != 0)
            {
              err_flag = TRUE;
              continue;
            }

          glist_add_tail(pexportlist, &p_export_item->exp_list);

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
  unsigned int        i = 0;
  int                 rc;
  char                hostname[MAXHOSTNAMELEN];
  in_addr_t           addr = get_in_addr(hostaddr);
  struct glist_head * glist;

  if(isFullDebug(COMPONENT_DISPATCH))
    {
      char * root_access = "";
      char * read_access = "";
      char * write_access = "";
      char * md_read_access = "";
      char * md_write_access = "";
      char * access_list = "";

      if(export_option & EXPORT_OPTION_ROOT)
        root_access = " ROOT";
      if(export_option & EXPORT_OPTION_READ_ACCESS)
        read_access = " READ";
      if(export_option & EXPORT_OPTION_WRITE_ACCESS)
        write_access = " WRITE";
      if(export_option & EXPORT_OPTION_MD_READ_ACCESS)
        md_read_access = " MD-READ";
      if(export_option & EXPORT_OPTION_MD_WRITE_ACCESS)
        md_write_access = " MD-WRITE";
      if(export_option & EXPORT_OPTION_ACCESS_LIST)
        access_list = " ACCESS-LIST";

      if((export_option & (EXPORT_OPTION_ROOT            |
                           EXPORT_OPTION_READ_ACCESS     |
                           EXPORT_OPTION_WRITE_ACCESS    |
                           EXPORT_OPTION_MD_WRITE_ACCESS |
                           EXPORT_OPTION_MD_READ_ACCESS  |
                           EXPORT_OPTION_ACCESS_LIST)) == 0)
        root_access = " NONE";

      LogFullDebug(COMPONENT_DISPATCH,
                   "Checking client %s for%s%s%s%s%s%s clients=%p",
                   ipstring,
                   root_access, read_access, write_access,
                   md_read_access, md_write_access, access_list,
                   clients);
    }

  glist_for_each(glist, &clients->client_list)
    {
       exportlist_client_entry_t * p_client;
       p_client = glist_entry(glist, exportlist_client_entry_t, cle_list);
       i++;

      /* Make sure the client entry has the permission flags we're looking for. */
      if((p_client->options & export_option) != export_option)
        continue;

      switch (p_client->type)
        {
        case HOSTIF_CLIENT:
          LogFullDebug(COMPONENT_DISPATCH,
                       "Test HOSTIF_CLIENT: Test entry %d: clientaddr %d.%d.%d.%d, match with %d.%d.%d.%d",
                       i,
                       (int)(ntohl(p_client->client.hostif.clientaddr) >> 24),
                       (int)((ntohl(p_client->client.hostif.clientaddr) >> 16) & 0xFF),
                       (int)((ntohl(p_client->client.hostif.clientaddr) >> 8) & 0xFF),
                       (int)(ntohl(p_client->client.hostif.clientaddr) & 0xFF),
                       (int)(ntohl(addr) >> 24),
                       (int)(ntohl(addr) >> 16) & 0xFF,
                       (int)(ntohl(addr) >> 8) & 0xFF,
                       (int)(ntohl(addr) & 0xFF));
          if(p_client->client.hostif.clientaddr == addr)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches host address for entry %u",
                           i);
              *pclient_found = *p_client;
              return TRUE;
            }
          break;

        case NETWORK_CLIENT:
          LogFullDebug(COMPONENT_DISPATCH,
                       "Test NETWORK_CLIENT: Test net %d.%d.%d.%d mask %d.%d.%d.%d, match with %d.%d.%d.%d",
                       (int)(ntohl(p_client->client.network.netaddr) >> 24),
                       (int)((ntohl(p_client->client.network.netaddr) >> 16) & 0xFF),
                       (int)((ntohl(p_client->client.network.netaddr) >> 8) & 0xFF),
                       (int)(ntohl(p_client->client.network.netaddr) & 0xFF),
                       (int)(ntohl(p_client->client.network.netmask) >> 24),
                       (int)((ntohl(p_client->client.network.netmask) >> 16) & 0xFF),
                       (int)((ntohl(p_client->client.network.netmask) >> 8) & 0xFF),
                       (int)(ntohl(p_client->client.network.netmask) & 0xFF),
                       (int)(ntohl(addr) >> 24),
                       (int)(ntohl(addr) >> 16) & 0xFF,
                       (int)(ntohl(addr) >> 8) & 0xFF,
                       (int)(ntohl(addr) & 0xFF));

          if((p_client->client.network.netmask & ntohl(addr)) ==
             p_client->client.network.netaddr)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches network address for entry %u",
                           i);
              *pclient_found = *p_client;
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
             (p_client->client.netgroup.netgroupname, hostname,
              NULL, NULL) == 1)
            {
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches netgroup for entry %u",
                           i);
              return TRUE;
            }
          break;

        case WILDCARDHOST_CLIENT:
          /* Now checking for IP wildcards */
          if(fnmatch
             (p_client->client.wildcard.wildcard, ipstring,
              FNM_PATHNAME) == 0)
            {
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches wildcard for entry %u",
                           i);
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
                      LogInfo(COMPONENT_DISPATCH,
                              "Could not resolve hostame for addr %d.%d.%d.%d ... not checking if a hostname wildcard matches",
                              (int)(ntohl(addr) & 0xFF),
                              (int)(ntohl(addr) >> 8) & 0xFF,
                              (int)(ntohl(addr) >> 16) & 0xFF,
                              (int)(ntohl(addr) >> 24));
                      break;
                    }
                }
            }
          LogFullDebug(COMPONENT_DISPATCH,
                       "Wildcarded hostname: testing if '%s' matches '%s'",
                       hostname, p_client->client.wildcard.wildcard);

          /* At this point 'hostname' should contain the name that was found */
          if(fnmatch
             (p_client->client.wildcard.wildcard, hostname,
              FNM_PATHNAME) == 0)
            {
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches wildcard for entry %u",
                           i);
              return TRUE;
            }
          LogFullDebug(COMPONENT_DISPATCH, "'%s' not matching '%s'",
                       hostname, p_client->client.wildcard.wildcard);
          break;

        case GSSPRINCIPAL_CLIENT:
          /** @toto BUGAZOMEU a completer lors de l'integration de RPCSEC_GSS */
          LogFullDebug(COMPONENT_DISPATCH,
                       "----------> Unsupported type GSS_PRINCIPAL_CLIENT");
          return FALSE;
          break;

       case BAD_CLIENT:
          LogCrit(COMPONENT_DISPATCH,
                  "Bad client in position %u seen in export list", i );
	  continue ;

        default:
           LogCrit(COMPONENT_DISPATCH,
                   "Unsupported client in position %u in export list with type %u", i, p_client->type);
	   continue ;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;

}                               /* export_client_match */

#ifdef _USE_TIRPC_IPV6
int export_client_matchv6(struct in6_addr *paddrv6,
			  exportlist_client_t *clients,
			  exportlist_client_entry_t * pclient_found,
			  unsigned int export_option)
{
  struct glist_head * glist;

  if(export_option & EXPORT_OPTION_ROOT)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for root access entries");

  if(export_option & EXPORT_OPTION_READ_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for nonroot access read entries");

  if(export_option & EXPORT_OPTION_WRITE_ACCESS)
    LogFullDebug(COMPONENT_DISPATCH,
                 "Looking for nonroot access write entries");

  glist_for_each(glist, &clients->client_list)
    {
       exportlist_client_entry_t * p_client;
       p_client = glist_entry(glist, exportlist_client_entry_t, cle_list);

      /* Make sure the client entry has the permission flags we're looking for. */
      if((p_client->options & export_option) != export_option)
        continue;

      switch (p_client->type)
        {
        case HOSTIF_CLIENT:
        case NETWORK_CLIENT:
        case NETGROUP_CLIENT:
        case WILDCARDHOST_CLIENT:
        case GSSPRINCIPAL_CLIENT:
          continue;

        case HOSTIF_CLIENT_V6:
          if(!memcmp(p_client->client.hostif.clientaddr6.s6_addr, paddrv6->s6_addr, 16))  /* Remember that IPv6 address are 128 bits = 16 bytes long */
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches host adress in IPv6");
              *pclient_found = *p_client;
              return TRUE;
            }
          break;

        default:
          break;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;
}                               /* export_client_matchv6 */
#endif

int export_client_match_any(sockaddr_t                * hostaddr,
                            char                      * ipstring,
                            exportlist_client_t       * clients,
                            exportlist_client_entry_t * pclient_found,
                            unsigned int                export_option)
{
#ifdef _USE_TIRPC_IPV6
  if(hostaddr->ss_family == AF_INET6)
    {
      struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *) hostaddr;
      return export_client_matchv6(&(psockaddr_in6->sin6_addr),
                                   clients,
                                   pclient_found,
                                   export_option);
    }
  else
#endif
    {
      return export_client_match(hostaddr,
                                 ipstring,
                                 clients,
                                 pclient_found,
                                 export_option);
    }
}

/**
 * nfs_export_check_security: checks if request security flavor is suffcient for the requested export
 *
 * Checks if request security flavor is suffcient for the requested export
 *
 * @param ptr_req       [IN]    pointer to the related RPC request.
 * @param pexpprt       [IN]    related export entry (if found, NULL otherwise).
 *
 * @return TRUE if the request flavor exists in the matching export
 * FALSE otherwise
 *
 */
int nfs_export_check_security(struct svc_req *req, exportlist_t * pexport)
{
  switch (req->rq_cred.oa_flavor)
    {
      case AUTH_NONE:
        if((pexport->options & EXPORT_OPTION_AUTH_NONE) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_NONE",
                    pexport->dirname);
            return FALSE;
          }
        break;

      case AUTH_UNIX:
        if((pexport->options & EXPORT_OPTION_AUTH_UNIX) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_UNIX",
                    pexport->dirname);
            return FALSE;
          }
        break;

#ifdef _HAVE_GSSAPI
      case RPCSEC_GSS:
        if((pexport->options &
           (EXPORT_OPTION_RPCSEC_GSS_NONE |
            EXPORT_OPTION_RPCSEC_GSS_INTG |
            EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support RPCSEC_GSS",
                    pexport->dirname);
            return FALSE;
          }
        else
          {
            struct svc_rpc_gss_data *gd;
            rpc_gss_svc_t svc;
            gd = SVCAUTH_PRIVATE(req->rq_xprt->xp_auth);
            svc = gd->sec.svc;
            LogFullDebug(COMPONENT_DISPATCH,
                         "Testing svc %d", (int) svc);
            switch(svc)
              {
                case RPCSEC_GSS_SVC_NONE:
                  if((pexport->options &
                      EXPORT_OPTION_RPCSEC_GSS_NONE) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_NONE",
                              pexport->dirname);
                      return FALSE;
                    }
                  break;

                case RPCSEC_GSS_SVC_INTEGRITY:
                  if((pexport->options &
                      EXPORT_OPTION_RPCSEC_GSS_INTG) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_INTEGRITY",
                              pexport->dirname);
                      return FALSE;
                    }
                  break;

                case RPCSEC_GSS_SVC_PRIVACY:
                  if((pexport->options &
                      EXPORT_OPTION_RPCSEC_GSS_PRIV) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_PRIVACY",
                              pexport->dirname);
                      return FALSE;
                    }
                  break;

                  default:
                    LogInfo(COMPONENT_DISPATCH,
                            "Export %s does not support unknown "
                            "RPCSEC_GSS_SVC %d",
                            pexport->dirname, (int) svc);
                    return FALSE;
              }
          }
      break;
#endif
      default:
        LogInfo(COMPONENT_DISPATCH,
                "Export %s does not support unknown oa_flavor %d",
                pexport->dirname, (int) req->rq_cred.oa_flavor);
        return FALSE;
    }

  return TRUE;
}

/**
 * nfs_export_check_access: checks if a machine is authorized to access an export entry.
 *
 * Checks if a machine is authorized to access an export entry.
 *
 * @param ssaddr        [IN]    the complete remote address (as a sockaddr_storage to be IPv6 compliant)
 * @param pexpprt       [IN]    related export entry (if found, NULL otherwise).
 * @param pexport_perms [OUT]   pointer to export permissions matching client.
                                pexport_perms->options will be 0 if client is denied
 *
 */

char ten_bytes_all_0[10];

sockaddr_t * check_convert_ipv6_to_ipv4(sockaddr_t * ipv6, sockaddr_t *ipv4)
{
#ifdef _USE_TIRPC_IPV6
  struct sockaddr_in  * paddr = (struct sockaddr_in *)ipv4;
  struct sockaddr_in6 * psockaddr_in6 = (struct sockaddr_in6 *)ipv6;

  /* If the client socket is IPv4, then it is wrapped into a ::ffff:a.b.c.d
   * IPv6 address. We check this here.
   * This kind of adress is shaped like this:
   * |---------------------------------------------------------------|
   * |   80 bits = 10 bytes  | 16 bits = 2 bytes | 32 bits = 4 bytes |
   * |---------------------------------------------------------------|
   * |            0          |        FFFF       |    IPv4 address   |
   * |---------------------------------------------------------------|   */
  if((ipv6->ss_family == AF_INET6) &&
     !memcmp(psockaddr_in6->sin6_addr.s6_addr, ten_bytes_all_0, 10) &&
     (psockaddr_in6->sin6_addr.s6_addr16[5] == 0xFFFF))
    {
      memset(ipv4, 0, sizeof(*ipv4));

      paddr->sin_port        = psockaddr_in6->sin6_port;
      paddr->sin_addr.s_addr = psockaddr_in6->sin6_addr.s6_addr32[3];
      ipv4->ss_family        = AF_INET;

      if(isFullDebug(COMPONENT_DISPATCH))
        {
          char ipstring4[SOCK_NAME_MAX];
          char ipstring6[SOCK_NAME_MAX];

          sprint_sockaddr(ipv6, ipstring6, sizeof(ipstring6));
          sprint_sockaddr(ipv4, ipstring4, sizeof(ipstring4));
          LogFullDebug(COMPONENT_DISPATCH,
                       "Converting IPv6 encapsulated IPv4 address %s to IPv4 %s",
                       ipstring6, ipstring4);
        }

      return ipv4;
    }
  else
    {
      return ipv6;
    }
#else
  return ipv4;
#endif
}

void nfs_export_check_access(sockaddr_t     * hostaddr,
                             exportlist_t   * pexport,
                             export_perms_t * pexport_perms)
{
  char ipstring[SOCK_NAME_MAX];
  int ipvalid;
  exportlist_client_entry_t client_found;
  sockaddr_t alt_hostaddr;
  sockaddr_t * puse_hostaddr;

  /* Initialize permissions to allow nothing */
  pexport_perms->options       = 0;
  pexport_perms->anonymous_uid = (uid_t) ANON_UID;
  pexport_perms->anonymous_gid = (gid_t) ANON_GID;

  puse_hostaddr = check_convert_ipv6_to_ipv4(hostaddr, &alt_hostaddr);

  ipstring[0] = '\0';
  ipvalid = sprint_sockip(puse_hostaddr, ipstring, sizeof(ipstring));

  /* Use IP address as a string for wild character access checks. */
  if(!ipvalid)
    {
      LogCrit(COMPONENT_DISPATCH,
              "Could not convert the IP address to a character string.");
      return;
    }

  LogFullDebug(COMPONENT_DISPATCH,
               "Check for address %s", ipstring);

  if(pexport == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
              "No export to check permission against");
      return;
    }

  /* Now that export is verified, grab anonymous uid and gid from export. */
  pexport_perms->anonymous_uid = pexport->anonymous_uid;
  pexport_perms->anonymous_gid = pexport->anonymous_gid;

  /* Test if client is in Root_Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_ROOT))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches Root_Access",
                   pexport->id, ipstring);

      pexport_perms->options |= client_found.options;
    }

  /* Continue on to see if client matches any other kind of access list */

  /* Test if client is in RW_Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_WRITE_ACCESS))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches RW_Access",
                   pexport->id, ipstring);

      pexport_perms->options |= client_found.options;

      return;
    }

  /* Test if client is in R_Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_READ_ACCESS))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches R_Access",
                   pexport->id, ipstring);

      pexport_perms->options |= client_found.options;

      return;
    }

  /* Test if client is in MDONLY_Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_MD_WRITE_ACCESS))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches MDONLY_Access",
                   pexport->id, ipstring);

      pexport_perms->options |= EXPORT_OPTION_MD_ACCESS;

      return;
    }

  /* Test if client is in MDONLY_RO_Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_MD_READ_ACCESS))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches MDONLY_RO_Access",
                   pexport->id, ipstring);

      pexport_perms->options |= EXPORT_OPTION_MD_READ_ACCESS;

      return;
    }

  /* Test if client is in Access list */
  if(export_client_match_any(puse_hostaddr,
                             ipstring,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_ACCESS_LIST))
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches Access",
                   pexport->id, ipstring);

      pexport_perms->options |= pexport->options & EXPORT_OPTION_ALL_ACCESS;

      return;
    }

  /* Client is in no other list, check if it was in Root_Access list above */
  if(pexport_perms->options & EXPORT_OPTION_ROOT)
    {
      /* Root_Access is specified, but no other access list matched.
       * Use base export access, and make sure at least READ access is granted.
       */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Export %d Client %s matches Root_Access and no other, granting export access (at least Read)",
                   pexport->id, ipstring);

      pexport_perms->options |= (pexport->options &
                                EXPORT_OPTION_ALL_ACCESS) |
                                EXPORT_OPTION_READ_ACCESS;

      return;
    }

  /* If this point is reached, no matching entry was found */
  LogFullDebug(COMPONENT_DISPATCH,
               "export %d permission denied - no matching entry",
               pexport->id);

  return;
}                               /* nfs_export_check_access */

/**
 *
 * nfs_export_create_root_entry: create the root entries for the cached entries.
 *
 * Create the root entries for the cached entries.
 *
 * @param pexportlist [IN]    the export list to be parsed
 *
 * @return TRUE is successfull, FALSE if something wrong occured.
 *
 */
int nfs_export_create_root_entry(struct glist_head * pexportlist)
{
      exportlist_t *pcurrent = NULL;
      struct glist_head * glist;
      struct glist_head * glistn;
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

      fsal_op_context_t context;
      fsal_staticfsinfo_t *pstaticinfo = NULL;
      fsal_export_context_t *export_context = NULL;

      /* Get the context for FSAL super user */
      fsal_status = FSAL_InitClientContext(&context);
      if(FSAL_IS_ERROR(fsal_status))
        {
          LogCrit(COMPONENT_INIT,
                  "Couldn't get the context for FSAL super user");
          return FALSE;
        }

      /* loop the export list */
      glist_for_each_safe(glist, glistn, pexportlist)
        {
          pcurrent = glist_entry(glist, exportlist_t, exp_list);

          /* Build the FSAL path */
          if(FSAL_IS_ERROR((fsal_status = FSAL_str2path(pcurrent->fullpath,
                                                        strsize, &exportpath_fsal))))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't build FSAL path for %s, removing export id %u",
                      pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          /* inits context for the current export entry */

          fsal_status =
              FSAL_BuildExportContext(&pcurrent->FS_export_context, &exportpath_fsal,
                                      pcurrent->FS_specific);

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't build export context for %s, removing export id %u",
                      pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          /* get the related client context */
          fsal_status = FSAL_GetClientContext(&context, &pcurrent->FS_export_context, 0, 0, NULL, 0 ) ;

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't get the credentials for FSAL super user for %s, removing export id %u",
                      pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          /* Lookup for the FSAL Path */
          if(FSAL_IS_ERROR((fsal_status = FSAL_lookupPath(&exportpath_fsal, &context, &fsal_handle, NULL))))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't access the root of the exported namespace, FSAL_ERROR=(%u,%u) for %s, removing export id %u",
                      fsal_status.major, fsal_status.minor,
                      pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          /* stores handle to the export entry */

          pcurrent->proot_handle = gsh_malloc(sizeof(fsal_handle_t));

          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't allocate memory for %s, removing export id %u",
                      pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          *pcurrent->proot_handle = fsal_handle;
          export_context = &pcurrent->FS_export_context;
          pstaticinfo = export_context->fe_static_fs_info;
          if( ((pcurrent->options & EXPORT_OPTION_MAXREAD) != EXPORT_OPTION_MAXREAD )) 
             {
               if ( pstaticinfo && pstaticinfo->maxread )
                  pcurrent->MaxRead = pstaticinfo->maxread;
               else
                  pcurrent->MaxRead = LASTDEFAULT;
             }
          if( ((pcurrent->options & EXPORT_OPTION_MAXWRITE) != EXPORT_OPTION_MAXWRITE )) 
             {
               if ( pstaticinfo && pstaticinfo->maxwrite )
                  pcurrent->MaxWrite = pstaticinfo->maxwrite;
               else
                  pcurrent->MaxWrite = LASTDEFAULT;
             }
          LogFullDebug(COMPONENT_INIT,
                      "Set MaxRead MaxWrite for Path=%s Options = 0x%x MaxRead = 0x%llX MaxWrite = 0x%llX",
                      pcurrent->fullpath, pcurrent->options,
                      (long long) pcurrent->MaxRead,
                      (long long) pcurrent->MaxWrite);
             
          /* Add this entry to the Cache Inode as a "root" entry */
          fsdata.fh_desc.start = (caddr_t) &fsal_handle;
          fsdata.fh_desc.len = 0;
	  (void) FSAL_ExpandHandle(
#ifdef _USE_SHARED_FSAL
		                   context[pcurrent->fsalid].export_context,
#else
				   context.export_context,
#endif
				   FSAL_DIGEST_SIZEOF,
				   &fsdata.fh_desc);

          /* cache_inode_make_root returns a cache_entry with
             reference count of 2, where 1 is the sentinel value of
             a cache entry in the hash table.  The export list in
             this case owns the extra reference, but other users of
             cache_inode_make_root MUST put the entry.  In the future
             if functionality is added to dynamically add and remove
             export entries, then the function to remove an export
             entry MUST put the extra reference. */

          if((pentry = cache_inode_make_root(&fsdata,
                                             &context,
                                             &cache_status)) == NULL)
            {
              LogCrit(COMPONENT_INIT,
                      "Error %s when creating root cached entry for %s, removing export id %u",
                      cache_inode_err_str(cache_status), pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
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
        }

  /* Note: As mentioned above, we are returning with an extra
     reference to the root entry.  This reference is owned by the
     export list.  If we ever have a function to remove objects from
     the export list, it must return this extra reference. */

  return TRUE;

} /* nfs_export_create_root_entry */

/* cleans up the export content */
int CleanUpExportContext(fsal_export_context_t * p_export_context)
{

  FSAL_CleanUpExportContext(p_export_context);

  return TRUE;
}


/* Frees current export entry and returns next export entry. */
exportlist_t *GetExportEntry(char *exportPath)
{
  exportlist_t *p_current_item = NULL;
  struct glist_head * glist;
  char tmplist_path[MAXPATHLEN];
  char tmpexport_path[MAXPATHLEN];
  int found = 0;

  /*
   * Find the export for the dirname (using as well Path or Tag )
   */
  glist_for_each(glist, nfs_param.pexportlist)
    {
    p_current_item = glist_entry(glist, exportlist_t, exp_list);

    LogDebug(COMPONENT_CONFIG, "full path %s, export path %s",
             p_current_item->fullpath, exportPath);

    /* Make sure the path in export entry ends with a '/', if not adds one */
    if(p_current_item->fullpath[strlen(p_current_item->fullpath) - 1] == '/')
      strncpy(tmplist_path, p_current_item->fullpath, MAXPATHLEN);
    else
      snprintf(tmplist_path, MAXPATHLEN, "%s/", p_current_item->fullpath);

    /* Make sure that the argument from MNT ends with a '/', if not adds one */
    if(exportPath[strlen(exportPath) - 1] == '/')
      strncpy(tmpexport_path, exportPath, MAXPATHLEN);
    else
      snprintf(tmpexport_path, MAXPATHLEN, "%s/", exportPath);

    /* Is tmplist_path a subdirectory of tmpexport_path ? */
    if(!strncmp(tmplist_path, tmpexport_path, strlen(tmplist_path)))
    {
      found = 1;
      break;
    }
  }

  if(found)
    {
      LogDebug(COMPONENT_CONFIG, "returning export %s", p_current_item->fullpath);
      return p_current_item;
    }
  else
    {
      LogDebug(COMPONENT_CONFIG, "returning export NULL");
      return NULL;
    }
}
