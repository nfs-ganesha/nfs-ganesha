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
#ifdef _USE_NODELIST
#include "nodelist.h"
#endif
#include <stdlib.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <ctype.h>

#define LASTDEFAULT 1048576

#define STRCMP strcasecmp

char * CONF_LABEL_EXPORT = "EXPORT";
char * CONF_LABEL_EXPORT_CLIENT = "EXPORT_CLIENT";

/* Labels in the export file */
#define CONF_EXPORT_ID                 "Export_id"
#define CONF_EXPORT_PATH               "Path"
#define CONF_EXPORT_ROOT               "Root_Access"
#define CONF_EXPORT_ACCESS             "Access"
#define CONF_EXPORT_READ_ACCESS        "R_Access"
#define CONF_EXPORT_READWRITE_ACCESS   "RW_Access"
#define CONF_EXPORT_NETBIOS_NAME       "Strip_NETBIOS_Name"
#define CONF_EXPORT_MD_ACCESS          "MDONLY_Access"
#define CONF_EXPORT_MD_RO_ACCESS       "MDONLY_RO_Access"
#define CONF_EXPORT_PSEUDO             "Pseudo"
#define CONF_EXPORT_ACCESSTYPE         "Access_Type"
#define CONF_EXPORT_ANON_USER          "Anonymous_uid"
#define CONF_EXPORT_ANON_ROOT          "Anonymous_root_uid"
#define CONF_EXPORT_ALL_ANON           "Make_All_Users_Anonymous"
#define CONF_EXPORT_ANON_GROUP         "Anonymous_gid"
#define CONF_EXPORT_SQUASH             "Squash"
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

/** @todo : add encrypt handles option */

/* Internal identifiers */
#define FLAG_EXPORT_ID              0x000000001
#define FLAG_EXPORT_PATH            0x000000002
#define FLAG_EXPORT_PSEUDO          0x000000004
#define FLAG_EXPORT_ACCESSTYPE      0x000000008
#define FLAG_EXPORT_ANON_USER       0x000000010
#define FLAG_EXPORT_ANON_ROOT       0x000000020
#define FLAG_EXPORT_ALL_ANON        0x000000040
#define FLAG_EXPORT_ANON_GROUP      0x000000080
#define FLAG_EXPORT_SQUASH          0x000000100
#define FLAG_EXPORT_NFS_PROTO       0x000000200
#define FLAG_EXPORT_TRANS_PROTO     0x000000400
#define FLAG_EXPORT_SECTYPE         0x000000800
#define FLAG_EXPORT_MAX_READ        0x000001000
#define FLAG_EXPORT_MAX_WRITE       0x000002000
#define FLAG_EXPORT_PREF_READ       0x000004000
#define FLAG_EXPORT_PREF_WRITE      0x000008000
#define FLAG_EXPORT_PREF_READDIR    0x000010000
#define FLAG_EXPORT_FSID            0x000020000
#define FLAG_EXPORT_NOSUID          0x000040000
#define FLAG_EXPORT_NOSGID          0x000080000
#define FLAG_EXPORT_PRIVILEGED_PORT 0x000100000
#define FLAG_EXPORT_FS_SPECIFIC     0x000200000
#define FLAG_EXPORT_FS_TAG          0x000400000
#define FLAG_EXPORT_MAX_OFF_WRITE   0x000800000
#define FLAG_EXPORT_MAX_OFF_READ    0x001000000
#define FLAG_EXPORT_USE_PNFS        0x002000000
#define FLAG_EXPORT_ACCESS_LIST     0x004000000
#define FLAG_EXPORT_USE_UQUOTA      0x008000000
#define FLAG_EXPORT_NETBIOS_NAME    0x010000000


/* limites for nfs_ParseConfLine */
/* Used in BuildExportEntry() */
#define EXPORT_MAX_CLIENTS   128        /* number of clients */

/* exports by id cache */
export_by_id_t export_by_id;

struct glist_head exportlist;

/**
 * nfs_ParseConfLine: parse a line with a settable separator and  end of line
 *
 * parse a line with a settable separator and  end of line .
 *
 * @param Argv               [OUT] result array
 * @param nbArgv             [IN]  allocated number of entries in the Argv
 * @param size               [IN]  maximum buffer size of tokens
 * @param line               [IN]  input line
 * @param separator          [IN]  character used to identify a separator
 *
 * @return the number of object found
 *
 * NOTE: line is modified, returned tokens are returned as pointers to
 *       the null terminated string within the original copy of line.
 *
 *       size includes the null terminator, if size is 0, caller doesn't
 *       care about size (can't be larger than input string anyway).
 *
 */
int nfs_ParseConfLine(char *Argv[],
                      int nbArgv,
                      size_t size,
                      char *line,
                      char separator)
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
      for(p2 = p1; (*p2 != separator) && (*p2 != '\0'); p2++) ;

      /* Test for end of line */
      if(*p2 == '\0')
        endLine = TRUE;

      /* terminate the token */
      *p2 = '\0';

      /* if the token is too large for buffer, return failure */
      if((size != 0) && ((p2 - p1) >= size))
        return -3;

      /* Put token pointer into list.
       * NOTE: we do NOT copy the string, the token points to the bytes in then
       *       input string that have just been null terminated.
       */
      Argv[output_value] = p1;

      output_value++;

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

  /* no end of line detected */
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

int display_export_perms(struct display_buffer * dspbuf,
                         export_perms_t        * p_perms)
{
  int b_left = display_start(dspbuf);

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_ROOT) == EXPORT_OPTION_ROOT)
    b_left = display_cat(dspbuf, "ROOT ");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_ALL_ANONYMOUS) == EXPORT_OPTION_ALL_ANONYMOUS)
    b_left = display_cat(dspbuf, "ALL SQUASH ");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_ACCESS_LIST) == EXPORT_OPTION_ACCESS_LIST)
    b_left = display_cat(dspbuf, "ACCESS LIST ");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_RW_ACCESS) == EXPORT_OPTION_RW_ACCESS)
    b_left = display_cat(dspbuf, "RW");
  else if((p_perms->options & EXPORT_OPTION_READ_ACCESS) == EXPORT_OPTION_READ_ACCESS)
    b_left = display_cat(dspbuf, "RO");
  else if((p_perms->options & EXPORT_OPTION_WRITE_ACCESS) == EXPORT_OPTION_WRITE_ACCESS)
    b_left = display_cat(dspbuf, "WO");
  else if((p_perms->options & EXPORT_OPTION_MD_ACCESS) == EXPORT_OPTION_MD_ACCESS)
    b_left = display_cat(dspbuf, "MD RW");
  else if((p_perms->options & EXPORT_OPTION_MD_READ_ACCESS) == EXPORT_OPTION_MD_READ_ACCESS)
    b_left = display_cat(dspbuf, "MD RO");
  else if((p_perms->options & EXPORT_OPTION_MD_WRITE_ACCESS) == EXPORT_OPTION_MD_WRITE_ACCESS)
    b_left = display_cat(dspbuf, "MD WO");
  else if((p_perms->options & EXPORT_OPTION_ACCESS_TYPE) != 0)
    b_left = display_printf(dspbuf, "%08x", p_perms->options & EXPORT_OPTION_ACCESS_TYPE);
  else
    b_left = display_cat(dspbuf, "NONE");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_NOSUID) == EXPORT_OPTION_NOSUID)
    b_left = display_cat(dspbuf, ", NOSUID");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_NOSGID) == EXPORT_OPTION_NOSGID)
    b_left = display_cat(dspbuf, ", NOSUID");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_AUTH_NONE) == EXPORT_OPTION_AUTH_NONE)
    b_left = display_cat(dspbuf, ", AUTH_NONE");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_AUTH_UNIX) == EXPORT_OPTION_AUTH_UNIX)
    b_left = display_cat(dspbuf, ", AUTH_SYS");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_NONE) == EXPORT_OPTION_RPCSEC_GSS_NONE)
    b_left = display_cat(dspbuf, ", RPCSEC_GSS_NONE");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_INTG) == EXPORT_OPTION_RPCSEC_GSS_INTG)
    b_left = display_cat(dspbuf, ", RPCSEC_GSS_INTG");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_PRIV) == EXPORT_OPTION_RPCSEC_GSS_PRIV)
    b_left = display_cat(dspbuf, ", RPCSEC_GSS_PRIV");

  if(b_left <= 0)
    return b_left;

  b_left = display_cat(dspbuf, ", ");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_NFSV2) == EXPORT_OPTION_NFSV2)
    b_left = display_cat(dspbuf, "2");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_NFSV3) == EXPORT_OPTION_NFSV3)
    b_left = display_cat(dspbuf, "3");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_NFSV4) == EXPORT_OPTION_NFSV4)
    b_left = display_cat(dspbuf, "4");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & (EXPORT_OPTION_NFSV2 |
                EXPORT_OPTION_NFSV3 |
                EXPORT_OPTION_NFSV4)) == 0)
    b_left = display_cat(dspbuf, "NONE");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_UDP) == EXPORT_OPTION_UDP)
    b_left = display_cat(dspbuf, ", UDP");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_TCP) == EXPORT_OPTION_TCP)
    b_left = display_cat(dspbuf, ", TCP");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_USE_PNFS) == EXPORT_OPTION_USE_PNFS)
    b_left = display_cat(dspbuf, ", PNFS");

  if(b_left <= 0)
    return b_left;

  if((p_perms->options & EXPORT_OPTION_USE_UQUOTA) == EXPORT_OPTION_USE_UQUOTA)
    b_left = display_cat(dspbuf, ", UQUOTA");

  if(b_left <= 0)
    return b_left;

  b_left = display_printf(dspbuf, ", anon_uid=%d",
                          (int)p_perms->anonymous_uid);

  if(b_left <= 0)
    return b_left;

  return display_printf(dspbuf, ", anon_gid=%d",
                        (int)p_perms->anonymous_gid);
}

void LogClientListEntry(log_components_t            component,
                        exportlist_client_entry_t * entry)
{
  char                    perms[1024];
  struct display_buffer   dspbuf = {sizeof(perms), perms, perms};
  char                    addr[INET6_ADDRSTRLEN];
  char                  * paddr = addr;

  if(isFullDebug(component))
    (void) display_export_perms(&dspbuf, &entry->client_perms);

  switch(entry->type)
    {
      case HOSTIF_CLIENT:
        if(inet_ntop
           (AF_INET, &(entry->client.hostif.clientaddr),
            addr, sizeof(addr)) == NULL)
          {
            paddr = "Invalid Host address";
          }
        LogFullDebug(component,
                     "  %p HOSTIF_CLIENT: %s(%s)",
                     entry, paddr, perms);
        return;

      case NETWORK_CLIENT:
        if(inet_ntop
           (AF_INET, &(entry->client.network.netaddr),
            addr, sizeof(addr)) == NULL)
          {
            paddr = "Invalid Network address";
          }
        LogFullDebug(component,
                     "  %p NETWORK_CLIENT: %s(%s)",
                     entry, paddr, perms);
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
            addr, sizeof(addr)) == NULL)
          {
            paddr = "Invalid Host address";
          }
        LogFullDebug(component,
                     "  %p HOSTIF_CLIENT_V6: %s(%s)",
                     entry, paddr, perms);
        return;

      case MATCH_ANY_CLIENT:
        LogFullDebug(component,
                     "  %p MATCH_ANY_CLIENT: *(%s)",
                     entry, perms);
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
      p_client->client_perms.options = option;

      /* using netdb to get information about the hostname */
      if((client_hostname[0] == '*') && (client_hostname[1] == '\0'))
        {
          p_client->type = MATCH_ANY_CLIENT;
          LogDebug(COMPONENT_CONFIG,
                   "entry %d %p: %s is match any client",
                   i, p_client, var_name);
        }
      else if(client_hostname[0] == '@')
        {
          /* Entry is a netgroup definition */
          if(strmaxcpy(p_client->client.netgroup.netgroupname,
                      client_hostname + 1,
                      sizeof(p_client->client.netgroup.netgroupname)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "netgroup %s too long, ignoring",
                      client_hostname);
              gsh_free(p_client);
              continue;
            }

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
              if(strmaxcpy(p_client->client.wildcard.wildcard,
                           client_hostname,
                           sizeof(p_client->client.wildcard.wildcard)) == -1)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "host wildcard %s too long, ignoring",
                          client_hostname);
                  gsh_free(p_client);
                  continue;
                }

              LogFullDebug(COMPONENT_CONFIG,
                           "entry %d %p: %s to wildcard \"%s\"",
                           i, p_client, var_name,
                           client_hostname);
            }
          else
            {
              /* Last case: client could not be identified, DNS failed. */
              LogCrit(COMPONENT_CONFIG,
                      "Unknown client %s (DNS failed)",
                      client_hostname);

              gsh_free(p_client);
              continue;
            }
        }

      glist_add_tail(&clients->client_list, &p_client->cle_list);
      clients->num_clients++;
    }                           /* for i */

  return 0;                     /* success !! */
}                               /* nfs_AddClientsToClientList */

#define DEFINED_TWICE_WARNING( _lbl_ , _str_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ %s: %s defined twice !!! (ignored)", \
          _lbl_ , _str_ )

#define DEFINED_CONFLICT_WARNING( _lbl_ , _opt1_ , _opt2_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ %s: %s defined when %s was already defined (ignored)", \
          _lbl_ , _opt1_ , _opt2_ )

int parseAccessParam(char                * var_name,
                     char                * var_value,
                     exportlist_client_t * clients,
                     int                   access_option,
                     const char          * label)
{
  int rc;
  char *expanded_node_list;

  /* temp array of clients */
  char *client_list[EXPORT_MAX_CLIENTS];
  int count;

  LogFullDebug(COMPONENT_CONFIG,
               "Parsing %s=\"%s\"",
               var_name, var_value);

#ifdef _USE_NODELIST
  /* expends host[n-m] notations */
  count =
    nodelist_common_condensed2extended_nodelist(var_value, &expanded_node_list);

  if(count <= 0)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Invalid format for client list in %s=\"%s\"",
	      label, var_name, var_value);

      return -1;
    }
  else if(count > EXPORT_MAX_CLIENTS)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ %s: Client list too long (%d>%d) in %s=\"%s\"",
	      label, count, EXPORT_MAX_CLIENTS, var_name, var_value);
      return -1;
    }
#else
  expanded_node_list = var_value;
  count = EXPORT_MAX_CLIENTS;
#endif

  /* fill client list with NULL pointers */
  memset(client_list, 0, sizeof(client_list));

  /*
   * Search for coma-separated list of hosts, networks and netgroups
   */
  rc = nfs_ParseConfLine(client_list,
                         count,
                         0,
			 expanded_node_list,
			 ',');

  if(rc < 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ %s: Client list too long (>%d)",
              label, count);

      goto out;
    }

  rc = nfs_AddClientsToClientList(clients,
                                  rc,
                                  (char **)client_list,
                                  access_option,
                                  var_name);

  if(rc != 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ %s: Invalid client found in %s=\"%s\"",
              label, var_name, var_value);
    }

 out:

#ifdef _USE_NODELIST
  /* free the buffer the nodelist module has allocated */
  free(expanded_node_list);
#endif

  return rc;
}

bool_t fsal_specific_checks(exportlist_t *p_entry)
{
  #if defined(_USE_GPFS) || defined (_USE_PT)
  if (p_entry->use_ganesha_write_buffer != FALSE)
    {
      LogWarn(COMPONENT_CONFIG,
              "NFS READ EXPORT: %s must be FALSE when using GPFS. "
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
 *
 * If label == CONF_LABEL_EXPORT build a base export entry
 * If label == CONF_LABEL_EXPORT_CLIENT add a client access list to export entry
 */
static int BuildExportEntry(config_item_t        block,
                            exportlist_t      ** pp_export,
                            struct glist_head  * pexportlist,
                            const char         * label)
{
  exportlist_t        * p_entry = NULL;
  exportlist_t        * p_found_entry = NULL;
  int                   i, rc;
  char                * var_name;
  char                * var_value;
  struct glist_head   * glist;
  exportlist_client_t   access_list;
  exportlist_client_t * p_access_list;
  export_perms_t        client_perms;
  export_perms_t      * p_perms;
  char                  temp_path[MAXPATHLEN+2];
  char                * ppath;
  unsigned int          mandatory_options;
  int                   err_flag = FALSE;
  unsigned int          set_options = 0;

  /* allocates export entry */
  if(label == CONF_LABEL_EXPORT)
    {
      p_entry = gsh_calloc(1, sizeof(exportlist_t));

      if(p_entry == NULL)
        return ENOMEM;

      /* the mandatory options */
      mandatory_options = FLAG_EXPORT_ID |
                          FLAG_EXPORT_PATH;

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
      strcpy(p_entry->fsal_up_type,"DUMB");
      /* We don't create the thread until all exports are parsed. */
      memset(&p_entry->fsal_up_thr, 0, sizeof(pthread_t));
#endif /* _USE_FSAL_UP */

#ifdef _USE_STAT_EXPORTER
      p_entry->worker_stats = gsh_calloc(nfs_param.core_param.nb_worker,
                                         sizeof(nfs_worker_stat_t));
      if(p_entry->worker_stats == NULL)
        {
          gsh_free(p_entry);
          return ENOMEM;
        }
#endif

      p_perms = &p_entry->export_perms;

      p_entry->filesystem_id.major = 666;
      p_entry->filesystem_id.minor = 666;

      p_entry->MaxWrite = 16384;
      p_entry->MaxRead = 16384;
      p_entry->PrefWrite = 16384;
      p_entry->PrefRead = 16384;
      p_entry->PrefReaddir = 16384;

      p_access_list = &p_entry->clients;

      init_glist(&p_entry->exp_state_list);
#ifdef _USE_NLM
      init_glist(&p_entry->exp_lock_list);
#endif

      if(pthread_mutex_init(&p_entry->exp_state_mutex, NULL) == -1)
        {
          RemoveExportEntry(p_entry);
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ %s: could not initialize exp_state_mutex",
                  label);
          /* free the entry before exiting */
          return -1;
        }

      strcpy(p_entry->FS_specific, "");
      strcpy(p_entry->FS_tag, "");
      strcpy(p_entry->fullpath, "/");
      strcpy(p_entry->pseudopath, "/");
      strcpy(p_entry->referral, "");
    }
  else
    {
      /* the mandatory options */
      mandatory_options = FLAG_EXPORT_ACCESS_LIST;

      p_perms = &client_perms;
      p_access_list = &access_list;
    }

  /* Init the access list */
  init_glist(&p_access_list->client_list);
  p_access_list->num_clients = 0;

  /* by default, we support auth_none and auth_sys */
  p_perms->options = EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* Default anonymous uid and gid */
  p_perms->anonymous_uid = (uid_t) ANON_UID;
  p_perms->anonymous_gid = (gid_t) ANON_GID;

  /* by default, we support all NFS versions supported by the core and
   * both transport protocols
   */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
    p_perms->options |= EXPORT_OPTION_NFSV2;

  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_perms->options |= EXPORT_OPTION_NFSV3;

  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_perms->options |= EXPORT_OPTION_NFSV4;

  p_perms->options |= EXPORT_OPTION_TRANSPORTS;

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
          if(label == CONF_LABEL_EXPORT)
            RemoveExportEntry(p_entry);
          else
            FreeClientList(p_access_list);

          if(rc == -2)
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ %s: var name \"%s\" was truncated",
                    label, var_name);
          else if(rc == -3)
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ %s: var value for \"%s\"=\"%s\" was truncated",
                    label, var_name, var_value);
          else
            LogCrit(COMPONENT_CONFIG,
                    "NFS READ %s: internal error %d",
                    label, rc);
          return -1;
        }

      if(!STRCMP(var_name, CONF_EXPORT_ID))
        {
          exportlist_t * p_fe;
          long int       export_id;
          char         * end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ID) == FLAG_EXPORT_ID)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ID);
              continue;
            }
          
          set_options |= FLAG_EXPORT_ID;

          /* parse and check export_id */
          errno = 0;
          export_id = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid export_id: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(export_id <= 0 || export_id > USHRT_MAX)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Export_id out of range: \"%ld\"",
                      label, export_id);
              err_flag = TRUE;
              continue;
            }

          /* set export_id */

          if((label == CONF_LABEL_EXPORT_CLIENT) &&
             (p_entry != NULL) &&
             (export_id != p_entry->id))
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Export_id: \"%ld\" does not match export %s Export_Id %u",
                      label, export_id,
                      p_entry->fullpath, p_entry->id);
              err_flag = TRUE;
              continue;
            }

          p_fe = nfs_Get_export_by_id(pexportlist, export_id);

          if(label == CONF_LABEL_EXPORT)
            {
              if(p_fe != NULL)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: Duplicate Export_id: \"%ld\"",
                          label, export_id);
                  err_flag = TRUE;
                  continue;
                }
              p_entry->id = (unsigned short)export_id;
            }
          else if(p_entry == NULL)
            {
              if(p_fe == NULL)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: EXPORT for Export_id: \"%ld\" not found",
                          label, export_id);
                  err_flag = TRUE;
                  continue;
                }
              p_entry = p_fe;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PATH))
        {
          exportlist_t * p_fe;
          int            pathlen;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PATH) == FLAG_EXPORT_PATH)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PATH);
              continue;
            }

          set_options |= FLAG_EXPORT_PATH;

          if(*var_value == '\0')
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Empty export path",
                      label);
              err_flag = TRUE;
              continue;
            }

          pathlen = strlen(var_value);

          if(pathlen >= MAXPATHLEN)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          /* Make sure fullpath ends with '/' */
          if(var_value[pathlen-1] != '/')
            {
              if(strmaxcpy(temp_path,
                           var_value,
                           sizeof(temp_path) - 1) == -1)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: %s \"%s\" too long",
                          label, var_name, var_value);
                  err_flag = TRUE;
                  continue;
                }
              temp_path[pathlen]   = '/';
              temp_path[pathlen+1] = '\0';
              ppath = temp_path;
            }
          else
            {
              ppath = var_value;
            }

          if((label == CONF_LABEL_EXPORT_CLIENT) &&
             (p_entry != NULL) &&
             (strcmp(ppath, p_entry->fullpath) != 0))
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Path: \"%s\" does not match export Export_Id %u Path=\"%s\"",
                      label, ppath,
                      p_entry->id, p_entry->fullpath);
              err_flag = TRUE;
              continue;
            }

          p_fe = nfs_Get_export_by_path(pexportlist, ppath);
          if(label == CONF_LABEL_EXPORT)
            {
              /* Pseudo, Tag, and Export_Id must be unique, Path may be
               * duplicated if at least Tag or Pseudo is specified (and
               * unique).
               */
              if(p_fe != NULL && p_found_entry != NULL)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: Duplicate Path: \"%s\"",
                          label, ppath);
                  err_flag = TRUE;
                  continue;
                }

              if(strmaxcpy(p_entry->fullpath,
                           ppath,
                           sizeof(p_entry->fullpath)) == -1)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: %s \"%s\" too long",
                          label, var_name, var_value);
                  err_flag = TRUE;
                  continue;
                }

              /* Remember the entry we found so we can verify Tag and/or Pseudo
               * is set by the time the EXPORT stanza is complete.
               */
              p_found_entry = p_fe;
            }
          else if(p_entry == NULL)
            {
              if(p_fe == NULL)
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: EXPORT for Path: \"%s\" not found",
                          label, ppath);
                  err_flag = TRUE;
                  continue;
                }

              p_entry = p_fe;
            }

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ROOT))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
          set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(*var_value == '\0')
            {
              continue;
            }

	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_ROOT,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESS))
        {
          int access_option;

	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(*var_value == '\0')
            {
              continue;
            }

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              access_option = EXPORT_OPTION_ACCESS_OPT_LIST;
              p_perms->options |= EXPORT_OPTION_ACCESS_OPT_LIST;
            }
          else
            {
              access_option = EXPORT_OPTION_ACCESS_LIST;
            }

          parseAccessParam(var_name, var_value, p_access_list,
                           access_option,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_ACCESS))
        {
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_MD_ACCESS,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_RO_ACCESS))
        {
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_MD_READ_ACCESS,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READ_ACCESS))
        {
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_READ_ACCESS,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READWRITE_ACCESS))
        {
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_RW_ACCESS,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NETBIOS_NAME))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NETBIOS_NAME) == FLAG_EXPORT_NETBIOS_NAME)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_NETBIOS_NAME);
              continue;
            }

          set_options |= FLAG_EXPORT_NETBIOS_NAME;

          if(*var_value == '\0')
            {
              continue;
            }
          parseAccessParam(var_name, var_value, p_access_list,
                           EXPORT_OPTION_NETBIOS_NAME,
                           label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PSEUDO))
        {
          exportlist_t * p_fe;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PSEUDO) == FLAG_EXPORT_PSEUDO)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PSEUDO);
              continue;
            }

          set_options |= FLAG_EXPORT_PSEUDO;

          if(*var_value != '/')
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Pseudo path must begin with a slash (invalid pseudo path: %s).",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          p_fe = nfs_Get_export_by_pseudo(pexportlist, var_value);
          if(p_fe != NULL)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Duplicate Pseudo: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(strmaxcpy(p_entry->pseudopath,
                       var_value,
                       sizeof(p_entry->pseudopath)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          p_perms->options |= EXPORT_OPTION_PSEUDO;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_REFERRAL))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(strmaxcpy(p_entry->referral,
                       var_value,
                       sizeof(p_entry->referral)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESSTYPE))
        {
          // check if it has not already been set
          if((set_options & FLAG_EXPORT_ACCESSTYPE) == FLAG_EXPORT_ACCESSTYPE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ACCESSTYPE);
              continue;
            }

          set_options |= FLAG_EXPORT_ACCESSTYPE;

          p_perms->options &= ~EXPORT_OPTION_ACCESS_TYPE;

          if(!STRCMP(var_value, "RW"))
            {
              p_perms->options |= EXPORT_OPTION_RW_ACCESS |
                                  EXPORT_OPTION_MD_ACCESS;
            }
          else if(!STRCMP(var_value, "RO"))
            {
              p_perms->options |= EXPORT_OPTION_READ_ACCESS |
                                  EXPORT_OPTION_MD_READ_ACCESS;
            }
          else if(!STRCMP(var_value, "MDONLY"))
            {
              p_perms->options |= EXPORT_OPTION_MD_ACCESS;
            }
          else if(!STRCMP(var_value, "MDONLY_RO"))
            {
              p_perms->options |= EXPORT_OPTION_MD_READ_ACCESS;
            }
          else if(!STRCMP(var_value, "NONE"))
            {
              LogFullDebug(COMPONENT_CONFIG,
                           "Export access type NONE");
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid access type \"%s\". Values can be: RW, RO, MDONLY, MDONLY_RO, NONE.",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

        }
      else if(!STRCMP(var_name, CONF_EXPORT_NFS_PROTO))
        {

#     define MAX_NFSPROTO      10       /* large enough !!! */

          char *nfsvers_list[MAX_NFSPROTO];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NFS_PROTO) == FLAG_EXPORT_NFS_PROTO)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_NFS_PROTO);
              continue;
            }

          set_options |= FLAG_EXPORT_NFS_PROTO;

          /* reset nfs proto flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_PROTOCOLS;

          /* fill nfs vers list with NULL pointers */
          memset(nfsvers_list, 0, sizeof(nfsvers_list));

          /*
           * Search for coma-separated list of nfsprotos
           */
          count = nfs_ParseConfLine(nfsvers_list,
                                    MAX_NFSPROTO,
                                    0,
                                    var_value,
                                    ',');

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: NFS protocols list too long (>%d)",
                      label, MAX_NFSPROTO);

              continue;
            }

          /* add each Nfs protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(nfsvers_list[idx], "2"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
                    p_perms->options |= EXPORT_OPTION_NFSV2;
                  else
                    {
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ %s: NFS version 2 is disabled in NFS_Core_Param.",
                              label);
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "3"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
                    p_perms->options |= EXPORT_OPTION_NFSV3;
                  else
                    {
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ %s: NFS version 3 is disabled in NFS_Core_Param.",
                              label);
                    }
                }
              else if(!STRCMP(nfsvers_list[idx], "4"))
                {
                  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
                    p_perms->options |= EXPORT_OPTION_NFSV4;
                  else
                    {
                      LogInfo(COMPONENT_CONFIG,
                              "NFS READ %s: NFS version 4 is disabled in NFS_Core_Param.",
                              label);
                    }
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: Invalid NFS version \"%s\". Values can be: 2, 3, 4.",
                          label, nfsvers_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* check that at least one nfs protocol has been specified */
          if((p_perms->options & EXPORT_OPTION_PROTOCOLS) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Empty NFS_protocols list",
                      label);
              err_flag = TRUE;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_TRANS_PROTO))
        {

#     define MAX_TRANSPROTO      10     /* large enough !!! */

          char *transproto_list[MAX_TRANSPROTO];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_TRANS_PROTO) == FLAG_EXPORT_TRANS_PROTO)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_TRANS_PROTO);
              continue;
            }

          /* reset TRANS proto flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_TRANSPORTS;

          /* fill TRANS vers list with NULL pointers */
          memset(transproto_list, 0, sizeof(transproto_list));

          /*
           * Search for coma-separated list of TRANSprotos
           */
          count = nfs_ParseConfLine(transproto_list,
                                    MAX_TRANSPROTO,
                                    0,
                                    var_value,
                                    ',');

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Protocol list too long (>%d)",
                      label, MAX_TRANSPROTO);

              continue;
            }

          /* add each TRANS protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(transproto_list[idx], "UDP"))
                {
                  p_perms->options |= EXPORT_OPTION_UDP;
                }
              else if(!STRCMP(transproto_list[idx], "TCP"))
                {
                  p_perms->options |= EXPORT_OPTION_TCP;
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: Invalid transport \"%s\". Values can be: UDP, TCP.",
                          label, transproto_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* check that at least one TRANS protocol has been specified */
          if((p_perms->options & EXPORT_OPTION_TRANSPORTS) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Empty transport list",
                      label);
              err_flag = TRUE;
            }

          set_options |= FLAG_EXPORT_TRANS_PROTO;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ALL_ANON))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ALL_ANON) == FLAG_EXPORT_ALL_ANON)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ALL_ANON);
              continue;
            }

          set_options |= FLAG_EXPORT_ALL_ANON;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ALL_ANON,
                                       CONF_EXPORT_SQUASH);
              continue;
            }

          if (StrToBoolean(var_value))
            p_perms->options |= EXPORT_OPTION_ALL_ANONYMOUS;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_ROOT))
        {
          long int anon_uid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ANON_ROOT);
              continue;
            }

          set_options |= FLAG_EXPORT_ANON_ROOT;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ANON_ROOT,
                                       CONF_EXPORT_ANON_USER);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid %s: \"%s\"",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */
          p_perms->anonymous_uid = (uid_t) anon_uid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_USER))
        {
          long int anon_uid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ANON_USER);
              continue;
            }

          set_options |= FLAG_EXPORT_ANON_USER;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ANON_USER,
                                       CONF_EXPORT_ANON_ROOT);
              continue;
            }

          /* parse and check anon_uid */
          errno = 0;

          anon_uid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid %s: \"%s\"",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */

          p_perms->anonymous_uid = (uid_t) anon_uid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_GROUP))
        {

          long int anon_gid;
          char *end_ptr;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_ANON_GROUP) == FLAG_EXPORT_ANON_GROUP)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_ANON_GROUP);
              continue;
            }

          set_options |= FLAG_EXPORT_ANON_GROUP;

          /* parse and check anon_uid */
          errno = 0;

          anon_gid = strtol(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid %s: \"%s\"",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }

          /* set anon_uid */

          p_perms->anonymous_gid = (gid_t) anon_gid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_SECTYPE))
        {
#     define MAX_SECTYPE      10        /* large enough !!! */

          char *sec_list[MAX_SECTYPE];
          int idx, count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_SECTYPE) == FLAG_EXPORT_SECTYPE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_SECTYPE);
              continue;
            }

          set_options |= FLAG_EXPORT_SECTYPE;

          /* reset security flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_AUTH_TYPES;

          /* fill sec list with NULL pointers */
          memset(sec_list, 0, sizeof(sec_list));

          /*
           * Search for coma-separated list of sectypes
           */
          count = nfs_ParseConfLine(sec_list,
                                    MAX_SECTYPE,
                                    0,
                                    var_value,
                                    ',');

          if(count < 0)
            {
              err_flag = TRUE;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: SecType list too long (>%d)",
                      label, MAX_SECTYPE);

              continue;
            }

          /* add each sectype flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(sec_list[idx], "none"))
                {
                  p_perms->options |= EXPORT_OPTION_AUTH_NONE;
                }
              else if(!STRCMP(sec_list[idx], "sys"))
                {
                  p_perms->options |= EXPORT_OPTION_AUTH_UNIX;
                }
              else if(!STRCMP(sec_list[idx], "krb5"))
                {
                  p_perms->options |= EXPORT_OPTION_RPCSEC_GSS_NONE;
                }
              else if(!STRCMP(sec_list[idx], "krb5i"))
                {
                  p_perms->options |= EXPORT_OPTION_RPCSEC_GSS_INTG;
                }
              else if(!STRCMP(sec_list[idx], "krb5p"))
                {
                  p_perms->options |= EXPORT_OPTION_RPCSEC_GSS_PRIV;
                }
              else
                {
                  LogCrit(COMPONENT_CONFIG,
                          "NFS READ %s: Invalid SecType \"%s\". Values can be: none, sys, krb5, krb5i, krb5p.",
                          label, sec_list[idx]);
                  err_flag = TRUE;
                }
            }

          /* check that at least one sectype has been specified */
          if((p_perms->options & EXPORT_OPTION_AUTH_TYPES) == 0)
            LogWarn(COMPONENT_CONFIG,
                    "NFS READ %s: Empty SecType",
                    label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_READ))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_MAX_READ) == FLAG_EXPORT_MAX_READ)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_MAX_READ);
              continue;
            }

          set_options |= FLAG_EXPORT_MAX_READ;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid MaxRead: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: MaxRead out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->MaxRead = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_MAXREAD;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_WRITE))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_MAX_WRITE) == FLAG_EXPORT_MAX_WRITE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_MAX_WRITE);
              continue;
            }

          set_options |= FLAG_EXPORT_MAX_WRITE;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid MaxWrite: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: MaxWrite out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->MaxWrite = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_MAXWRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READ))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_READ) == FLAG_EXPORT_PREF_READ)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PREF_READ);
              continue;
            }

          set_options |= FLAG_EXPORT_PREF_READ;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid PrefRead: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: PrefRead out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->PrefRead = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_PREFREAD;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_WRITE))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_WRITE) == FLAG_EXPORT_PREF_WRITE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PREF_WRITE);
              continue;
            }

          set_options |= FLAG_EXPORT_PREF_WRITE;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid PrefWrite: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: PrefWrite out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->PrefWrite = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_PREFWRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READDIR))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_READDIR) == FLAG_EXPORT_PREF_READDIR)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PREF_READDIR);
              continue;
            }

          set_options |= FLAG_EXPORT_PREF_READDIR;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid PrefReaddir: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: PrefReaddir out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->PrefReaddir = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_PREFRDDIR;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_WRITE))
        {
          long long int size;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PREF_WRITE) == FLAG_EXPORT_PREF_WRITE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_PREF_WRITE);
              continue;
            }

          set_options |= FLAG_EXPORT_PREF_WRITE;

          errno = 0;
          size = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid PrefWrite: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: PrefWrite out of range: %lld",
                      label, size);
              err_flag = TRUE;
              continue;
            }

          p_entry->PrefWrite = (fsal_size_t) size;
          p_perms->options |= EXPORT_OPTION_PREFWRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSID))
        {
          long long int major, minor;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FSID) == FLAG_EXPORT_FSID)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_FSID);
              continue;
            }

          set_options |= FLAG_EXPORT_FSID;

          /* parse and check filesystem id */
          errno = 0;
          major = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '.' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid filesystem_id: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          end_ptr++;            /* the first character after the dot */

          errno = 0;
          minor = strtoll(end_ptr, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid filesystem_id: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(major < 0 || minor < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Filesystem_id out of range: %lld.%lld",
                      label, major, minor);
              err_flag = TRUE;
              continue;
            }

          /* set filesystem_id */

          p_entry->filesystem_id.major = (fsal_u64_t) major;
          p_entry->filesystem_id.minor = (fsal_u64_t) minor;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSUID))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NOSUID) == FLAG_EXPORT_NOSUID)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_NOSUID);
              continue;
            }

          set_options |= FLAG_EXPORT_NOSUID;

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_perms->options |= EXPORT_OPTION_NOSUID;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                        label, var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSGID))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NOSGID) == FLAG_EXPORT_NOSGID)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_NOSGID);
              continue;
            }

          set_options |= FLAG_EXPORT_NOSGID;

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_perms->options |= EXPORT_OPTION_NOSGID;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PRIVILEGED_PORT))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_PRIVILEGED_PORT) == FLAG_EXPORT_PRIVILEGED_PORT)
            {
              DEFINED_TWICE_WARNING(label, "FLAG_EXPORT_PRIVILEGED_PORT");
              continue;
            }

          set_options |= FLAG_EXPORT_PRIVILEGED_PORT;

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_perms->options |= EXPORT_OPTION_PRIVILEGED_PORT;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_DATACACHE))
        {
          LogInfo(COMPONENT_CONFIG,
                  "NFS READ %s: Deprecated EXPORT option %s ignored",
                  label, var_name);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PNFS))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_PNFS) == FLAG_EXPORT_USE_PNFS)
            {
              DEFINED_TWICE_WARNING(label, "FLAG_EXPORT_USE_PNFS");
              continue;
            }

          set_options |= EXPORT_OPTION_USE_PNFS;

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_perms->options |= EXPORT_OPTION_USE_PNFS;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_UQUOTA ) )
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_UQUOTA) == FLAG_EXPORT_USE_UQUOTA)
            {
              DEFINED_TWICE_WARNING(label, "FLAG_EXPORT_USE_UQUOTA");
              continue;
            }

          set_options |= EXPORT_OPTION_USE_UQUOTA;

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_perms->options |= EXPORT_OPTION_USE_UQUOTA;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for '%s' (%s): TRUE or FALSE expected.",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_SPECIFIC))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_SPECIFIC) == FLAG_EXPORT_FS_SPECIFIC)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_FS_SPECIFIC);
              continue;
            }

          set_options |= FLAG_EXPORT_FS_SPECIFIC;

          if(strmaxcpy(p_entry->FS_specific,
                       var_value,
                       sizeof(p_entry->FS_specific)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_TAG))
        {
          exportlist_t * p_fe;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_TAG) == FLAG_EXPORT_FS_TAG)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_FS_TAG);
              continue;
            }

          set_options |= FLAG_EXPORT_FS_TAG;

          p_fe = nfs_Get_export_by_tag(pexportlist, var_value);

          if(p_fe != NULL)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Duplicate Tag: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          if(strmaxcpy(p_entry->FS_tag,
                       var_value,
                       sizeof(p_entry->FS_tag)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_WRITE))
        {
          long long int offset;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid MaxOffsetWrite: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          p_entry->MaxOffsetWrite = (fsal_size_t) offset;
          p_perms->options |= EXPORT_OPTION_MAXOFFSETWRITE;

          set_options |= FLAG_EXPORT_MAX_OFF_WRITE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_CACHE_SIZE))
        {
          LogInfo(COMPONENT_CONFIG,
                  "NFS READ %s: Deprecated EXPORT option %s ignored",
                  label, var_name);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_READ))
        {
          long long int offset;
          char *end_ptr;

          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid MaxOffsetRead: \"%s\"",
                      label, var_value);
              err_flag = TRUE;
              continue;
            }

          p_entry->MaxOffsetRead = (fsal_size_t) offset;
          p_perms->options |= EXPORT_OPTION_MAXOFFSETREAD;

          set_options |= FLAG_EXPORT_MAX_OFF_READ;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COMMIT))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

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
                        "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                        label, var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_GANESHA_WRITE_BUFFER))
        {
           if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

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
                        "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                        label, var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
#ifdef _USE_FSAL_UP
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL_UP_TYPE))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          if(strmaxcpy(p_entry->fsal_up_type,
                       var_value,
                       sizeof(p_entry->fsal_up_type)) == -1)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL_UP_TIMEOUT))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

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
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

          /* TODO: Parse the strings and form a list.
           * Later each name will match a predefined filter
           * in the FSAL UP interface. */
          p_entry->fsal_up_filter_list = NULL;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_FSAL_UP))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

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
                        "USR_FSAL_UP: Invalid value for %s (%s): TRUE or FALSE expected.",
                        var_name, var_value);
                err_flag = TRUE;
                continue;
              }
            }
        }
#endif /* _USE_FSAL_UP */
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COOKIE_VERIFIER))
        {
          if(label == CONF_LABEL_EXPORT_CLIENT)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s not allowed",
	              label, var_name);
              err_flag = TRUE;
              continue;
            }

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
                        "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                        label, var_name, var_value);
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
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_SQUASH);
              continue;
            }

          set_options |= FLAG_EXPORT_SQUASH;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ALL_ANON) == FLAG_EXPORT_ALL_ANON)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ANON_ROOT,
                                       CONF_EXPORT_ALL_ANON);
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
              p_perms->options |= EXPORT_OPTION_ALL_ANONYMOUS;
            }
          else if(!STRCMP(var_value, "No_Root_Squash") ||
                  !STRCMP(var_value, "None") ||
                  !STRCMP(var_value, "NoIdSquash"))
            {
              /* Allow Root access */
              p_perms->options |= EXPORT_OPTION_ROOT;
            }
          else
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for %s (%s): "
                      "Root, Root_Squash, RootSquash,"
                      "All, All_Squash, AllSquash,"
                      "No_Root_Squash, NoIdSquash, or None expected.",
                      label, var_name, var_value);
              err_flag = TRUE;
              continue;
            }
        }
      else
        {
          LogWarn(COMPONENT_CONFIG,
                  "NFS READ %s: Unknown option: %s",
                  label, var_name);
        }

    }

  /* check for mandatory options */
  if((set_options & mandatory_options) != mandatory_options)
    {
      if((set_options & FLAG_EXPORT_ID) !=
         (FLAG_EXPORT_ID & mandatory_options))
        LogCrit(COMPONENT_CONFIG,
                "NFS READ %s: Missing mandatory parameter %s",
                label, CONF_EXPORT_ID );

      if((set_options & FLAG_EXPORT_PATH) !=
         (FLAG_EXPORT_PATH & mandatory_options))
        LogCrit(COMPONENT_CONFIG,
                "NFS READ %s: Missing mandatory parameter %s",
                label, CONF_EXPORT_PATH);

      if((set_options & FLAG_EXPORT_ACCESS_LIST) !=
         (FLAG_EXPORT_ACCESS_LIST & mandatory_options))
        LogCrit(COMPONENT_CONFIG,
                "NFS READ %s: Must have at least one of %s, %s, %s, %s, %s, or %s",
                label,
                CONF_EXPORT_ACCESS,      CONF_EXPORT_ROOT,
                CONF_EXPORT_READ_ACCESS, CONF_EXPORT_READWRITE_ACCESS,
                CONF_EXPORT_MD_ACCESS,   CONF_EXPORT_MD_RO_ACCESS);

      err_flag = TRUE;
    }

  if((label == CONF_LABEL_EXPORT_CLIENT) &&
     (p_entry == NULL))
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ %s: Unable to find export, may be missing %s and/or %s, ignoring entry.",
              label, CONF_EXPORT_ID, CONF_EXPORT_PATH);

      FreeClientList(p_access_list);
      return -1;
    }
  else if(label == CONF_LABEL_EXPORT)
    {
      if(((p_perms->options & EXPORT_OPTION_NFSV4) != 0) &&
         ((set_options & FLAG_EXPORT_PSEUDO) == 0))
        {
          /* If we export for NFS v4, Pseudo is required */
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ %s: Missing mandatory parameter %s",
                  label, CONF_EXPORT_PSEUDO);

          err_flag = TRUE;
        }

      if((p_found_entry != NULL) &&
         ((set_options & FLAG_EXPORT_PSEUDO) == 0) &&
         ((set_options & FLAG_EXPORT_FS_TAG) == 0))
        {
          /* Duplicate export must specify at least one of tag and pseudo */
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ %s: Duplicate %s must have at least %s or %s",
                  label, label, CONF_EXPORT_PSEUDO, CONF_EXPORT_FS_TAG);

          err_flag = TRUE;
        }

      /* Here we can make sure certain options are turned on for specific FSALs */
      if(!err_flag && !fsal_specific_checks(p_entry))
        {
          LogCrit(COMPONENT_CONFIG,
                   "NFS READ %s: Found conflicts in export entry, ignoring entry.",
                   label);
          RemoveExportEntry(p_entry);
          return -1;
        }
    }

  if(label == CONF_LABEL_EXPORT_CLIENT)
    {
      if(((p_entry->export_perms.options & EXPORT_OPTION_NFSV4) == 0) &&
         ((p_perms->options & EXPORT_OPTION_NFSV4) != 0))
        {
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ %s: Export %d (%s) doesn't allow NFS v4 (pseudo path won't be set up)",
                  label, p_entry->id, p_entry->fullpath);

          err_flag = TRUE;
        }
    }

  /* check if there had any error.
   * if so, free the p_entry and return an error.
   */
  if(err_flag)
    {
      if(p_entry != NULL)
        LogCrit(COMPONENT_CONFIG,
                "NFS READ %s: Export %d (%s) had errors, ignoring entry",
                label, p_entry->id, p_entry->fullpath);
      else
        LogCrit(COMPONENT_CONFIG,
                "NFS READ %s: Export had errors, ignoring entry",
                label);

      if(label == CONF_LABEL_EXPORT)
        RemoveExportEntry(p_entry);
      else
        FreeClientList(p_access_list);

      return -1;
    }

  *pp_export = p_entry;

  LogEvent(COMPONENT_CONFIG,
           "NFS READ %s: Export %d (%s) successfully parsed",
           label, p_entry->id, p_entry->fullpath);

  if(isFullDebug(COMPONENT_CONFIG))
    {
      char                    perms[1024];
      struct display_buffer   dspbuf = {sizeof(perms), perms, perms};

      (void) display_export_perms(&dspbuf, p_perms);

      LogFullDebug(COMPONENT_CONFIG,
                   "  Export Perms: %s", perms);
    }

  glist_for_each(glist, &p_access_list->client_list)
    {
      exportlist_client_entry_t * p_client_entry;
      p_client_entry = glist_entry(glist, exportlist_client_entry_t, cle_list);

      if(label == CONF_LABEL_EXPORT_CLIENT)
        {
          /* Copy the final EXPORT_CLIENT permissions into the client
           * list entries.
           */
          p_client_entry->client_perms = *p_perms;
        }

      if(isFullDebug(COMPONENT_CONFIG))
        LogClientListEntry(COMPONENT_CONFIG, p_client_entry);
    }

  /* Append the new EXPORT_CLIENT Access list to the export */
  if(label == CONF_LABEL_EXPORT_CLIENT)
    {
      glist_add_list_tail(&p_entry->clients.client_list,
                          &p_access_list->client_list);
      p_entry->clients.num_clients += p_access_list->num_clients;
    }

  return 0;
}                                  /* BuildExportEntry */

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

  p_entry->export_perms.anonymous_uid = (uid_t) ANON_UID;

  /* By default, export is RW */
  p_entry->export_perms.options |= EXPORT_OPTION_RW_ACCESS;

  /* by default, we support auth_none and auth_sys */
  p_entry->export_perms.options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and both transport protocols */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV2) != 0)
    p_entry->export_perms.options |= EXPORT_OPTION_NFSV2;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->export_perms.options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->export_perms.options |= EXPORT_OPTION_NFSV4;
  p_entry->export_perms.options |= EXPORT_OPTION_TRANSPORTS;

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
              "NFS READ EXPORT: Invalid client \"%s\"",
              (char *)client_root_access);
      return NULL;
    }

  LogEvent(COMPONENT_CONFIG,
           "NFS READ EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return p_entry;

}                               /* BuildDefaultExport */

void FreeClientList(exportlist_client_t * clients)
{
  struct glist_head * glist;
  struct glist_head * glistn;

  glist_for_each_safe(glist, glistn, &clients->client_list)
    {
       exportlist_client_entry_t * p_client;
       p_client = glist_entry(glist, exportlist_client_entry_t, cle_list);
       glist_del(&p_client->cle_list);
       gsh_free(p_client);
    }
}

void RemoveExportEntry(exportlist_t * p_entry)
{
  FSAL_CleanUpExportContext(&p_entry->FS_export_context);

  FreeClientList(&p_entry->clients);

  if(p_entry->proot_handle != NULL)
    {
      gsh_free(p_entry->proot_handle);
    }

#ifdef _USE_STAT_EXPORTER
  if(p_entry->worker_stats != NULL)
    {
      gsh_free(p_entry->worker_stats);
    }
#endif

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
  eid_cache_t *cache_slot;

  exportlist_t *p_export_item = NULL;

  int nb_entries = 0;

  if(!pexportlist)
    return -EFAULT;

  /* get the number of blocks in the configuration file */
  nb_blk = config_GetNbBlocks(in_config);

  if(nb_blk < 0)
    return -1;

  /* Iteration on config file blocks for EXPORTs. */
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

          rc = BuildExportEntry(block,
                                &p_export_item,
                                pexportlist,
                                CONF_LABEL_EXPORT);

          /* If the entry is errorneous, ignore it
           * and continue checking syntax of other entries.
           */
          if(rc != 0)
            {
              err_flag = TRUE;
              continue;
            }

          glist_add_tail(pexportlist, &p_export_item->exp_list);
          /* Add to cache */
          cache_slot = (eid_cache_t *) &(export_by_id.eid_cache[p_export_item->id % EXPORT_BY_ID_HASHSIZE]);   
          if (cache_slot->eidc_cache_entry == NULL)
             {
               /* First entry in the bucket */
               cache_slot->eidc_cache_entry = p_export_item;
             }
           else
             {
               /* We have a hash collision; allocate a new one and add
                * it to the end of the list.
                */
               while (cache_slot->eidc_next != NULL)
                 cache_slot = cache_slot->eidc_next;

               /* reached end of this hash bucket */
               cache_slot->eidc_next = gsh_calloc(1, sizeof(eid_cache_t));
               if (cache_slot->eidc_next == NULL)
                  {
                    LogCrit(COMPONENT_CONFIG,
                            "No memory for export_by_id cache entry");
                    /* It is unlikely to come here, but if we do, still
                     * dont want fail because it is just treated like
                     * a cache miss.
                     */
                    nb_entries++;
                    continue;
                  }
                cache_slot = cache_slot->eidc_next;
                cache_slot->eidc_cache_entry = p_export_item;
             }
          nb_entries++;
        }
    }

  /* Iteration on config file blocks for EXPORT_CLIENTs. */
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

      if(!STRCMP(blk_name, CONF_LABEL_EXPORT_CLIENT))
        {

          rc = BuildExportEntry(block,
                                &p_export_item,
                                pexportlist,
                                CONF_LABEL_EXPORT_CLIENT);

          /* If the entry is errorneous, ignore it
           * and continue checking syntax of other entries.
           */
          if(rc != 0)
            {
              err_flag = TRUE;
              continue;
            }
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
static int export_client_match(sockaddr_t *hostaddr,
			exportlist_client_t *clients,
			exportlist_client_entry_t * pclient_found,
			unsigned int export_option)
{
  unsigned int        i = 0;
  int                 rc;
  char                hostname[MAXHOSTNAMELEN];
  in_addr_t           addr = get_in_addr(hostaddr);
  struct glist_head * glist;
  char                ipstring[SOCK_NAME_MAX];
  int                 ipvalid = -1; /* -1 need to print, 0 - invalid, 1 - ok */

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
      if(export_option & EXPORT_OPTION_ACCESS_OPT_LIST)
        access_list = " EXPORT_CLIENT ACCESS-LIST";      

      if((export_option & (EXPORT_OPTION_ROOT            |
                           EXPORT_OPTION_READ_ACCESS     |
                           EXPORT_OPTION_WRITE_ACCESS    |
                           EXPORT_OPTION_MD_WRITE_ACCESS |
                           EXPORT_OPTION_MD_READ_ACCESS  |
                           EXPORT_OPTION_ACCESS_LIST     |
                           EXPORT_OPTION_ACCESS_OPT_LIST)) == 0)
        root_access = " NONE";

      if(ipvalid < 0)
        ipvalid = sprint_sockip(hostaddr, ipstring, sizeof(ipstring));

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
      if((p_client->client_perms.options & export_option) != export_option)
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
          if((rc = nfs_ip_name_get(hostaddr, hostname, sizeof(hostname))) != IP_NAME_SUCCESS)
            {
              if(rc == IP_NAME_NOT_FOUND)
                {
                  /* IPaddr was not cached, add it to the cache */
                  if(nfs_ip_name_add(hostaddr, hostname, sizeof(hostname)) != IP_NAME_SUCCESS)
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
          if(ipvalid < 0)
            ipvalid = sprint_sockip(hostaddr, ipstring, sizeof(ipstring));
            
          if(ipvalid && 
             (fnmatch(p_client->client.wildcard.wildcard,
                      ipstring, FNM_PATHNAME) == 0))
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
          if((rc = nfs_ip_name_get(hostaddr, hostname, sizeof(hostname))) != IP_NAME_SUCCESS)
            {
              if(rc == IP_NAME_NOT_FOUND)
                {
                  /* IPaddr was not cached, add it to the cache */
                  if(nfs_ip_name_add(hostaddr, hostname, sizeof(hostname)) != IP_NAME_SUCCESS)
                    {
                      /* Major failure, name could not be resolved */
                      LogInfo(COMPONENT_DISPATCH,
                              "Could not resolve hostame for addr %d.%d.%d.%d ... not checking if a hostname wildcard matches",
                              (int)(ntohl(addr) >> 24),
                              (int)(ntohl(addr) >> 16) & 0xFF,
                              (int)(ntohl(addr) >> 8) & 0xFF,
                              (int)(ntohl(addr) & 0xFF)
                              );
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
          break;

       case HOSTIF_CLIENT_V6:
          break;

       case MATCH_ANY_CLIENT:
          *pclient_found = *p_client;
          LogFullDebug(COMPONENT_DISPATCH,
                       "This matches any client wildcard for entry %u",
                       i);
          return TRUE;

       case BAD_CLIENT:
          LogCrit(COMPONENT_DISPATCH,
                  "Bad client in position %u seen in export list", i );
	  break ;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;

}                               /* export_client_match */

#ifdef _USE_TIRPC_IPV6
static int export_client_matchv6(struct in6_addr *paddrv6,
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
      if((p_client->client_perms.options & export_option) != export_option)
        continue;

      switch (p_client->type)
        {
        case HOSTIF_CLIENT:
        case NETWORK_CLIENT:
        case NETGROUP_CLIENT:
        case WILDCARDHOST_CLIENT:
        case GSSPRINCIPAL_CLIENT:
        case BAD_CLIENT:
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

        case MATCH_ANY_CLIENT:
           *pclient_found = *p_client;
           LogFullDebug(COMPONENT_DISPATCH,
                        "This matches any client wildcard for entry %u",
                        i);
          return TRUE;
        }                       /* switch */
    }                           /* for */

  /* no export found for this option */
  return FALSE;
}                               /* export_client_matchv6 */
#endif

int export_client_match_any(sockaddr_t                * hostaddr,
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
int nfs_export_check_security(struct svc_req * ptr_req,
                              export_perms_t * p_export_perms,
                              exportlist_t   * pexport)
{
  switch (ptr_req->rq_cred.oa_flavor)
    {
      case AUTH_NONE:
        if((p_export_perms->options & EXPORT_OPTION_AUTH_NONE) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_NONE",
                    pexport->fullpath);
            return FALSE;
          }
        break;

      case AUTH_UNIX:
        if((p_export_perms->options & EXPORT_OPTION_AUTH_UNIX) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_UNIX",
                    pexport->fullpath);
            return FALSE;
          }
        break;

#ifdef _HAVE_GSSAPI
      case RPCSEC_GSS:
        if((p_export_perms->options &
           (EXPORT_OPTION_RPCSEC_GSS_NONE |
            EXPORT_OPTION_RPCSEC_GSS_INTG |
            EXPORT_OPTION_RPCSEC_GSS_PRIV)) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support RPCSEC_GSS",
                    pexport->fullpath);
            return FALSE;
          }
        else
          {
            struct svc_rpc_gss_data *gd;
            rpc_gss_svc_t svc;
            gd = SVCAUTH_PRIVATE(ptr_req->rq_auth);
            svc = gd->sec.svc;
            LogFullDebug(COMPONENT_DISPATCH,
                         "Testing svc %d", (int) svc);
            switch(svc)
              {
                case RPCSEC_GSS_SVC_NONE:
                  if((p_export_perms->options &
                      EXPORT_OPTION_RPCSEC_GSS_NONE) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_NONE",
                              pexport->fullpath);
                      return FALSE;
                    }
                  break;

                case RPCSEC_GSS_SVC_INTEGRITY:
                  if((p_export_perms->options &
                      EXPORT_OPTION_RPCSEC_GSS_INTG) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_INTEGRITY",
                              pexport->fullpath);
                      return FALSE;
                    }
                  break;

                case RPCSEC_GSS_SVC_PRIVACY:
                  if((p_export_perms->options &
                      EXPORT_OPTION_RPCSEC_GSS_PRIV) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_PRIVACY",
                              pexport->fullpath);
                      return FALSE;
                    }
                  break;

                  default:
                    LogInfo(COMPONENT_DISPATCH,
                            "Export %s does not support unknown "
                            "RPCSEC_GSS_SVC %d",
                            pexport->fullpath, (int) svc);
                    return FALSE;
              }
          }
      break;
#endif
      default:
        LogInfo(COMPONENT_DISPATCH,
                "Export %s does not support unknown oa_flavor %d",
                pexport->fullpath, (int) ptr_req->rq_cred.oa_flavor);
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
  /* ipv6 is indeed ipv4 in this case */
  return ipv6;
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

  if(isFullDebug(COMPONENT_DISPATCH))
    {
      ipstring[0] = '\0';
      ipvalid = sprint_sockip(puse_hostaddr, ipstring, sizeof(ipstring));

      /* Use IP address as a string for wild character access checks. */
      if(!ipvalid)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Could not convert the IP address to a character string.");
          return;
        }

      LogFullDebug(COMPONENT_DISPATCH, "Check for address %s", ipstring);
    }

  if(pexport == NULL)
    {
      LogCrit(COMPONENT_DISPATCH,
              "No export to check permission against");
      return;
    }

  /* Test if client is in EXPORT_CLIENT Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_ACCESS_OPT_LIST))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches EXPORT_CLIENT Access List",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      *pexport_perms = client_found.client_perms;

      return;
    }

  /* Grab anonymous uid and gid and base permissions from export. */
  pexport_perms->anonymous_uid = pexport->export_perms.anonymous_uid;
  pexport_perms->anonymous_gid = pexport->export_perms.anonymous_gid;
  pexport_perms->options       = pexport->export_perms.options &
                                 EXPORT_OPTION_BASE_ACCESS;
                                 
  /* Test if client is in Root_Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_ROOT))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches Root_Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      pexport_perms->options |= client_found.client_perms.options;
    }

  /* Continue on to see if client matches any other kind of access list */

  /* Test if client is in RW_Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_WRITE_ACCESS))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches RW_Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      pexport_perms->options |= EXPORT_OPTION_RW_ACCESS |
                                EXPORT_OPTION_MD_ACCESS;
      return;
    }

  /* Test if client is in R_Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_READ_ACCESS))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches R_Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      pexport_perms->options |= EXPORT_OPTION_READ_ACCESS |
                                EXPORT_OPTION_MD_READ_ACCESS;

      return;
    }

  /* Test if client is in MDONLY_Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_MD_WRITE_ACCESS))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches MDONLY_Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      pexport_perms->options |= EXPORT_OPTION_MD_ACCESS;

      return;
    }

  /* Test if client is in MDONLY_RO_Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_MD_READ_ACCESS))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches MDONLY_RO_Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      pexport_perms->options |= EXPORT_OPTION_MD_READ_ACCESS;

      return;
    }

  /* Test if client is in Access list */
  if(export_client_match_any(puse_hostaddr,
                             &(pexport->clients),
                             &client_found,
                             EXPORT_OPTION_ACCESS_LIST))
    {
      if(isFullDebug(COMPONENT_DISPATCH))
        {
          LogFullDebug(COMPONENT_DISPATCH,
                       "Export %d Client %s matches Access",
                       pexport->id, ipstring);

          LogClientListEntry(COMPONENT_DISPATCH, &client_found);
        }

      /* Grab the root access and rw/ro/mdonly/mdonly ro access from export */
      pexport_perms->options |= pexport->export_perms.options &
                                EXPORT_OPTION_CUR_ACCESS;

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

      pexport_perms->options |= (pexport->export_perms.options &
                                EXPORT_OPTION_CUR_ACCESS) |
                                EXPORT_OPTION_READ_ACCESS;

      return;
    }

  /* If this point is reached, no matching entry was found */
  LogFullDebug(COMPONENT_DISPATCH,
               "export %d permission denied - no matching entry",
               pexport->id);

  pexport_perms->options = 0;

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
      cache_entry_t *pentry = NULL;

      fsal_op_context_t context;
      fsal_staticfsinfo_t *pstaticinfo = NULL;
      fsal_export_context_t *export_context = NULL;

      /* Get the context for FSAL super user */
      memset(&context, 0, sizeof(fsal_op_context_t));
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
                                                        0,
                                                        &exportpath_fsal))))
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

          pcurrent->FS_export_context.fe_export = pcurrent;

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
          if( ((pcurrent->export_perms.options & EXPORT_OPTION_MAXREAD) != EXPORT_OPTION_MAXREAD )) 
             {
               if ( pstaticinfo && pstaticinfo->maxread )
                  pcurrent->MaxRead = pstaticinfo->maxread;
               else
                  pcurrent->MaxRead = LASTDEFAULT;
             }
          if( ((pcurrent->export_perms.options & EXPORT_OPTION_MAXWRITE) != EXPORT_OPTION_MAXWRITE )) 
             {
               if ( pstaticinfo && pstaticinfo->maxwrite )
                  pcurrent->MaxWrite = pstaticinfo->maxwrite;
               else
                  pcurrent->MaxWrite = LASTDEFAULT;
             }
          LogFullDebug(COMPONENT_INIT,
                      "Set MaxRead MaxWrite for Path=%s Options = 0x%x MaxRead = 0x%llX MaxWrite = 0x%llX",
                      pcurrent->fullpath, pcurrent->export_perms.options,
                      (long long) pcurrent->MaxRead,
                      (long long) pcurrent->MaxWrite);
             
          /* Add this entry to the Cache Inode as a "root" entry */
          fsdata.fh_desc.start = (caddr_t) &fsal_handle;
          fsdata.fh_desc.len = 0;
	  (void) FSAL_ExpandHandle(context.export_context,
				   FSAL_DIGEST_SIZEOF,
				   &fsdata.fh_desc);

          /* cache_inode_get returns a cache_entry with
             reference count of 2, where 1 is the sentinel value of
             a cache entry in the hash table.  The export list in
             this case owns the extra reference.  In the future
             if functionality is added to dynamically add and remove
             export entries, then the function to remove an export
             entry MUST put the extra reference. */

          if((pentry = cache_inode_get(&fsdata,
                                       NULL, /* Don't need the attr */
                                       &context,
                                       NULL,
                                       &cache_status)) == NULL)
            {
              LogCrit(COMPONENT_INIT,
                      "Error %s when creating root cached entry for %s, removing export id %u",
                      cache_inode_err_str(cache_status), pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
            }

          /* Save away the root entry */
          pcurrent->exp_root_cache_inode = pentry;

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
exportlist_t *GetExportEntry(char *path)
{
  exportlist_t *p_current_item = NULL;
  struct glist_head * glist;
  int found = 0;
  int len_path = strlen(path);
  int len_export;

  /*
   * Find the export for the path (using as well Path or Tag )
   */
  glist_for_each(glist, nfs_param.pexportlist)
    {
    p_current_item = glist_entry(glist, exportlist_t, exp_list);

    len_export = strlen(p_current_item->fullpath);

    LogDebug(COMPONENT_CONFIG,
             "export path %s, path %s",
             p_current_item->fullpath, path);

    /* If path doesn't end with '/' */
    if(path[len_path - 1] != '/' &&
       len_path == (len_export - 1))
      {
        /* Path would be same length as export if it had trailing '/'.
         * Since we know the path is otherwise the same length as
         * the export path, we don't need to try to compare the
         * trailing '/'. As long as the two strings are equal not
         * considering the trailing '/' in the export path, then path
         * is a proper sub-directory of the export path (mainly being
         * the exported directory...)
         */
        len_export = len_path;
      }

    /* If path is shorter than export path we are looking for,
     * then path we are looking for can't be this export path
     * or a sub-directory of it...
     */
    if(len_path < len_export)
      continue;

    /* Is export path a subdirectory of path? */
    if(!strncmp(p_current_item->fullpath,
                path,
                len_export))
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

void set_mounted_on_fileid(cache_entry_t      * entry,
                           fsal_attrib_list_t * attr,
                           exportlist_t       * exp)
{
  if(entry == exp->exp_root_cache_inode)
    {
      attr->mounted_on_fileid = exp->exp_mounted_on_file_id;

      LogFullDebug(COMPONENT_DISPATCH,
                   "Setting mounted_on_file_id to %"PRIu64
                   " from Export_Id %d Pseudo %s",
                   (uint64_t) attr->mounted_on_fileid,
                   exp->id,
                   exp->pseudopath);
    }
  else
    {
      attr->mounted_on_fileid = attr->fileid;

      LogFullDebug(COMPONENT_DISPATCH,
                   "Setting mounted_on_file_id to %"PRIu64
                   " (same as fileid) because entry is not root of Export_Id %d Pseudo %s",
                   (uint64_t) attr->mounted_on_fileid,
                   exp->id,
                   exp->pseudopath);
    }
}

/*
 * XXX - Giganto-hack
 *
 * Get rid of this monstrosity if/when GPFS ever supports per-FS grace
 */
int
get_first_context(fsal_op_context_t *p_context)
{
      exportlist_t              *pcurrent = NULL;
      struct glist_head         *glist;
      struct glist_head         *glistn;
      fsal_status_t              fsal_status;
      fsal_path_t                exportpath_fsal;

      /*
       * If no exports defined, punt
       */
      if (glist_empty(nfs_param.pexportlist))
                return 0;

      /* Get the context for FSAL super user */
      memset(p_context, 0, sizeof(fsal_op_context_t));
      fsal_status = FSAL_InitClientContext(p_context);
      if (FSAL_IS_ERROR(fsal_status)) {
          LogCrit(COMPONENT_INIT,
                "Couldn't get the context for FSAL super user");
          return 0;
      }

      /* loop the export list */
      glist_for_each_safe(glist, glistn, nfs_param.pexportlist)
      {
          pcurrent = glist_entry(glist, exportlist_t, exp_list);

          /* Build the FSAL path */
          fsal_status = FSAL_str2path(pcurrent->fullpath, 0, &exportpath_fsal);
          if (FSAL_IS_ERROR(fsal_status)) {
              LogCrit(COMPONENT_INIT,
                    "Couldn't build FSAL path for %s, removing export id %u",
                    pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
          }

          /* inits context for the current export entry */
          fsal_status = FSAL_BuildExportContext(
                &pcurrent->FS_export_context,
                &exportpath_fsal, pcurrent->FS_specific);

          if (FSAL_IS_ERROR(fsal_status)) {
              LogCrit(COMPONENT_INIT,
                        "Couldn't build export context for %s, "
                        "removing export id %u",
                        pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
          }
          pcurrent->FS_export_context.fe_export = pcurrent;

          /* get the related client context */
          fsal_status = FSAL_GetClientContext(p_context,
                &pcurrent->FS_export_context, 0, 0, NULL, 0) ;
          if (FSAL_IS_ERROR(fsal_status)) {
              LogCrit(COMPONENT_INIT,
                        "Couldn't get the credentials for FSAL "
                        "super user for %s, removing export id %u",
                        pcurrent->fullpath, pcurrent->id);
              RemoveExportEntry(pcurrent);
              continue;
          }

          /* We're done ! - p_context has the goods */
          return 1;
      }

      /*
       * NB: Getting here implies there are exports defined
       *     for which we could not get even one context.
       */
      return 0;
}
