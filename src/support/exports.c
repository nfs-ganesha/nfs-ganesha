/*
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
 * @file  exports.c
 * @brief Export parsing and management
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
#define CONF_EXPORT_FS_SPECIFIC        "FS_Specific"
#define CONF_EXPORT_FS_TAG             "Tag"
#define CONF_EXPORT_MAX_OFF_WRITE      "MaxOffsetWrite"
#define CONF_EXPORT_MAX_OFF_READ       "MaxOffsetRead"
#define CONF_EXPORT_MAX_CACHE_SIZE     "MaxCacheSize"
#define CONF_EXPORT_REFERRAL           "Referral"
#define CONF_EXPORT_FSAL               "FSAL"
#define CONF_EXPORT_PNFS               "Use_pNFS"
#define CONF_EXPORT_UQUOTA             "User_Quota"
#define CONF_EXPORT_DELEG              "Use_Delegation"
#define CONF_EXPORT_USE_COMMIT                  "Use_NFS_Commit"
#define CONF_EXPORT_USE_GANESHA_WRITE_BUFFER    "Use_Ganesha_Write_Buffer"
#define CONF_EXPORT_USE_COOKIE_VERIFIER "UseCookieVerifier"

/** @todo : add encrypt handles option */

/* Internal identifiers */
#define FLAG_EXPORT_ID            0x000000001
#define FLAG_EXPORT_PATH          0x000000002

#define FLAG_EXPORT_ROOT_OR_ACCESS 0x000000004

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
#define FLAG_EXPORT_MAX_CACHE_SIZE  0x001000000
#define FLAG_EXPORT_USE_PNFS        0x002000000
#define FLAG_EXPORT_ACCESS_LIST     0x004000000
#define FLAG_EXPORT_ACCESSTYPE_LIST 0x008000000
#define FLAG_EXPORT_ANON_GROUP      0x010000000
#define FLAG_EXPORT_ALL_ANON        0x020000000
#define FLAG_EXPORT_ANON_USER       0x040000000
#define FLAG_EXPORT_CACHE_POLICY    0x080000000
#define FLAG_EXPORT_USE_UQUOTA      0x100000000
#define FLAG_EXPORT_USE_DELEG       0x200000000

/* limites for nfs_ParseConfLine */
/* Used in BuildExportEntry() */
#define EXPORT_MAX_CLIENTS   EXPORTS_NB_MAX_CLIENTS     /* number of clients */
#define EXPORT_MAX_CLIENTLEN 256        /* client name len */

/**
 * @brief Parse a line with a settable separator and  end of line
 *
 * @param[out] Argv               Result array
 * @param[in]  nbArgv             Allocated number of entries in the Argv
 * @param[in]  line               Input line
 * @param[in]  separator_function function used to identify a separator
 * @param[in]  endLine_func       function used to identify an end of line
 *
 * @return the number of fields found
 */
int nfs_ParseConfLine(char *Argv[],
                      int nbArgv,
                      char *line,
                      int (*separator_function) (char),
		      int (*endLine_func) (char))
{
  int output_value = 0;
  int endLine = false;

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
        endLine = true;

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

    }

  /* out of bounds */
  if(output_value >= nbArgv)
    return -1;

  return -2;

}

inline static int string_contains_slash( char* host )
{
  char * c ;

  for( c = host ; *c != '\0' ; c++ ) 
    if( *c == '/' ) 
      return 1 ;

  return 0 ;
}

/**
 * @brief determine network address from string.
 *
 * This routine is converting a valid host name is both literal or
 * dotted format into a valid netdb structure. If it could not
 * successfull, NULL is returned by the function.
 *
 * @note Dotted host address are 4 hex, decimal, or octal numbers in
 *       base 256 each separated by a period
 *
 * @param[in]  host    hostname or dotted address, within a string literal.
 * @param[out] netAddr Return address
 * @param[out] netMask Return address mask
 *
 * @return 0 if successfull, other values show an error
 *
 * @see inet_addr
 * @see gethostbyname
 * @see gethostbyaddr
 */
int nfs_LookupNetworkAddr(char *host,
                          unsigned long *netAddr,
                          unsigned long *netMask)
{
  CIDR *pcidr = NULL ;

  if( ( pcidr = cidr_from_str( host ) ) == NULL )
    return 1 ;

  memcpy( netAddr, pcidr->addr, sizeof( unsigned long ) ) ;
  memcpy( netMask, pcidr->mask, sizeof( unsigned long ) ) ; 
  
  /* BE CAREFUL !! The following lines are specific to IPv4. The libcidr support IPv6 as well */
  memset( netAddr, 0, sizeof( unsigned long ) ) ;
  memcpy( netAddr, &pcidr->addr[12], 4 ) ;

  memset( netMask, 0, sizeof( unsigned long ) ) ;
  memcpy( netMask, &pcidr->mask[12], 4 ) ;

  return 0 ; 
}

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
  int is_wildcarded_host = false;
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
      char addrbuf[sizeof("255.255.255.255")];
      char maskbuf[sizeof("255.255.255.255")];

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
                   "----------------- %s to network %s = %s netmask=%s",
                   (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                   client_hostname,
                   inet_ntop(AF_INET, &p_clients[i].client.network.netaddr,
                             addrbuf, sizeof(addrbuf)),
                   inet_ntop(AF_INET, &p_clients[i].client.network.netmask,
                             maskbuf, sizeof(maskbuf)));
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
                       "----------------- %s to client %s = %s",
                       (option == EXPORT_OPTION_ROOT ? "Root-access" : "Access"),
                       client_hostname, 
                       inet_ntop(AF_INET, &p_clients[i].client.hostif.clientaddr,
                                 addrbuf, sizeof(addrbuf)));
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
          is_wildcarded_host = false;
          for(l = 0; l < strlen(client_hostname); l++)
            {
              if((client_hostname[l] == '*') || (client_hostname[l] == '?'))
                {
                  is_wildcarded_host = true;
                  break;
                }
            }

          if(is_wildcarded_host)
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
    }

  /* Before we finish, do not forget to set the new number of clients
   * and the new pointer to client array.
   */
  (*clients).num_clients += new_clients_number;

  return 0;                     /* success !! */
}


/**
 * @brief Adds clients to an export list
 *
 * Adds a client to an export list (temporary function ?).
 *
 * @todo BUGAZOMEU : handling wildcards.
 *
 * @param[in,out] ExportEntry        Entry to update
 * @param[in]     new_clients_number Number of clients to add
 * @param[in]     new_clients_name   Names of clients to add
 * @param[in]     option             Options to be added to export
 */
static void nfs_AddClientsToExportList(exportlist_t *ExportEntry,
				       int new_clients_number,
				       char **new_clients_name,
				       int option)
{
  /*
   * Notifying the export list structure that another option is to be
   * handled
   */
  ExportEntry->options |= option;
  nfs_AddClientsToClientArray(&ExportEntry->clients, new_clients_number,
			      new_clients_name, option);
}

#define DEFINED_TWICE_WARNING( _str_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ_EXPORT: WARNING: %s defined twice !!! (ignored)", _str_ )

int parseAccessParam(char *var_name,
		     char *var_value,
		     exportlist_t *p_entry,
		     int access_option)
{
  int rc;
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
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ_EXPORT: ERROR: Invalid format for client list in EXPORT::%s definition",
	      var_name);

      return -1;
    }
  else if(count > EXPORT_MAX_CLIENTS)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: Client list too long (%d>%d)",
	      count, EXPORT_MAX_CLIENTS);
      return -1;
    }

  /* allocate clients strings  */
  for(idx = 0; idx < count; idx++)
    {
      client_list[idx] = gsh_malloc(EXPORT_MAX_CLIENTLEN);
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

      /* free client strings */
      for(idx = 0; idx < count; idx++)
        gsh_free(client_list[idx]);

      return rc;
    }

  nfs_AddClientsToExportList(p_entry, rc, (char **)client_list, access_option);

  /* free client strings */
  for(idx = 0; idx < count; idx++)
    gsh_free(client_list[idx]);

  return rc;
}

/**
 * @brief Builds an export entry from configutation file
 *
 * Don't stop immediately on error,
 * continue parsing the file, for listing other errors.
 *
 * @param[in]  block     Export configuration block
 * @param[out] pp_export Export entry being built
 *
 * @return 0 on success.
 */
static int BuildExportEntry(config_item_t block, exportlist_t ** pp_export)
{
  exportlist_t *p_entry;
  int i, rc;
  char *var_name;
  char *var_value;
  struct fsal_module *fsal_hdl = NULL;

  /* the mandatory options */

  unsigned int mandatory_options =
      (FLAG_EXPORT_ID | FLAG_EXPORT_PATH |
       FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_PSEUDO);

  /* the given options */

  unsigned int set_options = 0;

  bool err_flag   = false;

  /* allocates export entry */
  p_entry = gsh_calloc(1, sizeof(exportlist_t));

  if(p_entry == NULL)
    return ENOMEM;

  p_entry->status = EXPORTLIST_OK;
  p_entry->access_type = ACCESSTYPE_RW;
  p_entry->anonymous_uid = (uid_t) ANON_UID;
  p_entry->anonymous_gid = (gid_t) ANON_GID;
  p_entry->use_commit = true;
  p_entry->use_ganesha_write_buffer = false;
  p_entry->UseCookieVerifier = true;

  p_entry->worker_stats = gsh_calloc(nfs_param.core_param.nb_worker,
                                     sizeof(nfs_worker_stat_t));

  /* by default, we support auth_none and auth_sys */
  p_entry->options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and
     both transport protocols */
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

  init_glist(&p_entry->exp_state_list);
  init_glist(&p_entry->exp_lock_list);

  if(pthread_mutex_init(&p_entry->exp_state_mutex, NULL) == -1)
    {
      gsh_free(p_entry);
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
          gsh_free(p_entry);
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
              err_flag = true;
              continue;
            }

          if(export_id <= 0 || export_id > USHRT_MAX)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Export_id out of range: \"%ld\"",
                      export_id);
              err_flag = true;
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
              err_flag = true;
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
          set_options |= FLAG_EXPORT_ROOT_OR_ACCESS;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
          parseAccessParam(var_name, var_value, p_entry,
                           EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESS_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_entry,
			   EXPORT_OPTION_MD_WRITE_ACCESS | EXPORT_OPTION_MD_READ_ACCESS);
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
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
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
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
	  set_options |= FLAG_EXPORT_ROOT_OR_ACCESS | FLAG_EXPORT_ACCESSTYPE_LIST;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_READWRITE_ACCESS))
        {
          if(*var_value == '\0')
            {
              continue;
            }
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
              err_flag = true;
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
              err_flag = true;
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
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: NFS protocols list too long (>%d)",
                      MAX_NFSPROTO);

              /* free sec strings */
              for(idx = 0; idx < MAX_NFSPROTO; idx++)
                gsh_free(nfsvers_list[idx]);

              continue;
            }

          /* add each Nfs protocol flag to the option field.  */

          for(idx = 0; idx < count; idx++)
            {
              if(!STRCMP(nfsvers_list[idx], "3"))
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
                          "NFS READ_EXPORT: ERROR: Invalid NFS version \"%s\". Values can be: 3, 4.",
                          nfsvers_list[idx]);
                  err_flag = true;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_NFSPROTO; idx++)
            gsh_free(nfsvers_list[idx]);

          /* check that at least one nfs protocol has been specified */
          if((p_entry->options & (EXPORT_OPTION_NFSV2
                                  | EXPORT_OPTION_NFSV3 | EXPORT_OPTION_NFSV4)) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: WARNING: /!\\ Empty NFS_protocols list");
              err_flag = true;
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
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Protocol list too long (>%d)",
                      MAX_TRANSPROTO);

              /* free sec strings */
              for(idx = 0; idx < MAX_TRANSPROTO; idx++)
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
                          "NFS READ_EXPORT: ERROR: Invalid protocol \"%s\". Values can be: UDP, TCP.",
                          transproto_list[idx]);
                  err_flag = true;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_TRANSPROTO; idx++)
            gsh_free(transproto_list[idx]);

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
            p_entry->all_anonymous = true;

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
              err_flag = true;
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
              err_flag = true;
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
              err_flag = true;
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
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: SecType list too long (>%d)",
                      MAX_SECTYPE);

              /* free sec strings */
              for(idx = 0; idx < MAX_SECTYPE; idx++)
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
                  err_flag = true;
                }
            }

          /* free sec strings */
          for(idx = 0; idx < MAX_SECTYPE; idx++)
            gsh_free(sec_list[idx]);

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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: MaxRead out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxRead = size;
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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: MaxWrite out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxWrite = size;
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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefRead out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefRead = size;
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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefWrite out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefWrite = size;
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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefReaddir out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefReaddir = size;
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
              err_flag = true;
              continue;
            }

          if(size < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: PrefWrite out of range: %lld",
                      size);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->PrefWrite = size;
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
              err_flag = true;
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
              err_flag = true;
              continue;
            }

          if(major < 0 || minor < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: filesystem_id out of range: %lld.%lld",
                      major, minor);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->filesystem_id.major = major;
          p_entry->filesystem_id.minor = minor;

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
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): true or false expected.",
                        var_name, var_value);
                err_flag = true;
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
                      "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): true or false expected.",
                      var_name, var_value);
              err_flag = true;
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
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): true or false expected.",
                      var_name, var_value);
              err_flag = true;
              continue;
            }
          set_options |= FLAG_EXPORT_PRIVILEGED_PORT;
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
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): true or false expected.",
                      var_name, var_value);
              err_flag = true;
              continue;
            }
          set_options |= EXPORT_OPTION_USE_PNFS;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_DELEG))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_USE_DELEG) == FLAG_EXPORT_USE_DELEG)
            {
              DEFINED_TWICE_WARNING("FLAG_EXPORT_USE_DELEG");
              continue;
            }

          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->options |= EXPORT_OPTION_USE_DELEG;
              break;

            case 0:
              /*default (false) */
              break;

            default:           /* error */
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): true or false expected.",
                      var_name, var_value);
              err_flag = true;
              continue;
            }
          set_options |= EXPORT_OPTION_USE_DELEG;
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
                      "NFS READ_EXPORT: ERROR: Invalid value for '%s' (%s): true or false expected.",
                      var_name, var_value);
              err_flag = true;
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
          uint64_t offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxOffsetWrite: \"%s\"",
                      var_value);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxOffsetWrite = offset;
          p_entry->options |= EXPORT_OPTION_MAXOFFSETWRITE;

          set_options |= FLAG_EXPORT_MAX_OFF_WRITE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_CACHE_SIZE))
        {
          uint64_t offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxCacheSize: \"%s\"",
                      var_value);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxCacheSize = offset;
          p_entry->options |= EXPORT_OPTION_MAXCACHESIZE;

          set_options |= FLAG_EXPORT_MAX_CACHE_SIZE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_READ))
        {
          uint64_t offset;
          char *end_ptr;

          errno = 0;
          offset = strtoll(var_value, &end_ptr, 10);

          if(end_ptr == NULL || *end_ptr != '\0' || errno != 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ_EXPORT: ERROR: Invalid MaxOffsetRead: \"%s\"",
                      var_value);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->MaxOffsetRead = offset;
          p_entry->options |= EXPORT_OPTION_MAXOFFSETREAD;

          set_options |= FLAG_EXPORT_MAX_OFF_READ;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COMMIT))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->use_commit = true;
              break;

            case 0:
              p_entry->use_commit = false;
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): true or false expected.",
                        var_name, var_value);
                err_flag = true;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_GANESHA_WRITE_BUFFER))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->use_ganesha_write_buffer = true;
              break;

            case 0:
              p_entry->use_ganesha_write_buffer = false;
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): true or false expected.",
                        var_name, var_value);
                err_flag = true;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COOKIE_VERIFIER))
        {
          switch (StrToBoolean(var_value))
            {
            case 1:
              p_entry->UseCookieVerifier = true;
              break;

            case 0:
              p_entry->UseCookieVerifier = false;
              break;

            default:           /* error */
              {
                LogCrit(COMPONENT_CONFIG,
                        "NFS READ_EXPORT: ERROR: Invalid value for %s (%s): true or false expected.",
                        var_name, var_value);
                err_flag = true;
                continue;
              }
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL))
        {
	  if(p_entry->export_hdl != NULL)
	    {
	      LogCrit(COMPONENT_CONFIG,
		      "FSAL is already defined as (%s), new attempt = (%s)",
		      p_entry->export_hdl->fsal->ops->get_name(p_entry->export_hdl->fsal),
		      var_value);
	      continue;
	    }
	  fsal_hdl = lookup_fsal(var_value);
	  if(fsal_hdl != NULL)
	    {
              fsal_status_t expres = fsal_hdl->ops->create_export(fsal_hdl,
								  p_entry->fullpath, /* correct path? */
								  p_entry->FS_specific,
								  p_entry,
								  NULL, /* no stacked fsals for now */
                                                                  &fsal_up_top,
								  &p_entry->export_hdl);
              if(FSAL_IS_ERROR(expres))
 	        {
	          LogCrit(COMPONENT_CONFIG,
			  "Could not create FSAL export for %s", p_entry->fullpath);
                  err_flag = true;
                }
              fsal_hdl->ops->put(fsal_hdl); /* unlock the fsal */
            }
          else
	    {
		    LogCrit(COMPONENT_CONFIG,
			    "FSAL %s is not loaded!", var_value);
	    }
	}
      else
        {
          LogCrit(COMPONENT_CONFIG,
                  "NFS READ_EXPORT: WARNING: Unknown option: %s",
                  var_name);
        }

    }
/** @TODO at some point, have a global config def for the default FSAL when
 * an export doesn't supply it.  Right now, it is VFS for lack of a better
 * idea.
 */
  if(p_entry->export_hdl == NULL)
    {
      LogMajor(COMPONENT_CONFIG,
	      "No FSAL for this export defined. Fallback to using VFS");
      fsal_hdl = lookup_fsal("VFS"); /* should have a "Default_FSAL" param... */
      if(fsal_hdl != NULL)
        {
          fsal_status_t expres = fsal_hdl->ops->create_export(fsal_hdl,
							      p_entry->fullpath, /* correct path? */
							      p_entry->FS_specific,
							      p_entry,
							      NULL, /* no stacked fsals for now */
                                                              &fsal_up_top,
							      &p_entry->export_hdl);
          if(FSAL_IS_ERROR(expres))
            {
	      LogCrit(COMPONENT_CONFIG,
		      "Could not create FSAL export for %s", p_entry->fullpath);
              err_flag = true;
            }
          fsal_hdl->ops->put(fsal_hdl);
        }
      else
        {
	  LogCrit(COMPONENT_CONFIG,
		  "HELP! even VFS FSAL is not resident!");
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

      err_flag = true;
    }

  if (
      ((set_options & FLAG_EXPORT_ACCESSTYPE) || (set_options & FLAG_EXPORT_ACCESS_LIST)) &&
      (set_options & FLAG_EXPORT_ACCESSTYPE_LIST))
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ_EXPORT: ERROR: %s list cannot be used when %s and/or %s are used in the same export entry config.",
              CONF_EXPORT_READWRITE_ACCESS, CONF_EXPORT_ACCESSTYPE,
              CONF_EXPORT_ACCESS);
      err_flag = true;
    }

  if ((set_options & FLAG_EXPORT_ACCESSTYPE) || (set_options & FLAG_EXPORT_ACCESS_LIST))
    p_entry->new_access_list_version = false;
  else
    p_entry->new_access_list_version = true;

  /* check if there had any error.
   * if so, free the p_entry and return an error.
   */
  if(err_flag)
    {
      gsh_free(p_entry);
      return -1;
    }

  *pp_export = p_entry;

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return 0;

}

static char *client_root_access[] = { "*" };

/**
 * @brief builds an export entry for '/' with default parameters
 *
 * @return Root export.
 */

exportlist_t *BuildDefaultExport()
{
  exportlist_t *p_entry;

  /* allocates new export entry */
  p_entry = gsh_malloc(sizeof(exportlist_t));

  if(p_entry == NULL)
    return NULL;

  /** @todo set default values here */

  p_entry->next = NULL;
  p_entry->options = 0;
  p_entry->status = EXPORTLIST_OK;
  p_entry->clients.num_clients = 0;
  p_entry->access_type = ACCESSTYPE_RW;
  p_entry->anonymous_uid = ANON_UID;
  p_entry->MaxOffsetWrite = 0;
  p_entry->MaxOffsetRead = 0;
  p_entry->MaxCacheSize = 0;

  /* by default, we support auth_none and auth_sys */
  p_entry->options |= EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* by default, we support all NFS versions supported by the core and both transport protocols */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->options |= EXPORT_OPTION_NFSV4;
  p_entry->options |= EXPORT_OPTION_UDP | EXPORT_OPTION_TCP;

  p_entry->filesystem_id.major = 101;
  p_entry->filesystem_id.minor = 101;

  p_entry->MaxWrite = 0x100000;
  p_entry->MaxRead = 0x100000;
  p_entry->PrefWrite = 0x100000;
  p_entry->PrefRead = 0x100000;
  p_entry->PrefReaddir = 0x100000;

  strcpy(p_entry->FS_specific, "");
  strcpy(p_entry->FS_tag, "ganesha");

  p_entry->id = 1;

  strcpy(p_entry->fullpath, "/");
  strcpy(p_entry->dirname, "/");
  strcpy(p_entry->fsname, "");
  strcpy(p_entry->pseudopath, "/");
  strcpy(p_entry->referral, "");

  p_entry->UseCookieVerifier = true;

  /**
   * Grant root access to all clients
   */
  nfs_AddClientsToExportList(p_entry, 1, client_root_access, EXPORT_OPTION_ROOT);

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return p_entry;

}

/**
 * @brief Read the export entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the export list
 * @param[out] ppexportlist The export list
 *
 * @return A negative value on error,
 *         the number of export entries else.
 */
int ReadExports(config_file_t in_config,        /* The file that contains the export list */
                exportlist_t ** ppexportlist)   /* Pointer to the export list */
{

  int nb_blk, rc, i;
  char *blk_name;
  int err_flag = false;

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
              err_flag = true;
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

static const char *
cidr_net(unsigned int addr, unsigned int netmask, char *buf, socklen_t len)
{
        unsigned int rb = ntohl(netmask);
        int bitcnt = 33;

        if (inet_ntop(AF_INET, &addr, buf, len) == NULL)
                return "???";

        /* Get the rightmost non-zero bit */
        rb &= - rb;
        if(!rb) {
                bitcnt = 0;
        } else while (rb) {
                rb >>= 1;
                bitcnt--;
        }

        rb = strlen(buf);
        snprintf(buf+rb, len - rb, "/%d", bitcnt);
        return buf;
}

/**
 * @brief Match a specific option in the client export list
 *
 * @param[in]  hostaddr      Host to search for
 * @param[in]  clients       Client list to search
 * @param[out] pclient_found Matching entry
 * @param[in]  export_option Option to search for
 *
 * @return true if found, false otherwise.
 */
bool export_client_match(sockaddr_t *hostaddr,
			 exportlist_client_t *clients,
			 exportlist_client_entry_t *pclient_found,
			 unsigned int export_option)
{
  unsigned int i;
  int rc;
  char hostname[MAXHOSTNAMELEN];
  char ipstring[SOCK_NAME_MAX];
  int ipvalid = -1; /* -1 need to print, 0 - invalid, 1 - ok */
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
      char addrbuf[sizeof("255.255.255.255")]; 
      char patbuf[sizeof("255.255.255.255/32")]; 
      exportlist_client_entry_t * p_client = clients->clientarray + i;

      /* Make sure the client entry has the permission flags we're looking for
       * Also make sure we aren't looking at a root client entry when we're not root. */
      if(((p_client->options & export_option) == 0) ||
         ((p_client->options & EXPORT_OPTION_ROOT) != (export_option & EXPORT_OPTION_ROOT)))
        continue;

      switch (p_client->type)
        {
        case HOSTIF_CLIENT:
          LogFullDebug(COMPONENT_DISPATCH,
                       "Test HOSTIF_CLIENT: Test entry %d: %s vs %s",
                       i,
                       inet_ntop(AF_INET, &p_client->client.hostif.clientaddr,
                                 patbuf, sizeof(patbuf)),
                       inet_ntop(AF_INET, &addr, addrbuf, sizeof(addrbuf)));
          if(p_client->client.hostif.clientaddr == addr)
            {
              LogFullDebug(COMPONENT_DISPATCH, "This matches host address");
              *pclient_found = clients->clientarray[i];
              return true;
            }
          break;

        case NETWORK_CLIENT:
          LogFullDebug(COMPONENT_DISPATCH,
                       "Test NETWORK_CLIENT: Test net %s vs %s",
                       cidr_net(p_client->client.network.netaddr,
                                p_client->client.network.netmask,
                                patbuf, sizeof(patbuf)),
                       inet_ntop(AF_INET, &addr, addrbuf, sizeof(addrbuf)));

          if((p_client->client.network.netmask & addr) ==
             p_client->client.network.netaddr)
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches network address for entry %u",
                           i);
              *pclient_found = *p_client;
              return true;
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
          if(innetgr(p_client->client.netgroup.netgroupname, hostname,
		     NULL, NULL) == 1)
            {
              *pclient_found = clients->clientarray[i];
              return true;
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
              *pclient_found = clients->clientarray[i];
              return true;
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
				   "Could not resolve hostame for addr %s... "
			           "not checking if a hostname wildcard matches",
				   inet_ntop(AF_INET, &addr,
				             addrbuf, sizeof(addrbuf)));
                      break;
                    }
                }
            }
          LogFullDebug(COMPONENT_DISPATCH,
                       "Wildcarded hostname: testing if '%s' matches '%s'",
                       hostname, p_client->client.wildcard.wildcard);

          /* At this point 'hostname' should contain the name that was found */
          if(fnmatch(p_client->client.wildcard.wildcard, hostname,
		     FNM_PATHNAME) == 0)
            {
              *pclient_found = clients->clientarray[i];
              return true;
            }
          LogFullDebug(COMPONENT_DISPATCH, "'%s' not matching '%s'",
                       hostname, p_client->client.wildcard.wildcard);

          break;

        case GSSPRINCIPAL_CLIENT:
          /** @todo BUGAZOMEU a completer lors de l'integration de RPCSEC_GSS */
          LogFullDebug(COMPONENT_DISPATCH,
                       "----------> Unsupported type GSS_PRINCIPAL_CLIENT");
          return false;
          break;

       case BAD_CLIENT:
          LogDebug(COMPONENT_DISPATCH,
                  "Bad client in position %u seen in export list", i );
	  continue ;

        default:
           LogCrit(COMPONENT_DISPATCH,
                   "Unsupported client in position %u in export list with type %u", i, p_client->type);
	   continue ;
        }
    }

  /* no export found for this option */
  return false;

}

/**
 * @brief Match a specific option in the client export list
 *
 * @param[in]  paddrv6       Host to search for
 * @param[in]  clients       Client list to search
 * @param[out] pclient_found Matching entry
 * @param[in]  export_option Option to search for
 *
 * @return true if found, false otherwise.
 */
bool export_client_matchv6(struct in6_addr *paddrv6,
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
              return true;
            }
          break;

        default:
          return false;         /* Should never occurs */
          break;
        }
    }

  /* no export found for this option */
  return false;
}

/**
 * @brief Checks if request security flavor is suffcient for the requested export
 *
 * @param[in] req     Related RPC request.
 * @param[in] pexoprt Related export entry
 *
 * @return true if the request flavor exists in the matching export
 * false otherwise
 */
bool nfs_export_check_security(struct svc_req *req, exportlist_t *pexport)
{
  switch (req->rq_cred.oa_flavor)
    {
      case AUTH_NONE:
        if((pexport->options & EXPORT_OPTION_AUTH_NONE) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_NONE",
                    pexport->dirname);
            return false;
          }
        break;

      case AUTH_UNIX:
        if((pexport->options & EXPORT_OPTION_AUTH_UNIX) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_UNIX",
                    pexport->dirname);
            return false;
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
            return false;
          }
        else
          {
            struct svc_rpc_gss_data *gd;
            rpc_gss_svc_t svc;
            gd = SVCAUTH_PRIVATE(req->rq_auth);
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
                      return false;
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
                      return false;
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
                      return false;
                    }
                  break;

                  default:
                    LogInfo(COMPONENT_DISPATCH,
                            "Export %s does not support unknown "
                            "RPCSEC_GSS_SVC %d",
                            pexport->dirname, (int) svc);
                    return false;
              }
          }
      break;
#endif
      default:
        LogInfo(COMPONENT_DISPATCH,
                "Export %s does not support unknown oa_flavor %d",
                pexport->dirname, (int) req->rq_cred.oa_flavor);
        return false;
    }

  return true;
}

/**
 * @brief Checks if a machine is authorized to access an export entry
 *
 * @param[in]     hostaddr         The complete remote address (as a sockaddr_storage to be IPv6 compliant)
 * @param[in]     ptr_req          The related RPC request.
 * @param[in]     pexport          Related export entry (if found, NULL otherwise).
 * @param[in]     nfs_prog         Number for the NFS program.
 * @param[in]     mnt_prog         Number for the MOUNT program.
 * @param[in,out] ht_ip_stats      IP/stats hash table
 * @param[in,out] ip_stats_pool    IP/stats pool
 * @param[in]     user_credentials 
 * @param[out]    pclient_found Client entry found in export list, NULL if nothing was found.
 *
 * @retval EXPORT_PERMISSION_GRANTED on success
 * @retval EXPORT_PERMISSION_DENIED
 * @retval EXPORT_WRITE_ATTEMPT_WHEN_RO
 * @retval EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO
 */
int nfs_export_check_access(sockaddr_t *hostaddr,
                            struct svc_req *ptr_req,
                            exportlist_t * pexport,
                            unsigned int nfs_prog,
                            unsigned int mnt_prog,
                            hash_table_t *ht_ip_stats,
                            pool_t *ip_stats_pool,
                            exportlist_client_entry_t * pclient_found,
                            const struct user_cred *user_credentials,
                            bool proc_makes_write)
{
  int rc;

  if (pexport != NULL)
    {
      if(proc_makes_write && (pexport->access_type == ACCESSTYPE_RO))
        return EXPORT_WRITE_ATTEMPT_WHEN_RO;
      else if(proc_makes_write && (pexport->access_type == ACCESSTYPE_MDONLY_RO))
        return EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO;
    }


  /* For now, no matching client is found */
  memset(pclient_found, 0, sizeof(exportlist_client_entry_t));

  /* PROC NULL is always authorized, in all protocols */
  if(ptr_req->rq_proc == 0)
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Granted NULL proc");
      return EXPORT_PERMISSION_GRANTED;
    }

#ifdef _USE_TIPRC_IPV6
  if(hostaddr->ss_family == AF_INET)
#endif
    /* Increment the stats per client address (for IPv4 Only) */
    if((rc =
        nfs_ip_stats_incr(ht_ip_stats, hostaddr, nfs_prog, mnt_prog,
                          ptr_req)) == IP_STATS_NOT_FOUND)
      {
        if(nfs_ip_stats_add(ht_ip_stats, hostaddr, ip_stats_pool) ==
           IP_STATS_SUCCESS)
          rc = nfs_ip_stats_incr(ht_ip_stats, hostaddr, nfs_prog,
                                 mnt_prog, ptr_req);
      }

#ifdef _USE_TIRPC_IPV6
  if(hostaddr->ss_family == AF_INET)
    {
#endif                          /* _USE_TIRPC_IPV6 */


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
         !memcmp((psockaddr_in6->sin6_addr.s6_addr + 10),
                 &two_bytes_all_1, 2))
        {
          /* Use IP address as a string for wild character access checks. */
          if(!ipvalid)
            {
              LogCrit(COMPONENT_DISPATCH,
                      "Error: Could not convert the IPv6 address to a character string.");
              return EXPORT_PERMISSION_DENIED;
            }

          /* This is an IPv4 address mapped to an IPv6 one. Extract the IPv4 address and proceed with IPv4 autentication */
          memcpy(&hostaddr, (psockaddr_in6->sin6_addr.s6_addr + 12), 4);

          /* Proceed with IPv4 dedicated function */
          /* check if any root access export matches this client */
          if((user_credentials->caller_uid == 0) &&
             export_client_match(hostaddr, &(pexport->clients),
                                 pclient_found, EXPORT_OPTION_ROOT))
            return EXPORT_PERMISSION_GRANTED;
          /* else, check if any access only export matches this client */
          if(proc_makes_write)
            {
              if (export_client_match(hostaddr, &(pexport->clients), pclient_found, EXPORT_OPTION_WRITE_ACCESS))
                return EXPORT_PERMISSION_GRANTED;
              else if (pexport->new_access_list_version &&
                       export_client_match(hostaddr,
                                           &(pexport->clients), pclient_found, EXPORT_OPTION_MD_WRITE_ACCESS))
                {
                  pexport->access_type = ACCESSTYPE_MDONLY;
                  return EXPORT_MDONLY_GRANTED;
                }
            } else { /* request will not write anything */
            if (export_client_match(hostaddr, &(pexport->clients), pclient_found, EXPORT_OPTION_READ_ACCESS))
              return EXPORT_PERMISSION_GRANTED;
            else if (pexport->new_access_list_version &&
                     export_client_match(hostaddr,
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

}

/**
 * @brief Create the root entries for the cached entries.
 *
 * @param[in] pexportlist Export list to be parsed
 *
 * @return true is successfull, false if something wrong occured.
 *
 */
bool nfs_export_create_root_entry(exportlist_t *pexportlist)
{
      exportlist_t *pcurrent = NULL;
      cache_inode_status_t cache_status;
      fsal_status_t fsal_status;
      cache_entry_t *entry = NULL;

      /* loop the export list */

      for(pcurrent = pexportlist; pcurrent != NULL; pcurrent = pcurrent->next)
        {
          /* Lookup for the FSAL Path */
          fsal_status = pcurrent->export_hdl->ops->lookup_path(pcurrent->export_hdl,
                                                               NULL,
							       pcurrent->fullpath,
							       &pcurrent->proot_handle);
          if(FSAL_IS_ERROR(fsal_status))
            {
              LogCrit(COMPONENT_INIT,
                      "Couldn't access the root of the exported namespace, ExportId=%u Path=%s FSAL_ERROR=(%u,%u)",
                      pcurrent->id, pcurrent->fullpath, fsal_status.major,
                      fsal_status.minor);
              return false;
            }
          if( ((pcurrent->options & EXPORT_OPTION_MAXREAD) != EXPORT_OPTION_MAXREAD )) 
             {
	       if ( pcurrent->export_hdl->ops->fs_maxread(pcurrent->export_hdl) > 0)
                  pcurrent->MaxRead = pcurrent->export_hdl->ops->fs_maxread(pcurrent->export_hdl);
               else
                  pcurrent->MaxRead = LASTDEFAULT;
             }
          if( ((pcurrent->options & EXPORT_OPTION_MAXWRITE) != EXPORT_OPTION_MAXWRITE )) 
             {
               if ( pcurrent->export_hdl->ops->fs_maxwrite(pcurrent->export_hdl) > 0)
                  pcurrent->MaxWrite = pcurrent->export_hdl->ops->fs_maxwrite(pcurrent->export_hdl);
               else
                  pcurrent->MaxWrite = LASTDEFAULT;
             }
          LogFullDebug(COMPONENT_INIT,
                      "Set MaxRead MaxWrite for Path=%s Options = 0x%x MaxRead = 0x%llX MaxWrite = 0x%llX",
                      pcurrent->fullpath, pcurrent->options,
                      (long long) pcurrent->MaxRead,
                      (long long) pcurrent->MaxWrite);
             
          /* Add this entry to the Cache Inode as a "root" entry */

          /* cache_inode_make_root returns a cache_entry with
             reference count of 2, where 1 is the sentinel value of
             a cache entry in the hash table.  The export list in
             this case owns the extra reference, but other users of
             cache_inode_make_root MUST put the entry.  In the future
             if functionality is added to dynamically add and remove
             export entries, then the function to remove an export
             entry MUST put the extra reference. */

          cache_status = cache_inode_make_root(pcurrent->proot_handle,
					       &entry);
          if (entry == NULL)
            {
              LogCrit(COMPONENT_INIT,
                      "Error when creating root cached entry for %s, export_id=%d, cache_status=%d",
                      pcurrent->fullpath, pcurrent->id, cache_status);
              return false;
            }
          else
            LogInfo(COMPONENT_INIT,
                    "Added root entry for path %s on export_id=%d",
                    pcurrent->fullpath, pcurrent->id);

          /* Set the entry as a referral if needed */
          if(strcmp(pcurrent->referral, ""))
            {
              /* Set the cache_entry object as a referral by setting the 'referral' field */
              entry->object.dir.referral = pcurrent->referral;
              LogInfo(COMPONENT_INIT, "A referral is set : %s",
                      entry->object.dir.referral);
            }
        }

  /* Note: As mentioned above, we are returning with an extra
     reference to the root entry.  This reference is owned by the
     export list.  If we ever have a function to remove objects from
     the export list, it must return this extra reference. */

  return true;

}

/* Frees current export entry and returns next export entry. */
exportlist_t *RemoveExportEntry(exportlist_t * exportEntry)
{
  exportlist_t *next;
  fsal_status_t fsal_status;

  if (exportEntry == NULL)
    return NULL;

  next = exportEntry->next;
  if(exportEntry->export_hdl != NULL)
    {
      fsal_status = exportEntry->export_hdl->ops->release(exportEntry->export_hdl);
      if(FSAL_IS_ERROR(fsal_status))
        {
	  LogCrit(COMPONENT_CONFIG,
		  "Cannot release export object, quitting");
	  return NULL;
	}
    }
  if (exportEntry->worker_stats != NULL)
    gsh_free(exportEntry->worker_stats);

  gsh_free(exportEntry);
  return next;
}

exportlist_t *GetExportEntry(char *exportPath)
{
  exportlist_t *pexport = NULL;
  exportlist_t *p_current_item = NULL;
  char tmplist_path[MAXPATHLEN];
  char tmpexport_path[MAXPATHLEN];
  int found = 0;

  pexport = nfs_param.pexportlist;

  /*
   * Find the export for the dirname (using as well Path or Tag )
   */
  for(p_current_item = pexport; p_current_item != NULL;
      p_current_item = p_current_item->next)
  {
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
