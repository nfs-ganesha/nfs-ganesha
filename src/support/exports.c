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
#include "config.h"
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
#ifdef USE_NODELIST
#include "nodelist.h"
#endif /* USE_NODELIST */
#include <stdlib.h>
#include <fnmatch.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "export_mgr.h"

extern struct fsal_up_vector fsal_up_top;

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
#define CONF_EXPORT_FS_SPECIFIC        "FS_Specific"
#define CONF_EXPORT_FS_TAG             "Tag"
#define CONF_EXPORT_MAX_OFF_WRITE      "MaxOffsetWrite"
#define CONF_EXPORT_MAX_OFF_READ       "MaxOffsetRead"
#define CONF_EXPORT_MAX_CACHE_SIZE     "MaxCacheSize"
#define CONF_EXPORT_FSAL               "FSAL"
#define CONF_EXPORT_UQUOTA             "User_Quota"
#define CONF_EXPORT_PNFS               "Use_pNFS"
#define CONF_EXPORT_DELEG              "Use_Delegation"
#define CONF_EXPORT_USE_COMMIT          "Use_NFS_Commit"
#define CONF_EXPORT_USE_COOKIE_VERIFIER "UseCookieVerifier"
#define CONF_EXPORT_CLIENT_DEF         "Client"

/** @todo : add encrypt handles option */

/* Internal identifiers */
#define FLAG_EXPORT_ID              0x000000001
#define FLAG_EXPORT_PATH            0x000000002
#define FLAG_EXPORT_SQUASH          0x000000004
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
#define FLAG_EXPORT_ACCESS_LIST     0x004000000
#define FLAG_EXPORT_ACCESSTYPE_LIST 0x008000000
#define FLAG_EXPORT_ANON_GROUP      0x010000000
#define FLAG_EXPORT_ALL_ANON        0x020000000
#define FLAG_EXPORT_ANON_USER       0x040000000
#define FLAG_EXPORT_CACHE_POLICY    0x080000000
#define FLAG_EXPORT_USE_UQUOTA      0x100000000
#define FLAG_EXPORT_USE_COMMIT      0x200000000
#define FLAG_EXPORT_USE_COOKIE_VERIFIER 0x400000000

/* limites for nfs_ParseConfLine */
/* Used in BuildExportEntry() */
#define EXPORT_MAX_CLIENTS   128        /* number of clients */

/**
 * @brief Parse a line with a settable separator and end of line
 *
 * Loop through the tokens and call proc to process them.
 * Proc also takes a void * arg for state/output
 *
 * @param[in] line      Input line
 * @param[in] separator Character used to identify a separator
 * @param[in] proc      function pointer.
 *
 * @note int proc(token, arg)
 *  token null terminated string
 *  arg points to state or other args
 *  returns true if successful, false on errors
 *
 * @return the number of fields found or -1 if error.
 */
int token_to_proc(char *line,
		  char separator,
		  bool (*proc)(char * token, void *arg),
		  void *arg)
{
	int tok_cnt = 0;
	char *p1 = line;
	char *p2 = line;
	bool rc = true;

	while(rc && p2 != NULL) {
		while(isspace(*p1))
			p1++;
		if(*p1 == '\0')
			break;
		p2 = index(p1, separator);
		if(p2 != NULL)
			*p2++ = '\0';
		rc = proc(p1, arg);
		p1 = p2;
		tok_cnt++;
	}
	return (rc && p2 == NULL) ? (tok_cnt) : -1;
}

/**
 * @brief Parse a line with a settable separator and end of line
 *
 * @note Line is modified, returned tokens are returned as pointers to
 * the null terminated string within the original copy of line.
 *
 * @todo deprecated.  Should be replaced by token_to_proc elsewhere.
 *
 * @note if return == -1, the first nbArgv tokens have been found
 *       the last token (Argv[nbArgv - 1] is the remainder of the string.
 *
 * @param[out] Argv     Result array
 * @param[in] nbArgv    Allocated number of entries in the Argv
 * @param[in] line      Input line
 * @param[in] separator Character used to identify a separator
 *
 * @return the number of fields found or -1 if ran off the end.
 */
int nfs_ParseConfLine(char *Argv[],
                      int nbArgv,
                      char *line,
		      char separator)
{
	int tok_index;
	char *p1 = line;
	char *p2 = NULL;

	for(tok_index = 0; tok_index < nbArgv; tok_index++) {
		while(isspace(*p1))
			p1++;
		if(*p1 == '\0')
			break;
		p2 = index(p1, separator);
		Argv[tok_index] = p1;
		if(p2 == NULL)
			break;
		*p2++ = '\0';
		p1 = p2;
	}
	return (p2 == NULL) ? (tok_index + 1) : -1;
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
  *netAddr = ntohl( *netAddr ) ;

  memset( netMask, 0, sizeof( unsigned long ) ) ;
  memcpy( netMask, &pcidr->mask[12], 4 ) ;
  *netAddr = ntohl( *netAddr ) ;

  return 0 ; 
}

static void StrExportOptions(export_perms_t * p_perms,
			     char           * buffer)
{
  char * buf = buffer;

  if((p_perms->options & EXPORT_OPTION_ROOT) == EXPORT_OPTION_ROOT)
    buf += sprintf(buf, "ROOT ");

  if((p_perms->options & EXPORT_OPTION_ALL_ANONYMOUS) == EXPORT_OPTION_ALL_ANONYMOUS)
    buf += sprintf(buf, "ALL SQUASH ");

  if((p_perms->options & EXPORT_OPTION_ACCESS_LIST) == EXPORT_OPTION_ACCESS_LIST)
    buf += sprintf(buf, "ACCESS LIST ");

  if((p_perms->options & EXPORT_OPTION_RW_ACCESS) == EXPORT_OPTION_RW_ACCESS)
    buf += sprintf(buf, "RW");
  else if((p_perms->options & EXPORT_OPTION_READ_ACCESS) == EXPORT_OPTION_READ_ACCESS)
    buf += sprintf(buf, "RO");
  else if((p_perms->options & EXPORT_OPTION_WRITE_ACCESS) == EXPORT_OPTION_WRITE_ACCESS)
    buf += sprintf(buf, "WO");
  else if((p_perms->options & EXPORT_OPTION_MD_ACCESS) == EXPORT_OPTION_MD_ACCESS)
    buf += sprintf(buf, "MD RW");
  else if((p_perms->options & EXPORT_OPTION_MD_READ_ACCESS) == EXPORT_OPTION_MD_READ_ACCESS)
    buf += sprintf(buf, "MD RO");
  else if((p_perms->options & EXPORT_OPTION_MD_WRITE_ACCESS) == EXPORT_OPTION_MD_WRITE_ACCESS)
    buf += sprintf(buf, "MD WO");
  else if((p_perms->options & EXPORT_OPTION_ACCESS_TYPE) != 0)
    buf += sprintf(buf, "%08x", p_perms->options & EXPORT_OPTION_ACCESS_TYPE);
  else
    buf += sprintf(buf, "NONE");

  if((p_perms->options & EXPORT_OPTION_NOSUID) == EXPORT_OPTION_NOSUID)
    buf += sprintf(buf, ", NOSUID");
  if((p_perms->options & EXPORT_OPTION_NOSGID) == EXPORT_OPTION_NOSGID)
    buf += sprintf(buf, ", NOSUID");

  if((p_perms->options & EXPORT_OPTION_AUTH_NONE) == EXPORT_OPTION_AUTH_NONE)
    buf += sprintf(buf, ", AUTH_NONE");
  if((p_perms->options & EXPORT_OPTION_AUTH_UNIX) == EXPORT_OPTION_AUTH_UNIX)
    buf += sprintf(buf, ", AUTH_SYS");
  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_NONE) == EXPORT_OPTION_RPCSEC_GSS_NONE)
    buf += sprintf(buf, ", RPCSEC_GSS_NONE");
  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_INTG) == EXPORT_OPTION_RPCSEC_GSS_INTG)
    buf += sprintf(buf, ", RPCSEC_GSS_INTG");
  if((p_perms->options & EXPORT_OPTION_RPCSEC_GSS_PRIV) == EXPORT_OPTION_RPCSEC_GSS_PRIV)
    buf += sprintf(buf, ", RPCSEC_GSS_PRIV");

  buf += sprintf(buf, ", ");

  if((p_perms->options & EXPORT_OPTION_NFSV2) == EXPORT_OPTION_NFSV2)
    buf += sprintf(buf, "2");
  if((p_perms->options & EXPORT_OPTION_NFSV3) == EXPORT_OPTION_NFSV3)
    buf += sprintf(buf, "3");
  if((p_perms->options & EXPORT_OPTION_NFSV4) == EXPORT_OPTION_NFSV4)
    buf += sprintf(buf, "4");
  if((p_perms->options & (EXPORT_OPTION_NFSV2 |
                EXPORT_OPTION_NFSV3 |
                EXPORT_OPTION_NFSV4)) == 0)
    buf += sprintf(buf, "NONE");

  if((p_perms->options & EXPORT_OPTION_UDP) == EXPORT_OPTION_UDP)
    buf += sprintf(buf, ", UDP");
  if((p_perms->options & EXPORT_OPTION_TCP) == EXPORT_OPTION_TCP)
    buf += sprintf(buf, ", TCP");

  if((p_perms->options & EXPORT_OPTION_USE_PNFS) == EXPORT_OPTION_USE_PNFS)
    buf += sprintf(buf, ", PNFS");
  if((p_perms->options & EXPORT_OPTION_USE_UQUOTA) == EXPORT_OPTION_USE_UQUOTA)
    buf += sprintf(buf, ", UQUOTA");

  buf += sprintf(buf, ", anon_uid=%d",
                 (int)p_perms->anonymous_uid);
  buf += sprintf(buf, ", anon_gid=%d",
                 (int)p_perms->anonymous_gid);
}

void LogClientListEntry(log_components_t            component,
                        exportlist_client_entry_t * entry)
{
  char perms[1024];
  char addr[INET6_ADDRSTRLEN];
  char *paddr = addr;

  StrExportOptions(&entry->client_perms, perms);

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

#define DEFINED_TWICE_WARNING( _lbl_ , _str_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ %s: WARNING: %s defined twice !!! (ignored)", \
          _lbl_ , _str_ )

#define DEFINED_CONFLICT_WARNING( _lbl_ , _opt1_ , _opt2_ ) \
  LogWarn(COMPONENT_CONFIG,            \
          "NFS READ %s: %s defined when %s was already defined (ignored)", \
          _lbl_ , _opt1_ , _opt2_ )

struct client_args {
	exportlist_client_t *client;
	int option;
	const char *var_name;
};

static bool add_export_client(char *client_hostname, void *arg)
{
	struct client_args *argp = (struct client_args *)arg;
	struct addrinfo *info;
	exportlist_client_entry_t *p_client;
	unsigned long netMask;
	unsigned long netAddr;

	/* Allocate a new export client entry */
	p_client = gsh_calloc(1, sizeof(exportlist_client_entry_t));

	if(p_client == NULL) {
		LogCrit(COMPONENT_CONFIG,
			"Unable to allocate memory for export client %s",
			client_hostname);
		return false;
        }

	/* Set client options */
	p_client->client_perms.options = argp->option;

	/* using netdb to get information about the hostname */
	if((client_hostname[0] == '*') && (client_hostname[1] == '\0')) {
		p_client->type = MATCH_ANY_CLIENT;
		LogDebug(COMPONENT_CONFIG,
			 "entry %p: %s is match any client",
			 p_client, argp->var_name);
        } else if(client_hostname[0] == '@') {
		/* Entry is a netgroup definition */
		if(strmaxcpy(p_client->client.netgroup.netgroupname,
			     client_hostname + 1,
			     sizeof(p_client->client.netgroup.netgroupname)) == -1) {
			LogCrit(COMPONENT_CONFIG,
				"netgroup %s too long, ignoring",
				client_hostname);
			gsh_free(p_client);
			return true;
		}
		p_client->type = NETGROUP_CLIENT;
		LogDebug(COMPONENT_CONFIG,
			 "entry %p: %s to netgroup %s",
			 p_client, argp->var_name,
			 p_client->client.netgroup.netgroupname);
        } else if(index(client_hostname, '/') != NULL &&
		  nfs_LookupNetworkAddr(client_hostname,
                                        &netAddr,
                                        &netMask) == 0) {
		/* Entry is a network definition */
		p_client->client.network.netaddr = netAddr;
		p_client->client.network.netmask = netMask;
		p_client->type = NETWORK_CLIENT;
		LogDebug(COMPONENT_CONFIG,
			 "entry %p: %s to network %s = %d.%d.%d.%d/%d.%d.%d.%d",
			 p_client, argp->var_name,
			 client_hostname,
			 (int)(ntohl(p_client->client.network.netaddr) >> 24),
			 (int)((ntohl(p_client->client.network.netaddr) >> 16) & 0xFF),
			 (int)((ntohl(p_client->client.network.netaddr) >> 8) & 0xFF),
			 (int)(ntohl(p_client->client.network.netaddr) & 0xFF),
			 (int)(ntohl(p_client->client.network.netmask) >> 24),
			 (int)((ntohl(p_client->client.network.netmask) >> 16) & 0xFF),
			 (int)((ntohl(p_client->client.network.netmask) >> 8) & 0xFF),
			 (int)(ntohl(p_client->client.network.netmask) & 0xFF));
        } else if(index(client_hostname, '*') != NULL ||
		  index(client_hostname, '?') != NULL) {
		/* this is a wildcarded host if it contains '*' or '?' */
		p_client->type = WILDCARDHOST_CLIENT;
		if(strmaxcpy(p_client->client.wildcard.wildcard,
			     client_hostname,
			     sizeof(p_client->client.wildcard.wildcard)) == -1) {
			LogCrit(COMPONENT_CONFIG,
				"host wildcard %s too long, ignoring",
				client_hostname);
			gsh_free(p_client);
			return true;
		}
		LogFullDebug(COMPONENT_CONFIG,
			     "entry %p: %s to wildcard \"%s\"",
			     p_client, argp->var_name,
			     client_hostname);
	} else if( getaddrinfo(client_hostname, NULL, NULL, &info) == 0) {
		struct addrinfo *ip;

		for(ip = info; ip != NULL; ip = ip->ai_next) {
			/* Entry is a hostif */
			if(p_client == NULL) {
				/* Allocate a new export client entry */
				p_client = gsh_calloc(1,
						      sizeof(exportlist_client_entry_t));
				if(p_client == NULL) {
					LogCrit(COMPONENT_CONFIG,
						"Unable to allocate memory for export client %s",
						client_hostname);
					return false;
				}
				/* Set client options */
				p_client->client_perms.options = argp->option;
			}
			if(ip->ai_family == AF_INET) {
				struct in_addr infoaddr
					= ((struct sockaddr_in *)ip->ai_addr)->sin_addr;
				memcpy(&(p_client->client.hostif.clientaddr),
				       &infoaddr,
				       sizeof(struct in_addr));
				p_client->type = HOSTIF_CLIENT;
				LogDebug(COMPONENT_CONFIG,
					 "entry %p: %s to client %s = %d.%d.%d.%d",
					 p_client, argp->var_name,
					 client_hostname, 
					 (int)(ntohl(p_client->client.hostif.clientaddr) >> 24),
					 (int)((ntohl(p_client->client.hostif.clientaddr) >> 16) & 0xFF),
					 (int)((ntohl(p_client->client.hostif.clientaddr) >> 8) & 0xFF),
					 (int)(ntohl(p_client->client.hostif.clientaddr) & 0xFF));
			} else if(ip->ai_family == AF_INET6) {
				struct in6_addr infoaddr
					= ((struct sockaddr_in6 *)ip->ai_addr)->sin6_addr;
				/* IPv6 address */
				memcpy(&(p_client->client.hostif.clientaddr6),
				       &infoaddr,
				       sizeof(struct in6_addr));
				p_client->type = HOSTIF_CLIENT_V6;
				LogDebug(COMPONENT_CONFIG,
					 "entry %p: %s to client %s = IPv6",
					 p_client, argp->var_name,
					 client_hostname);
			} else {
				continue;
			}
			glist_add_tail(&argp->client->client_list, &p_client->cle_list);
			argp->client->num_clients++;
			p_client = NULL;
		}
		freeaddrinfo(info);
		return true;
        } else {
		/* Last case: client could not be identified, DNS failed. */
		LogCrit(COMPONENT_CONFIG,
			"Unknown client %s (DNS failed)",
			client_hostname);
		gsh_free(p_client);
		return true;
	}
	glist_add_tail(&argp->client->client_list, &p_client->cle_list);
	argp->client->num_clients++;
	return true;
}

static int parseAccessParam(char *var_name,
		     char *var_value,
                     exportlist_client_t * clients,
                     int                   access_option,
                     const char          * label)
{
  int rc;
  char *expanded_node_list;
  struct client_args args;

#ifdef USE_NODELIST
  /* temp array of clients */
  int count;

  LogFullDebug(COMPONENT_CONFIG,
               "Parsing %s=\"%s\"",
               var_name, var_value);

  /* expends host[n-m] notations */
  count =
    nodelist_common_condensed2extended_nodelist(var_value, &expanded_node_list);

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
#else
  expanded_node_list = var_value;
#endif /* USE_NODELIST */

  args.client = clients;
  args.option = access_option;
  args.var_name = var_name;

  /*
   * Search for coma-separated list of hosts, networks and netgroups
   */
  rc = token_to_proc(expanded_node_list,
		     ',',
		     add_export_client,
		     &args);
  if(rc < 0)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ %s: Invalid client found in %s=\"%s\"",
              label, var_name, var_value);
    }

#ifdef USE_NODELIST
  /* free the buffer the nodelist module has allocated */
  free(expanded_node_list);
#endif /* USE_NODELIST */

  return rc;
}

static void FreeClientList(exportlist_client_t * clients)
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

static bool parse_int32_t(char *var_value,
			  int32_t min,
			  int32_t max,
			  uint64_t *set_opts,
			  uint64_t option,
			  const char *blk_name,
			  const char *var_name,
			  bool *err_flag,
			  int32_t *valptr)
{
	int32_t val;
	char *end_ptr;

	if((*set_opts & option) == option) {
		LogWarn(COMPONENT_CONFIG,
			"NFS READ %s: WARNING: %s defined twice !!! (ignored)",
			blk_name,
			var_name);
		return false;
	}
	errno = 0;
	val = strtol(var_value, &end_ptr, 10);
	if(end_ptr == NULL || *end_ptr != '\0' || errno != 0) {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Invalid %s: \"%s\"",
			blk_name, var_name, var_value);
		*err_flag = true;
		return false;
	}
	if((min < max) &&    /* min < max means range check... */
	   (val < min || val > max)) {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: %s out of range: \"%d\"",
			blk_name, var_name, val);
		*err_flag = true;
		return false;
	}
	*valptr = val;
	*set_opts |= option;
	return true;
}
			  
static bool parse_int64_t(char *var_value,
			  int64_t min,
			  int64_t max,
			  uint64_t *set_opts,
			  uint64_t option,
			  const char *blk_name,
			  const char *var_name,
			  bool *err_flag,
			  int64_t *valptr)
{
	int64_t val;
	char *end_ptr;

	if((*set_opts & option) == option) {
		LogWarn(COMPONENT_CONFIG,
			"NFS READ %s: WARNING: %s defined twice !!! (ignored)",
			blk_name,
			var_name);
		return false;
	}
	errno = 0;
	val = strtoll(var_value, &end_ptr, 10);
	if(end_ptr == NULL || *end_ptr != '\0' || errno != 0) {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Invalid %s: \"%s\"",
			blk_name, var_name, var_value);
		*err_flag = true;
		return false;
	}
	if((min < max) &&    /* min < max means range check... */
	   (val < min || val > max)) {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: %s out of range: \"%ld\"",
			blk_name, var_name, val);
		*err_flag = true;
		return false;
	}
	*valptr = val;
	*set_opts |= option;
	return true;
}

static bool parse_bool(char *var_value,
		       uint64_t *set_opts,
		       uint64_t option,
		       const char *blk_name,
		       const char *var_name,
		       bool *err_flag,
		       bool *valptr)
{
	int val;

	if((*set_opts & option) == option) {
		LogWarn(COMPONENT_CONFIG,
			"NFS READ %s: WARNING: %s defined twice !!! (ignored)",
			blk_name,
			var_name);
		return false;
	}
	val = StrToBoolean(var_value);
	if(val == -1) {
		LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Invalid value for %s (%s): TRUE or FALSE expected.",
                      blk_name, var_name, var_value);
		*err_flag = true;
		return false;
	}
	*valptr = !!val;
	*set_opts |= option;
	return true;
}

struct proto_xxx_args {
	export_perms_t *perms;
	const char *label;
};

static bool proto_vers(char *tok, void *void_arg)
{
	struct proto_xxx_args *args = void_arg;

	if(!STRCMP(tok, "3")) {
		if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0) {
			args->perms->options |= EXPORT_OPTION_NFSV3;
			goto out;
		} else {
			LogInfo(COMPONENT_CONFIG,
				"NFS READ %s: NFS version 3 is  globally disabled.",
				args->label);
		}
	} else if(!STRCMP(tok, "4")) {
		if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0) {
			args->perms->options |= EXPORT_OPTION_NFSV4;
			goto out;
		} else {
			LogInfo(COMPONENT_CONFIG,
				"NFS READ %s: NFS version 4 is globally disabled.",
				args->label);
		}
	} else {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Invalid NFS version \"%s\"."
			" Values can be: 2, 3, 4.",
			args->label, tok);
	}
	return false;

out:
	return true;
}

static bool proto_trans(char *tok, void *void_arg)
{
	struct proto_xxx_args *args = void_arg;

	if(!STRCMP(tok, "UDP")) {
		args->perms->options |= EXPORT_OPTION_UDP;
	} else if(!STRCMP(tok, "TCP")) {
		args->perms->options |= EXPORT_OPTION_TCP;
	} else {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Invalid transport \"%s\". Values can be: UDP, TCP.",
			args->label, tok);
		return false;
	}
	return true;
}

static bool proto_sectype(char *tok, void *void_arg)
{
	struct proto_xxx_args *args = void_arg;

	if(!STRCMP(tok, "none")) {
		args->perms->options |= EXPORT_OPTION_AUTH_NONE;
	} else if(!STRCMP(tok, "sys")) {
		args->perms->options |= EXPORT_OPTION_AUTH_UNIX;
	} else if(!STRCMP(tok, "krb5")) {
		args->perms->options |= EXPORT_OPTION_RPCSEC_GSS_NONE;
	} else if(!STRCMP(tok, "krb5i")) {
		args->perms->options |= EXPORT_OPTION_RPCSEC_GSS_INTG;
	} else if(!STRCMP(tok, "krb5p")) {
		args->perms->options |= EXPORT_OPTION_RPCSEC_GSS_PRIV;
	} else {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Invalid SecType \"%s\"."
			" Values can be: none, sys, krb5, krb5i, krb5p.",
			args->label, tok);
		return false;
	}
	return true;
}
	
static int BuildExportClient(config_item_t block,
			     exportlist_client_t *client_list)
{
	export_perms_t client_perms;
	exportlist_client_t   access_list;
	exportlist_client_t *p_access_list;
	export_perms_t *perms;
	struct glist_head *glist;
	uint64_t mandatory_options = FLAG_EXPORT_ACCESS_LIST;
	uint64_t set_options = 0;
	int item_cnt, i, rc;
	config_item_t item;
	char *var_name;
	char *var_value;
	char *label = "Client";
	bool err_flag   = false;

	/* Init the perms and access list */
	perms = &client_perms;
	p_access_list = &access_list;
	glist_init(&p_access_list->client_list);
	p_access_list->num_clients = 0;

	/* by default, we support auth_none and auth_sys */
	perms->options = (EXPORT_OPTION_AUTH_NONE |
			  EXPORT_OPTION_AUTH_UNIX |
			  EXPORT_OPTION_TRANSPORTS);

	/* Default anonymous uid and gid */
	perms->anonymous_uid = (uid_t) ANON_UID;
	perms->anonymous_gid = (gid_t) ANON_GID;

	/* by default, we support all NFS versions supported by the core and
	 * both transport protocols
	 */
	if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
		perms->options |= EXPORT_OPTION_NFSV3;

	if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
		perms->options |= EXPORT_OPTION_NFSV4;

	item_cnt = config_GetNbItems(block);
	for(i = 0; i < item_cnt; i++) {
		item = config_GetItemByIndex(block, i);
		rc = config_GetKeyValue(item, &var_name, &var_value);

		if((rc != 0) || (var_value == NULL)) {
			if(rc == -2)
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: name \"%s\" was truncated",
					label, var_name);
			else if(rc == -3)
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: value in \"%s\"=\"%s\" was truncated",
					label, var_name, var_value);
			else
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: internal error %d",
					label, rc);
			return -1;
		}

		if(!STRCMP(var_name, CONF_EXPORT_ACCESS)) {
			/* Notice that as least one of the three options
			 * Root_Access, R_Access, or RW_Access has been specified.
			 */
			set_options |= FLAG_EXPORT_ACCESS_LIST;
			if(*var_value == '\0')
				continue;
			perms->options |= EXPORT_OPTION_ACCESS_OPT_LIST;
			parseAccessParam(var_name, var_value, p_access_list,
					 EXPORT_OPTION_ACCESS_OPT_LIST,
					 label);
		} else if(!STRCMP(var_name, CONF_EXPORT_ACCESSTYPE)) {
			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_ACCESSTYPE) == FLAG_EXPORT_ACCESSTYPE) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_ACCESSTYPE);
				continue;
			}
			set_options |= FLAG_EXPORT_ACCESSTYPE;
			perms->options &= ~EXPORT_OPTION_ACCESS_TYPE;
			if(!STRCMP(var_value, "RW")) {
				perms->options |= (EXPORT_OPTION_RW_ACCESS |
						     EXPORT_OPTION_MD_ACCESS);
			} else if(!STRCMP(var_value, "RO")) {
				perms->options |= (EXPORT_OPTION_READ_ACCESS |
						     EXPORT_OPTION_MD_READ_ACCESS);
			} else if(!STRCMP(var_value, "MDONLY")) {
				perms->options |= EXPORT_OPTION_MD_ACCESS;
			} else if(!STRCMP(var_value, "MDONLY_RO")) {
				perms->options |= EXPORT_OPTION_MD_READ_ACCESS;
			} else if(!STRCMP(var_value, "NONE")) {
				LogFullDebug(COMPONENT_CONFIG,
					     "Export access type NONE");
			} else {
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: Invalid access type \"%s\"."
					" Values can be: RW, RO, MDONLY, MDONLY_RO, NONE.",
					label, var_value);
				err_flag = true;
				continue;
			}
		} else if(!STRCMP(var_name, CONF_EXPORT_NFS_PROTO)) {
			struct proto_xxx_args proto_args;
			int count;

			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_NFS_PROTO) == FLAG_EXPORT_NFS_PROTO) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_NFS_PROTO);
				continue;
			}
			set_options |= FLAG_EXPORT_NFS_PROTO;
			perms->options &= ~EXPORT_OPTION_PROTOCOLS;
			proto_args.label = label;
			proto_args.perms = perms;
			count = token_to_proc(var_value, ',',
					      proto_vers,
					      &proto_args);
			if(count < 0) {
				err_flag = true;
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: NFS protocols list error",
					label);
				continue;
			}

			/* check that at least one nfs protocol has been specified */
			if((perms->options & EXPORT_OPTION_PROTOCOLS) == 0) {
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: Empty NFS_protocols list",
					label);
				err_flag = true;
			}
		} else if(!STRCMP(var_name, CONF_EXPORT_TRANS_PROTO)) {
			struct proto_xxx_args proto_args;
			int count;

			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_TRANS_PROTO) == FLAG_EXPORT_TRANS_PROTO) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_TRANS_PROTO);
				continue;
			}

			/* reset TRANS proto flags (clean defaults) */
			perms->options &= ~EXPORT_OPTION_TRANSPORTS;

			/*
			 * Search for coma-separated list of TRANSprotos
			 */
			proto_args.label = label;
			proto_args.perms = perms;
			count = token_to_proc(var_value, ',',
					      proto_trans,
					      &proto_args);
			if(count < 0) {
				err_flag = true;
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: Protocol transport error",
					label);
				continue;
			}

			/* check that at least one TRANS protocol has been specified */
			if((perms->options & EXPORT_OPTION_TRANSPORTS) == 0) {
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: Empty transport list",
					label);
				err_flag = true;
			}
			set_options |= FLAG_EXPORT_TRANS_PROTO;
		} else if(!STRCMP(var_name, CONF_EXPORT_ALL_ANON)) {
			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_ALL_ANON) == FLAG_EXPORT_ALL_ANON) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_ALL_ANON);
				continue;
			}
			set_options |= FLAG_EXPORT_ALL_ANON;
			/* Check for conflicts */
			if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH) {
				DEFINED_CONFLICT_WARNING(label,
							 CONF_EXPORT_ALL_ANON,
							 CONF_EXPORT_SQUASH);
				continue;
			}
			if (StrToBoolean(var_value))
				perms->options |= EXPORT_OPTION_ALL_ANONYMOUS;
		} else if(!STRCMP(var_name, CONF_EXPORT_ANON_ROOT)) {
			uid_t anon_uid;

			/* Check for conflicts */
			if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER) {
				DEFINED_CONFLICT_WARNING(label,
							 CONF_EXPORT_ANON_ROOT,
							 CONF_EXPORT_ANON_USER);
				continue;
			}
			if( !parse_int32_t(var_value,
					   0,0,
					   &set_options, FLAG_EXPORT_ANON_ROOT,
					   label, var_name,
					   &err_flag,
					   &anon_uid))
				continue;
			/* set anon_uid */
			perms->anonymous_uid = (uid_t) anon_uid;
		} else if(!STRCMP(var_name, CONF_EXPORT_ANON_USER)) {
			uid_t anon_uid;

			/* Check for conflicts */
			if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT) {
				DEFINED_CONFLICT_WARNING(label,
							 CONF_EXPORT_ANON_USER,
							 CONF_EXPORT_ANON_ROOT);
				continue;
			}
			if( !parse_int32_t(var_value,
					   0,0,
					   &set_options, FLAG_EXPORT_ANON_USER,
					   label, var_name,
					   &err_flag,
					   &anon_uid))
				continue;
			perms->anonymous_uid = (uid_t) anon_uid;
		} else if(!STRCMP(var_name, CONF_EXPORT_ANON_GROUP)) {
			gid_t anon_gid;

			if( !parse_int32_t(var_value,
					   0,0,
					   &set_options, FLAG_EXPORT_ANON_GROUP,
					   label, var_name,
					   &err_flag,
					   &anon_gid))
				continue;
			perms->anonymous_gid = anon_gid;
		} else if(!STRCMP(var_name, CONF_EXPORT_SECTYPE)) {
			struct proto_xxx_args proto_args;
			int count;

			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_SECTYPE) == FLAG_EXPORT_SECTYPE) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_SECTYPE);
				continue;
			}
			set_options |= FLAG_EXPORT_SECTYPE;
			/* reset security flags (clean defaults) */
			perms->options &= ~EXPORT_OPTION_AUTH_TYPES;

			/*
			 * Search for coma-separated list of sectypes
			 */
			proto_args.label = label;
			proto_args.perms = perms;
			count = token_to_proc(var_value, ',',
					      proto_sectype,
					      &proto_args);
			if(count < 0) {
				err_flag = true;
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: SecType list error",
					label);
				continue;
			}

			/* check that at least one sectype has been specified */
			if((perms->options & EXPORT_OPTION_AUTH_TYPES) == 0)
				LogWarn(COMPONENT_CONFIG,
					"NFS READ %s: Empty SecType",
					label);
		} else if(!STRCMP(var_name, CONF_EXPORT_NOSUID)) {
			bool on;

			if( !parse_bool(var_value,
					&set_options, FLAG_EXPORT_NOSUID,
					label, var_name,
					&err_flag,
					&on))
				continue;
			if(on)
				perms->options |= EXPORT_OPTION_NOSUID;
		} else if(!STRCMP(var_name, CONF_EXPORT_NOSGID)) {
			bool on;

			if( !parse_bool(var_value,
					&set_options, FLAG_EXPORT_NOSGID,
					label, var_name,
					&err_flag,
					&on))
				continue;
			if(on)
				perms->options |= EXPORT_OPTION_NOSGID;
		} else if(!STRCMP(var_name, CONF_EXPORT_PRIVILEGED_PORT)) {
			bool on;

			if( !parse_bool(var_value,
					&set_options, FLAG_EXPORT_PRIVILEGED_PORT,
					label, var_name,
					&err_flag,
					&on))
				continue;
			if(on)
				perms->options |= EXPORT_OPTION_PRIVILEGED_PORT;
		} else if(!STRCMP(var_name, CONF_EXPORT_SQUASH)) {
			/* check if it has not already been set */
			if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH) {
				DEFINED_TWICE_WARNING(label, CONF_EXPORT_SQUASH);
				continue;
			}
			set_options |= FLAG_EXPORT_SQUASH;

			/* Check for conflicts */
			if((set_options & FLAG_EXPORT_ALL_ANON) == FLAG_EXPORT_ALL_ANON) {
				DEFINED_CONFLICT_WARNING(label,
							 CONF_EXPORT_ANON_ROOT,
							 CONF_EXPORT_ALL_ANON);
				continue;
			}

			if(!STRCMP(var_value, "Root") ||
			   !STRCMP(var_value, "Root_Squash") ||
			   !STRCMP(var_value, "RootSquash")) {
				/* Nothing to do, default is root squash */
			} else if(!STRCMP(var_value, "All") ||
				  !STRCMP(var_value, "All_Squash") ||
				  !STRCMP(var_value, "AllSquash")) {
				/* Squash all users */
				perms->options |= EXPORT_OPTION_ALL_ANONYMOUS;
			} else if(!STRCMP(var_value, "No_Root_Squash") ||
				  !STRCMP(var_value, "None") ||
				  !STRCMP(var_value, "NoIdSquash")) {
				/* Allow Root access */
				perms->options |= EXPORT_OPTION_ROOT;
			} else {
				LogCrit(COMPONENT_CONFIG,
					"NFS READ %s: Invalid value for %s (%s): "
					"Root, Root_Squash, RootSquash,"
					"All, All_Squash, AllSquash,"
					"No_Root_Squash, NoIdSquash, or None expected.",
					label, var_name, var_value);
				err_flag = true;
				continue;
			}
		} else {
			LogWarn(COMPONENT_CONFIG,
				"NFS READ %s: Unknown option: %s, ignored",
				label, var_name);
		}
	}
	/** Done parsing, now check for mandatory options */
	if((set_options & mandatory_options) != mandatory_options) {
		if((set_options & FLAG_EXPORT_ACCESS_LIST) !=
		   (FLAG_EXPORT_ACCESS_LIST & mandatory_options))
			LogCrit(COMPONENT_CONFIG,
				"NFS READ %s: Must have the \"%s\" declaration",
				label,
				CONF_EXPORT_ACCESS);
		err_flag = true;
	}
	/* check if there had any error.
	 * if so, free the p_entry and return an error.
	 */
	if(err_flag) {
		LogCrit(COMPONENT_CONFIG,
			"NFS READ %s: Client entry had errors, ignoring",
			label);
		FreeClientList(p_access_list);
		return -1;
	}
	/* Copy the permissions into the client
	 * list entries.
	 */
	glist_for_each(glist, &p_access_list->client_list) {
		exportlist_client_entry_t * p_client_entry;

		p_client_entry = glist_entry(glist, exportlist_client_entry_t, cle_list);
		p_client_entry->client_perms = *perms;
		if(isFullDebug(COMPONENT_CONFIG))
			LogClientListEntry(COMPONENT_CONFIG, p_client_entry);
	}
	/* append this client list to the export
	 */
	glist_add_list_tail(&client_list->client_list,
			    &p_access_list->client_list);
	client_list->num_clients += p_access_list->num_clients;
	return 0;
}

/**
 * @brief Builds an export entry from configuration file
 *
 * Don't stop immediately on error,
 * continue parsing the file, for listing other errors.
 *
 * @param[in]  block     Export configuration block
 *
 * @return 0 on success.
 */

static int BuildExportEntry(config_item_t block)
{
  exportlist_t *p_entry = NULL;
  bool path_matches = false;
  int i, rc;
  config_item_t item;
  int num_items;
  int32_t export_id = -1;
  struct gsh_export *exp;
  char *var_name;
  char *var_value;
  struct fsal_module *fsal_hdl = NULL;
  struct glist_head   * glist;
  exportlist_client_t   access_list;
  exportlist_client_t * p_access_list;
  export_perms_t      * p_perms;
  char                * ppath;
  const char         * label = "Export";
  uint64_t mandatory_options;
  uint64_t set_options = 0;  /* the given options */
  bool err_flag   = false;

  num_items = config_GetNbItems(block);
  if(num_items == 0)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Export block is empty", label);
      return -1;
    }
  var_value = config_GetKeyValueByName(block, CONF_EXPORT_ID);
  if(var_value == NULL)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Export block contains no %s",
	      label, CONF_EXPORT_ID);
      return -1;
    }
  if( !parse_int32_t(var_value,
		     1, USHRT_MAX,
		     &set_options, FLAG_EXPORT_ID,
		     label, var_name,
		     &err_flag, &export_id))
      return -1;
  exp = get_gsh_export(export_id, false);
  if(exp == NULL) /* gsh_calloc error */
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: unable to allocate space for export id %s",
	      label, CONF_EXPORT_ID);
      return -1;
    }
  else if(exp->state != EXPORT_INIT)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: This export is a duplicate of %s",
	      label, CONF_EXPORT_ID);
      put_gsh_export(exp);
      return -1;
    }
  else
    {  /* initialize the exportlist part with the id */
      p_entry = &exp->export;
      set_options &= ~FLAG_EXPORT_ID; /* to warn defined twice nicely */
      if(pthread_mutex_init(&p_entry->exp_state_mutex, NULL) == -1)
        {
	  LogCrit(COMPONENT_CONFIG,
		  "NFS READ %s: could not initialize exp_state_mutex",
		  label);
	  put_gsh_export(exp);
	  remove_gsh_export(export_id);
	  return -1;
	}
      p_entry->use_commit = true;
      p_entry->UseCookieVerifier = true;
      p_entry->UseCookieVerifier = true;
      p_entry->filesystem_id.major = 666;
      p_entry->filesystem_id.minor = 666;
      p_entry->MaxWrite = 16384;
      p_entry->MaxRead = 16384;
      p_entry->PrefWrite = 16384;
      p_entry->PrefRead = 16384;
      p_entry->PrefReaddir = 16384;
      glist_init(&p_entry->exp_state_list);
      glist_init(&p_entry->exp_lock_list);
      glist_init(&p_entry->clients.client_list);
    }
	  
  /* the mandatory options */
  mandatory_options = (FLAG_EXPORT_ID | FLAG_EXPORT_PATH);


  /* Init the access list */
  p_access_list = &access_list;
  glist_init(&p_access_list->client_list);
  p_access_list->num_clients = 0;

  /* by default, we support auth_none and auth_sys */
  p_perms = &p_entry->export_perms;
  p_perms->options = EXPORT_OPTION_AUTH_NONE | EXPORT_OPTION_AUTH_UNIX;

  /* Default anonymous uid and gid */
  p_perms->anonymous_uid = (uid_t) ANON_UID;
  p_perms->anonymous_gid = (gid_t) ANON_GID;

  /* by default, we support all NFS versions supported by the core and
   * both transport protocols
   */
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_perms->options |= EXPORT_OPTION_NFSV3;

  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_perms->options |= EXPORT_OPTION_NFSV4;

  p_perms->options |= EXPORT_OPTION_TRANSPORTS;

  /* parse options for this export entry */

  for(i = 0; i < num_items; i++)
    {
      item = config_GetItemByIndex(block, i);

      if(config_ItemType(item) ==CONFIG_ITEM_BLOCK) {
	      char *blk_name = config_GetBlockName(item);

	      if( !strcasecmp(blk_name, CONF_EXPORT_CLIENT_DEF)) {
		      rc = BuildExportClient(item, &p_entry->clients);
	      } else {
		      LogCrit(COMPONENT_CONFIG,
			      "NFS READ %s: unknown block \"%s\", ignored",
			      label, var_name);
		      err_flag = true;
	      }
	      continue;
      }
      /* get var name and value */
      rc = config_GetKeyValue(item, &var_name, &var_value);

      if((rc != 0) || (var_value == NULL))
        {
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
          err_flag = true;
        }

      if(!STRCMP(var_name, CONF_EXPORT_ID))
        {
          int32_t export_id;

	  if( !parse_int32_t(var_value,
			      1, USHRT_MAX,
			      &set_options, FLAG_EXPORT_ID,
			      label, var_name,
			      &err_flag, &export_id))
		  continue;
	  if(export_id != p_entry->id)
	    {
               LogCrit(COMPONENT_CONFIG,
		       "NFS READ %s: Strange Export_id: (new)\"%d\" != (old)\"%d\"",
		       label, export_id, p_entry->id);
	       err_flag = true;
	       continue;
	    }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PATH))
        {
          struct gsh_export *exp;
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
              err_flag = true;
              continue;
            }

          pathlen = strlen(var_value);

          if(pathlen > MAXPATHLEN)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s \"%s\" too long",
                      label, var_name, var_value);
              err_flag = true;
              continue;
            }

          /* Some admins stuff a '/' at  the end for some reason.
	   * chomp it so we have a /dir/path/basename to work with.
	   * But only if it's a non-root path starting with /. */
	  if ((var_value[pathlen-1] == '/') &&
	      (pathlen > 1) &&
	      (var_value[0] == '/'))
            {
	      var_value[pathlen - 1] = '\0';
            }
	  ppath = var_value;

          exp = get_gsh_export_by_path(ppath);
	  /* Pseudo, Tag, and Export_Id must be unique, Path may be
	   * duplicated if at least Tag or Pseudo is specified (and
	   * unique).
	   */
	  if(exp != NULL && path_matches)
	    {
               LogCrit(COMPONENT_CONFIG,
		       "NFS READ %s: Duplicate Path: \"%s\"",
		       label, ppath);
	       err_flag = true;
	       put_gsh_export(exp);
	       continue;
	    }

	  p_entry->fullpath = gsh_strdup(var_value);

	  /* Remember the entry we found so we can verify Tag and/or Pseudo
	   * is set by the time the EXPORT stanza is complete.
	   */
	  path_matches = true;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ROOT))
        {
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
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

          if(*var_value == '\0')
            {
              continue;
            }
          parseAccessParam(var_name, var_value, p_access_list,
                           EXPORT_OPTION_ACCESS_LIST,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MD_ACCESS))
        {
	  /* Notice that as least one of the three options
	   * Root_Access, R_Access, or RW_Access has been specified.
	   */
	  set_options |= FLAG_EXPORT_ACCESS_LIST;

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

          if(*var_value == '\0')
            {
              continue;
            }
	  parseAccessParam(var_name, var_value, p_access_list,
			   EXPORT_OPTION_RW_ACCESS,
			   label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PSEUDO))
        {
          struct gsh_export *exp;

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
              err_flag = true;
              continue;
            }

          exp = get_gsh_export_by_pseudo(var_value);
          if(exp != NULL)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Duplicate Pseudo: \"%s\"",
                      label, var_value);
              err_flag = true;
	      put_gsh_export(exp);
              continue;
            }

          if(strlen(var_value) > MAXPATHLEN)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = true;
              continue;
            }
	  p_entry->pseudopath = gsh_strdup(var_value);

          p_perms->options |= EXPORT_OPTION_PSEUDO;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ACCESSTYPE))
        {
          /* check if it has not already been set */
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
                      "NFS READ %s: Invalid access type \"%s\"."
		      " Values can be: RW, RO, MDONLY, MDONLY_RO, NONE.",
                      label, var_value);
              err_flag = true;
              continue;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NFS_PROTO))
        {
	  struct proto_xxx_args proto_args;
	  int count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_NFS_PROTO) == FLAG_EXPORT_NFS_PROTO)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_NFS_PROTO);
              continue;
            }

          set_options |= FLAG_EXPORT_NFS_PROTO;

          /* reset nfs proto flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_PROTOCOLS;

          /*
           * Search for coma-separated list of nfsprotos
           */
	  proto_args.label = label;
	  proto_args.perms = p_perms;
	  count = token_to_proc(var_value, ',',
				proto_vers,
				&proto_args);
          if(count < 0)
            {
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: NFS protocols list error",
                      label);

              continue;
            }
          /* check that at least one nfs protocol has been specified */
          if((p_perms->options & EXPORT_OPTION_PROTOCOLS) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Empty NFS_protocols list",
                      label);
              err_flag = true;
            }
        }
      else if(!STRCMP(var_name, CONF_EXPORT_TRANS_PROTO))
        {
	  struct proto_xxx_args proto_args;
	  int count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_TRANS_PROTO) == FLAG_EXPORT_TRANS_PROTO)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_TRANS_PROTO);
              continue;
            }

          /* reset TRANS proto flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_TRANSPORTS;

          /*
           * Search for coma-separated list of TRANSprotos
           */
	  proto_args.label = label;
	  proto_args.perms = p_perms;
	  count = token_to_proc(var_value, ',',
				proto_trans,
				&proto_args);
          if(count < 0)
            {
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Protocol transport error",
                      label);

              continue;
            }

          /* check that at least one TRANS protocol has been specified */
          if((p_perms->options & EXPORT_OPTION_TRANSPORTS) == 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Empty transport list",
                      label);
              err_flag = true;
            }

          set_options |= FLAG_EXPORT_TRANS_PROTO;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_ALL_ANON))
        {
	  bool on;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_SQUASH) == FLAG_EXPORT_SQUASH)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ALL_ANON,
                                       CONF_EXPORT_SQUASH);
              continue;
            }
	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_ALL_ANON,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  if(on)
            p_perms->options |= EXPORT_OPTION_ALL_ANONYMOUS;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_ROOT))
        {
          uid_t anon_uid;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_USER) == FLAG_EXPORT_ANON_USER)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ANON_ROOT,
                                       CONF_EXPORT_ANON_USER);
              continue;
            }

	  if( !parse_int32_t(var_value,
			     0,0,
			     &set_options, FLAG_EXPORT_ANON_ROOT,
			     label, var_name,
			     &err_flag,
			     &anon_uid))
		  continue;
          /* set anon_uid */
          p_perms->anonymous_uid = (uid_t) anon_uid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_USER))
        {
          uid_t anon_uid;

          /* Check for conflicts */
          if((set_options & FLAG_EXPORT_ANON_ROOT) == FLAG_EXPORT_ANON_ROOT)
            {
              DEFINED_CONFLICT_WARNING(label,
                                       CONF_EXPORT_ANON_USER,
                                       CONF_EXPORT_ANON_ROOT);
              continue;
            }

	  if( !parse_int32_t(var_value,
			      0,0,
			      &set_options, FLAG_EXPORT_ANON_USER,
			      label, var_name,
			      &err_flag,
			      &anon_uid))
		  continue;
          p_perms->anonymous_uid = (uid_t) anon_uid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_ANON_GROUP))
        {

          gid_t anon_gid;

	  if( !parse_int32_t(var_value,
			      0,0,
			      &set_options, FLAG_EXPORT_ANON_GROUP,
			      label, var_name,
			      &err_flag,
			      &anon_gid))
		  continue;
          p_perms->anonymous_gid = anon_gid;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_SECTYPE))
        {
	  struct proto_xxx_args proto_args;
	  int count;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_SECTYPE) == FLAG_EXPORT_SECTYPE)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_SECTYPE);
              continue;
            }

          set_options |= FLAG_EXPORT_SECTYPE;

          /* reset security flags (clean defaults) */
          p_perms->options &= ~EXPORT_OPTION_AUTH_TYPES;

          /*
           * Search for coma-separated list of sectypes
           */
	  proto_args.label = label;
	  proto_args.perms = p_perms;
	  count = token_to_proc(var_value, ',',
				proto_sectype,
				&proto_args);
          if(count < 0)
            {
              err_flag = true;
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: SecType list error",
                      label);

              continue;
            }

          /* check that at least one sectype has been specified */
          if((p_perms->options & EXPORT_OPTION_AUTH_TYPES) == 0)
            LogWarn(COMPONENT_CONFIG,
                    "NFS READ %s: Empty SecType",
                    label);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_READ))
        {
          int64_t size;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_MAX_READ,
			      label, var_name,
			      &err_flag,
			      &size))
		  continue;
          p_entry->MaxRead = (uint32_t) size;
          p_perms->options |= EXPORT_OPTION_MAXREAD;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_WRITE))
        {
          int64_t size;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_MAX_WRITE,
			      label, var_name,
			      &err_flag,
			      &size))
		  continue;
          p_entry->MaxWrite = (uint32_t) size;
          p_perms->options |= EXPORT_OPTION_MAXWRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READ))
        {
          int64_t size;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_PREF_READ,
			      label, var_name,
			      &err_flag,
			      &size))
		  continue;
          p_entry->PrefRead = (uint32_t) size;
          p_perms->options |= EXPORT_OPTION_PREFREAD;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_WRITE))
        {
          int64_t size;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_PREF_WRITE,
			      label, var_name,
			      &err_flag,
			      &size))
		  continue;
          p_entry->PrefWrite = (uint32_t) size;
          p_perms->options |= EXPORT_OPTION_PREFWRITE;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PREF_READDIR))
        {
          int64_t size;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_PREF_READDIR,
			      label, var_name,
			      &err_flag,
			      &size))
		  continue;
          p_entry->PrefReaddir = (uint32_t) size;
          p_perms->options |= EXPORT_OPTION_PREFRDDIR;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FSID))
        {
          int64_t major, minor;
          char *end_ptr;

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
              err_flag = true;
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
              err_flag = true;
              continue;
            }

          if(major < 0 || minor < 0)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Filesystem_id out of range: %ld.%ld",
                      label, major, minor);
              err_flag = true;
              continue;
            }

          /* set filesystem_id */

          p_entry->filesystem_id.major = (uint64_t) major;
          p_entry->filesystem_id.minor = (uint64_t) minor;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSUID))
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_NOSUID,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  if(on)
              p_perms->options |= EXPORT_OPTION_NOSUID;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_NOSGID))
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_NOSGID,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  if(on)
              p_perms->options |= EXPORT_OPTION_NOSGID;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_PRIVILEGED_PORT))
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_PRIVILEGED_PORT,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  if(on)
              p_perms->options |= EXPORT_OPTION_PRIVILEGED_PORT;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_UQUOTA ) )
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_PRIVILEGED_PORT,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  if(on)
              p_perms->options |= EXPORT_OPTION_USE_UQUOTA;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_SPECIFIC))
        {
          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_SPECIFIC) == FLAG_EXPORT_FS_SPECIFIC)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_FS_SPECIFIC);
              continue;
            }

          set_options |= FLAG_EXPORT_FS_SPECIFIC;

          if(strlen(var_value) > MAXPATHLEN)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = true;
              continue;
            }
	  p_entry->FS_specific = gsh_strdup(var_value);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_FS_TAG))
        {
          struct gsh_export *exp;

          /* check if it has not already been set */
          if((set_options & FLAG_EXPORT_FS_TAG) == FLAG_EXPORT_FS_TAG)
            {
              DEFINED_TWICE_WARNING(label, CONF_EXPORT_FS_TAG);
              continue;
            }

          set_options |= FLAG_EXPORT_FS_TAG;

          exp = get_gsh_export_by_tag(var_value);

          if(exp != NULL)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: Duplicate Tag: \"%s\"",
                      label, var_value);
              err_flag = true;
	      put_gsh_export(exp);
              continue;
            }

          if(strlen(var_value) > MAXPATHLEN)
            {
              LogCrit(COMPONENT_CONFIG,
                      "NFS READ %s: %s: \"%s\" too long",
                      label, var_name, var_value);
              err_flag = true;
              continue;
            }
	  p_entry->FS_tag = gsh_strdup(var_value);
        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_WRITE))
        {
          int64_t offset;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_PREF_READ,
			      label, var_name,
			      &err_flag,
			      &offset))
		  continue;
          p_entry->MaxOffsetWrite = (uint32_t) offset;
          p_perms->options |= EXPORT_OPTION_MAXOFFSETWRITE;

          set_options |= FLAG_EXPORT_MAX_OFF_WRITE;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_MAX_OFF_READ))
        {
          int64_t offset;

	  if( !parse_int64_t(var_value,
			      0, INTMAX_MAX,
			      &set_options, FLAG_EXPORT_PREF_READ,
			      label, var_name,
			      &err_flag,
			      &offset))
		  continue;
          p_entry->MaxOffsetRead = (uint32_t) offset;
          p_perms->options |= EXPORT_OPTION_MAXOFFSETREAD;

          set_options |= FLAG_EXPORT_MAX_OFF_READ;

        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COMMIT))
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_USE_COMMIT,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  p_entry->use_commit = on;
        }
      else if(!STRCMP(var_name, CONF_EXPORT_USE_COOKIE_VERIFIER))
        {
	  bool on;

	  if( !parse_bool(var_value,
			  &set_options, FLAG_EXPORT_USE_COOKIE_VERIFIER,
			  label, var_name,
			  &err_flag,
			  &on))
		  continue;
	  p_entry->UseCookieVerifier = on;
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
              err_flag = true;
              continue;
            }
	}
      else if(!STRCMP(var_name, CONF_EXPORT_FSAL))
        {
	  if(fsal_hdl != NULL)
	    {
	      LogCrit(COMPONENT_CONFIG,
		      "NFS READ %s: FSAL is already defined as (%s), new attempt = (%s)",
		      label, fsal_hdl->ops->get_name(p_entry->export_hdl->fsal),
		      var_value);
	      continue;
	    }
	  fsal_hdl = lookup_fsal(var_value);
	  if(fsal_hdl == NULL)
	    {
		    LogCrit(COMPONENT_CONFIG,
			    "NFS READ %s: FSAL %s is not loaded!", label, var_value);
		    err_flag = true;
		    continue;
	    }
	}
      else
        {
          LogWarn(COMPONENT_CONFIG,
                  "NFS READ %s: Unknown option: %s, ignored",
                  label, var_name);
        }

    }

  /** check for mandatory options */
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

      err_flag = true;
    }

  if(p_perms->options & EXPORT_OPTION_ACCESS_TYPE)
    {
      /* Copy the default permissions into the default client
       * list entries.  It is an error if the list is empty.
       */
      bool found = false;

      glist_for_each(glist, &p_access_list->client_list)
        {
          exportlist_client_entry_t * p_client_entry;

	  p_client_entry = glist_entry(glist, exportlist_client_entry_t, cle_list);
	  if(p_client_entry->client_perms.options & EXPORT_OPTION_ACCESS_LIST)
	    {
	      p_client_entry->client_perms.anonymous_uid = p_perms->anonymous_uid;
	      p_client_entry->client_perms.anonymous_gid = p_perms->anonymous_gid;
	      p_client_entry->client_perms.options |= p_perms->options;
	      if(isFullDebug(COMPONENT_CONFIG))
		 LogClientListEntry(COMPONENT_CONFIG, p_client_entry);
	      found = true;
	    }
	}
      if( !found)
        {
	  LogCrit(COMPONENT_CONFIG,
		  "NFS READ %s: %s specified but no %s found.",
		  label, CONF_EXPORT_ACCESSTYPE, CONF_EXPORT_ACCESS);
	  err_flag = true;
	}
    }
     
  if(((p_perms->options & EXPORT_OPTION_NFSV4) != 0) &&
     ((set_options & FLAG_EXPORT_PSEUDO) == 0))
    {
      /* If we export for NFS v4, Pseudo is required */
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Missing mandatory parameter %s",
	      label, CONF_EXPORT_PSEUDO);

      err_flag = true;
    }

  if(path_matches &&
     ((set_options & FLAG_EXPORT_PSEUDO) == 0) &&
     ((set_options & FLAG_EXPORT_FS_TAG) == 0))
    {
      /* Duplicate export must specify at least one of tag and pseudo */
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Duplicate %s must have at least %s or %s",
	      label, label, CONF_EXPORT_PSEUDO, CONF_EXPORT_FS_TAG);

      err_flag = true;
    }

/** @TODO at some point, have a global config def for the default FSAL when
 * an export doesn't supply it.  Right now, it is VFS for lack of a better
 * idea.
 */
  if( !err_flag && fsal_hdl == NULL)
    {
       LogMajor(COMPONENT_CONFIG,
		"NFS READ %s: No FSAL for this export defined. Fallback to using VFS",
		label);
       fsal_hdl = lookup_fsal("VFS"); /* should have a "Default_FSAL" param... */
    }
  if( !err_flag && fsal_hdl != NULL)
    {
       fsal_status_t expres = fsal_hdl->ops->create_export(fsal_hdl,
							   p_entry->fullpath,
							   p_entry->FS_specific,
							   p_entry,
							   NULL, 
							   &fsal_up_top,
							   &p_entry->export_hdl);
       if(FSAL_IS_ERROR(expres))
         {
	    LogCrit(COMPONENT_CONFIG,
		    "Could not create FSAL export for %s", p_entry->fullpath);
	    err_flag = true;
	 }
       else
         {
           if((p_entry->export_perms.options & EXPORT_OPTION_MAXREAD) != EXPORT_OPTION_MAXREAD) 
             {
	       if ( p_entry->export_hdl->ops->fs_maxread(p_entry->export_hdl) > 0)
                  p_entry->MaxRead = p_entry->export_hdl->ops->fs_maxread(p_entry->export_hdl);
               else
                  p_entry->MaxRead = LASTDEFAULT;
             }
          if((p_entry->export_perms.options & EXPORT_OPTION_MAXWRITE) != EXPORT_OPTION_MAXWRITE) 
             {
               if ( p_entry->export_hdl->ops->fs_maxwrite(p_entry->export_hdl) > 0)
                  p_entry->MaxWrite = p_entry->export_hdl->ops->fs_maxwrite(p_entry->export_hdl);
               else
                  p_entry->MaxWrite = LASTDEFAULT;
             }
          LogFullDebug(COMPONENT_INIT,
                      "Set MaxRead MaxWrite for Path=%s Options = 0x%x MaxRead = 0x%llX MaxWrite = 0x%llX",
                      p_entry->fullpath, p_entry->export_perms.options,
                      (long long) p_entry->MaxRead,
                      (long long) p_entry->MaxWrite);
	}

       fsal_hdl->ops->put(fsal_hdl);
    }
  else
    {
       LogCrit(COMPONENT_CONFIG,
	       "HELP! even VFS FSAL is not resident!");
    }

  /* Append the default Access list to the export so someone owns them */
  glist_add_list_tail(&p_entry->clients.client_list,
		      &p_access_list->client_list);
  p_entry->clients.num_clients += p_access_list->num_clients;

  /* check if there had any error.
   * if so, free the p_entry and return an error.
   */
  if(err_flag)
    {
      LogCrit(COMPONENT_CONFIG,
	      "NFS READ %s: Export %d (%s) had errors, ignoring entry",
	      label, p_entry->id, p_entry->fullpath);
      put_gsh_export(exp);
      remove_gsh_export(export_id);
      return -1;
    }
  set_gsh_export_state(exp, EXPORT_READY);

  LogEvent(COMPONENT_CONFIG,
           "NFS READ %s: Export %d (%s) successfully parsed",
           label, p_entry->id, p_entry->fullpath);

  if(isFullDebug(COMPONENT_CONFIG)) {
	  char perms[1024];

	  StrExportOptions(p_perms, perms);
	  LogFullDebug(COMPONENT_CONFIG,
		       "  Export Perms: %s", perms);
  }
  put_gsh_export(exp); /* all done, let go */
  return 0;

}

/**
 * @brief Free resources attached to an export
 *
 * @param export [IN] pointer to export
 *
 * @return true if all went well
 */

void free_export_resources(exportlist_t *export)
{
	fsal_status_t fsal_status;

	FreeClientList(&export->clients);
	if(export->exp_root_cache_inode)
		cache_inode_put(export->exp_root_cache_inode);
	export->exp_root_cache_inode = NULL;
	if(export->export_hdl != NULL) {
		if(export->export_hdl->ops->put(export->export_hdl) == 0) {
			fsal_status =
				export->export_hdl->ops->release(export->export_hdl);
			if(FSAL_IS_ERROR(fsal_status)) {
				LogCrit(COMPONENT_CONFIG,
					"Cannot release export object, quitting");
			}
		} else {
			LogCrit(COMPONENT_CONFIG,
				"Cannot put export object, quitting");
		}
	}
	export->export_hdl = NULL;
	/* free strings here */
	if(export->fullpath != NULL)
		gsh_free(export->fullpath);
	if(export->pseudopath != NULL)
		gsh_free(export->pseudopath);
	if(export->FS_specific != NULL)
		gsh_free(export->FS_specific);
	if(export->FS_tag != NULL)
		gsh_free(export->FS_tag);
}

/**
 * @brief builds an export entry for '/' with default parameters
 *
 * @note This is only referenced by MainNFSD/fuse_binding.c which is
 * not operational at this point.  When that happens, this will have
 * to be reworked into export manager.
 *
 * @return Root export.
 */

exportlist_t *BuildDefaultExport()
{
  exportlist_t *p_entry;
  bool rc;
  struct client_args args;

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
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV3) != 0)
    p_entry->export_perms.options |= EXPORT_OPTION_NFSV3;
  if((nfs_param.core_param.core_options & CORE_OPTION_NFSV4) != 0)
    p_entry->export_perms.options |= EXPORT_OPTION_NFSV4;
  p_entry->export_perms.options |= EXPORT_OPTION_TRANSPORTS;

  p_entry->filesystem_id.major = 101;
  p_entry->filesystem_id.minor = 101;

  p_entry->MaxWrite = 0x100000;
  p_entry->MaxRead = 0x100000;
  p_entry->PrefWrite = 0x100000;
  p_entry->PrefRead = 0x100000;
  p_entry->PrefReaddir = 0x100000;

  p_entry->FS_tag = gsh_strdup("ganesha");

  p_entry->id = 1;

  p_entry->fullpath = gsh_strdup("/");
  p_entry->pseudopath = gsh_strdup("/");

  p_entry->UseCookieVerifier = true;

  glist_init(&p_entry->clients.client_list);
  glist_init(&p_entry->exp_state_list);
  glist_init(&p_entry->exp_lock_list);

  /**
   * Grant root access to all clients
   */
  args.client = &p_entry->clients;
  args.option = EXPORT_OPTION_ROOT;
  args.var_name = CONF_EXPORT_ROOT;
  rc = add_export_client("*", &args);
  if( !rc)
    {
      LogCrit(COMPONENT_CONFIG,
              "NFS READ EXPORT: Invalid client \"*\"");
      gsh_free(p_entry);
      return NULL;
    }

  LogEvent(COMPONENT_CONFIG,
           "NFS READ_EXPORT: Export %d (%s) successfully parsed",
           p_entry->id, p_entry->fullpath);

  return p_entry;

}

/**
 * @brief Read the export entries from the parsed configuration file.
 *
 * @param[in]  in_config    The file that contains the export list
 *
 * @return A negative value on error,
 *         the number of export entries else.
 */
int ReadExports(config_file_t in_config)
{

  int nb_blk, rc, i;
  char *blk_name;
  int err_flag = false;
  int nb_entries = 0;

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

          rc = BuildExportEntry(block);

          /* If the entry is errorneous, ignore it
           * and continue checking syntax of other entries.
           */
          if(rc != 0)
            {
              err_flag = true;
              continue;
            }
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
 * @brief pkginit callback to initialize exports from nfs_init
 */

static bool init_export(struct gsh_export *cl, void *state)
{
	cache_inode_status_t status;
	cache_entry_t *entry;

	status = nfs_export_get_root_entry(&cl->export, &entry);
	if(status != CACHE_INODE_SUCCESS || entry == NULL)
		return false;
	else
		return true;
}

/**
 * @brief Initialize exports over a live cache inode and fsal layer
 */

void exports_pkginit(void)
{
	foreach_gsh_export(init_export, NULL);
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
  struct glist_head * glist;
  in_addr_t addr = get_in_addr(hostaddr);
  unsigned int i = 0;
  int rc;
  int ipvalid = -1; /* -1 need to print, 0 - invalid, 1 - ok */
  char hostname[MAXHOSTNAMELEN + 1];
  char ipstring[SOCK_NAME_MAX + 1];

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
                       "Test HOSTIF_CLIENT: Test entry %d: clientaddr %d.%d.%d.%d, "
		       "match with %d.%d.%d.%d",
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
              return true;
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
              return true;
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
          if(innetgr(p_client->client.netgroup.netgroupname, hostname,
		     NULL, NULL) == 1)
            {
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches netgroup for entry %u",
                           i);
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
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches wildcard for entry %u",
                           i);
              return true;
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
/**
 * @TODO this change from 1.5 is not IPv6 useful.
 * come back to this and use the string from client mgr inside req_ctx...
 */
                      LogInfo(COMPONENT_DISPATCH,
                              "Could not resolve hostame for addr %d.%d.%d.%d ... not checking if a hostname wildcard matches",
                              (int)(ntohl(addr) >> 24),
                              (int)(ntohl(addr) >> 16) & 0xFF,
                              (int)(ntohl(addr) >> 8) & 0xFF,
                              (int)(ntohl(addr) & 0xFF));
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
              *pclient_found = *p_client;
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches wildcard for entry %u",
                           i);
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

       case HOSTIF_CLIENT_V6:
          break;

       case MATCH_ANY_CLIENT:
          *pclient_found = *p_client;
          LogFullDebug(COMPONENT_DISPATCH,
                       "This matches any client wildcard for entry %u",
                       i);
          return true;

       case BAD_CLIENT:
          LogDebug(COMPONENT_DISPATCH,
                  "Bad client in position %u seen in export list", i );
	  continue ;

        default:
           LogCrit(COMPONENT_DISPATCH,
                   "Unsupported client in position %u in export list with type %u",
		   i, p_client->type);
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
          break;

        case HOSTIF_CLIENT_V6:
          if(!memcmp(p_client->client.hostif.clientaddr6.s6_addr, paddrv6->s6_addr, 16))  /* Remember that IPv6 address are 128 bits = 16 bytes long */
            {
              LogFullDebug(COMPONENT_DISPATCH,
                           "This matches host adress in IPv6");
              *pclient_found = *p_client;
              return true;
            }
          break;

        case MATCH_ANY_CLIENT:
           *pclient_found = *p_client;
           LogFullDebug(COMPONENT_DISPATCH,
                        "This matches any client wildcard for entry %p",
                        p_client);
          return true;

        default:
          return false;         /* Should never occurs */
          break;
        }
    }

  /* no export found for this option */
  return false;
}

int export_client_match_any(sockaddr_t                * hostaddr,
                            exportlist_client_t       * clients,
                            exportlist_client_entry_t * pclient_found,
                            unsigned int                export_option)
{
  if(hostaddr->ss_family == AF_INET6)
    {
      struct sockaddr_in6 *psockaddr_in6 = (struct sockaddr_in6 *) hostaddr;
      return export_client_matchv6(&(psockaddr_in6->sin6_addr),
                                   clients,
                                   pclient_found,
                                   export_option);
    }
  else
    {
      return export_client_match(hostaddr,
                                 clients,
                                 pclient_found,
                                 export_option);
    }
}

/**
 * @brief Checks if request security flavor is suffcient for the requested export
 *
 * @param[in] req     Related RPC request.
 * @param[in] pexport Related export entry
 *
 * @return true if the request flavor exists in the matching export
 * false otherwise
 */
bool nfs_export_check_security(struct svc_req *req,
			       export_perms_t * p_export_perms,
			       exportlist_t *pexport)
{
  switch (req->rq_cred.oa_flavor)
    {
      case AUTH_NONE:
        if((p_export_perms->options & EXPORT_OPTION_AUTH_NONE) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_NONE",
                    pexport->fullpath);
            return false;
          }
        break;

      case AUTH_UNIX:
        if((p_export_perms->options & EXPORT_OPTION_AUTH_UNIX) == 0)
          {
            LogInfo(COMPONENT_DISPATCH,
                    "Export %s does not support AUTH_UNIX",
                    pexport->fullpath);
            return false;
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
                  if((p_export_perms->options &
                      EXPORT_OPTION_RPCSEC_GSS_NONE) == 0)
                    {
                      LogInfo(COMPONENT_DISPATCH,
                              "Export %s does not support "
                              "RPCSEC_GSS_SVC_NONE",
                              pexport->fullpath);
                      return false;
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
                      return false;
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
                      return false;
                    }
                  break;

                  default:
                    LogInfo(COMPONENT_DISPATCH,
                            "Export %s does not support unknown "
                            "RPCSEC_GSS_SVC %d",
                            pexport->fullpath, (int) svc);
                    return false;
              }
          }
      break;
#endif
      default:
        LogInfo(COMPONENT_DISPATCH,
                "Export %s does not support unknown oa_flavor %d",
                pexport->fullpath, (int) req->rq_cred.oa_flavor);
        return false;
    }

  return true;
}

static char ten_bytes_all_0[10];

sockaddr_t * check_convert_ipv6_to_ipv4(sockaddr_t * ipv6, sockaddr_t *ipv4)
{
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
     (psockaddr_in6->sin6_addr.s6_addr[10] == 0xFF) &&
     (psockaddr_in6->sin6_addr.s6_addr[11] == 0xFF))
    {
      memset(ipv4, 0, sizeof(*ipv4));

      paddr->sin_port        = psockaddr_in6->sin6_port;
      paddr->sin_addr.s_addr = *(in_addr_t *)&psockaddr_in6->sin6_addr.s6_addr[12];
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
}

/**
 * @brief Checks if a machine is authorized to access an export entry
 *
 * @param[in]     hostaddr         The complete remote address (as a sockaddr_storage to be IPv6 compliant)
 * @param[in]     ptr_req          The related RPC request.
 * @param[in]     pexport          Related export entry (if found, NULL otherwise).
 * @param[in]     nfs_prog         Number for the NFS program.
 * @param[in]     mnt_prog         Number for the MOUNT program.
 * @param[out]    pclient_found    Client entry found in export list, NULL if nothing was found.
 * @param[in]     user_credentials User credentials
 * @param[in]     proc_makes_write Whether this operation counts as a write
 *
 * @retval EXPORT_PERMISSION_GRANTED on success
 * @retval EXPORT_PERMISSION_DENIED
 * @retval EXPORT_WRITE_ATTEMPT_WHEN_RO
 * @retval EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO
 */


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
 * @brief Lookup and associate a cache inode entry with the root of the export
 *
 * Fetch the cache entry of the export's root.  If not there yet, look it up
 * which takes references.  This is fine for attaching it to an export but
 * other uses (psesudofs) should take their own references while holding it.
 *
 * @param export [IN] the aforementioned export
 * @param entryp [IN/OUT] call by ref pointer to store cache entry
 *
 * @return status cache inode status code
 */

cache_inode_status_t nfs_export_get_root_entry(exportlist_t *export,
					       cache_entry_t **entryp)
{
      fsal_status_t fsal_status;
      cache_inode_status_t cache_status;
      struct fsal_obj_handle *root_handle;
      cache_entry_t *entry = NULL;

      if(export->exp_root_cache_inode != NULL) {
	      *entryp = export->exp_root_cache_inode;
	      return CACHE_INODE_SUCCESS;
      }
      /* Lookup for the FSAL Path */
      fsal_status = export->export_hdl->ops->lookup_path(export->export_hdl,
							 NULL,
							 export->fullpath,
							 &root_handle);
      if(FSAL_IS_ERROR(fsal_status)) {
              LogCrit(COMPONENT_INIT,
                      "Lookup failed on path, ExportId=%u Path=%s FSAL_ERROR=(%s,%u)",
                      export->id, export->fullpath, msg_fsal_err(fsal_status.major),
                      fsal_status.minor);
              return cache_inode_error_convert(fsal_status);
      }
      /* Add this entry to the Cache Inode as a "root" entry */

      cache_status = cache_inode_new_entry(root_handle,
					   CACHE_INODE_FLAG_NONE,
					   &entry);
      if (entry != NULL) {
	      /* cache_inode_get returns a cache_entry with
	       * reference count of 2, where 1 is the sentinel value of
	       * a cache entry in the hash table.  The export list in
	       * this case owns the extra reference.  In the future
	       * if functionality is added to dynamically add and remove
	       * export entries, then the function to remove an export
	       * entry MUST put the extra reference.
	       */
              export->exp_root_cache_inode = entry;
              LogInfo(COMPONENT_INIT,
		      "Added root entry for path %s on export_id=%d",
		      export->fullpath, export->id);
	      *entryp = export->exp_root_cache_inode;
      } else {
              LogCrit(COMPONENT_INIT,
                      "Error when creating root cached entry for %s, export_id=%d, cache_status=%d",
                      export->fullpath, export->id, cache_status);
	      *entryp = NULL;
      }
      return cache_status;
}

