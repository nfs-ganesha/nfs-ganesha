/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * \file    rpc_tools.c
 * \author  $Author: ffilz $
 * \date    $Date: 2006/01/20 07:39:22 $
 * \version $Revision: 1.14 $
 * \brief   Some tools very usefull in the nfs protocol implementation.
 *
 * rpc_tools.c : Some tools very usefull in the nfs protocol implementation
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
#include <gssrpc/rpc.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif

#include "LRU_List.h"
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "nfs_core.h"
#include "nfs23.h"
#include "nfs4.h"
#include "fsal.h"
#include "stuff_alloc.h"
#include "nfs_tools.h"
#include "nfs_exports.h"
#include "nfs_file_handle.h"
#include "nfs_dupreq.h"

/**
 *
 * copy_xprt_addr: copies and transport address into an address field.
 *
 * copies and transport address into an address field.
 *
 * @param addr [OUT] address field to fill in.
 * @param xprt [IN]  transport to get address from.
 *
 * @return 0 if ok, other values mean an error.
 *
 */
int copy_xprt_addr(sockaddr_t *addr, SVCXPRT *xprt)
#ifdef _USE_TIRPC
{
  struct netbuf *phostaddr = svc_getcaller_netbuf(xprt);
  if(phostaddr->len > sizeof(sockaddr_t) || phostaddr->buf == NULL)
    return 0;
  memcpy(addr, phostaddr->buf, phostaddr->len);
  return 1;
}
#else
{
  struct sockaddr_in *phostaddr = svc_getcaller(xprt);

  memcpy(addr, phostaddr, sizeof(sockaddr_t);
  return 1;
}
#endif

 
/**
 *
 * hash_sockaddr: create a hash value based on the sockaddr_t structure
 *
 * This creates a native pointer size (unsigned long int) hash value
 * from the sockaddr_t structure. It supports both IPv4 and IPv6,
 * other types can be added in time.
 *
 * @param addr [IN] sockaddr_t address to hash
 * @param xprt [IN]  transport to get address from.
 *
 * @return hash value
 *
 */
unsigned long hash_sockaddr(sockaddr_t *addr)
{
  unsigned long addr_hash = 0;
  int port;
#ifdef _USE_TIRPC
  switch(addr->ss_family)
    {
      case AF_INET:
        {
          struct sockaddr_in *paddr = (struct sockaddr_in *)addr;
          addr_hash = paddr->sin_addr.s_addr;
          port = paddr->sin_port;
          addr_hash ^= (port<<16);
          break;
        }
      case AF_INET6:
        {
          struct sockaddr_in6 *paddr = (struct sockaddr_in6 *)addr;

          addr_hash = paddr->sin6_addr.s6_addr32[0] ^
                      paddr->sin6_addr.s6_addr32[1] ^
                      paddr->sin6_addr.s6_addr32[2] ^
                      paddr->sin6_addr.s6_addr32[3];
          port = paddr->sin6_port;
          addr_hash ^= (port<<16);
          break;
        }
      default:
        break;
    }
#else
  addr_hash = addr->sin_addr.s_addr;
  port = addr->sin_port;
  addr_hash ^= (port<<16);
#endif
  
  return addr_hash;
}

int sprint_sockaddr(sockaddr_t *addr, char *buf, int len)
{
  const char *name = NULL;
  int port, alen;

  buf[0] = '\0';

#ifdef _USE_TIRPC
  switch(addr->ss_family)
    {
      case AF_INET:
        name = inet_ntop(addr->ss_family, &(((struct sockaddr_in *)addr)->sin_addr), buf, len);
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        break;
      case AF_INET6:
        name = inet_ntop(addr->ss_family, &(((struct sockaddr_in6 *)addr)->sin6_addr), buf, len);
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        break;
      case AF_LOCAL:
        strncpy(buf, ((struct sockaddr_un *)addr)->sun_path, len);
        name = buf;
        port = -1;
      default:
        port = -1;
    }
#else
  name = inet_ntop(addr->ss_family, &addr->sin_addr), buf, len);
  port = ntohs(addr->sin_port);
#endif

  alen = strlen(buf);

  if(name == NULL)
    {
      strncpy(buf, "<unknown>", len);
      return 0;
    }

  if(port >= 0 && alen < len)
    snprintf(buf + alen, len - alen, ":%d", port);
  return 1;
}

int sprint_sockip(sockaddr_t *addr, char *buf, int len)
{
  const char *name = NULL;

  buf[0] = '\0';

#ifdef _USE_TIRPC
  switch(addr->ss_family)
    {
      case AF_INET:
        name = inet_ntop(addr->ss_family, &(((struct sockaddr_in *)addr)->sin_addr), buf, len);
        break;
      case AF_INET6:
        name = inet_ntop(addr->ss_family, &(((struct sockaddr_in6 *)addr)->sin6_addr), buf, len);
        break;
      case AF_LOCAL:
        strncpy(buf, ((struct sockaddr_un *)addr)->sun_path, len);
        name = buf;
    }
#else
  name = inet_ntop(addr->ss_family, &addr->sin_addr), buf, len);
#endif

  if(name == NULL)
    {
      strncpy(buf, "<unknown>", len);
      return 0;
    }
  return 1;
}

int cmp_sockaddr(sockaddr_t *addr_1,
                 sockaddr_t *addr_2,
                 int ignore_port)
{
#ifdef _USE_TIRPC
  if(addr_1->ss_family != addr_2->ss_family)
    return 0;
#else
  if(addr_1->sa_family != addr_2->sa_family)
    return 0;
#endif  

#ifdef _USE_TIRPC
  switch (addr_1->ss_family)
#else
  switch (addr_1->sa_family)
#endif
    {
      case AF_INET:
        {
          struct sockaddr_in *paddr1 = (struct sockaddr_in *)addr_1;
          struct sockaddr_in *paddr2 = (struct sockaddr_in *)addr_2;

          return (paddr1->sin_addr.s_addr == paddr2->sin_addr.s_addr
                  && (ignore_port || paddr1->sin_port == paddr2->sin_port));
        }
#ifdef _USE_TIRPC
      case AF_INET6:
        {
          struct sockaddr_in6 *paddr1 = (struct sockaddr_in6 *)addr_1;
          struct sockaddr_in6 *paddr2 = (struct sockaddr_in6 *)addr_2;

          return (paddr1->sin6_addr.s6_addr == paddr2->sin6_addr.s6_addr
                  && (ignore_port || paddr1->sin6_port == paddr2->sin6_port));
        }
#endif
      default:
        return 0;
    }
}

in_addr_t get_in_addr(sockaddr_t *addr)
{
#ifdef _USE_TIRPC
  if(addr->ss_family == AF_INET)
    return ((struct sockaddr_in *)addr)->sin_addr.s_addr;
  else
    return 0;
#else
  return addr->sin_addr.s_addr;
#endif
}

int get_port(sockaddr_t *addr)
{
#ifdef _USE_TIRPC
  switch(addr->ss_family)
    {
      case AF_INET:
        return ntohs(((struct sockaddr_in *)addr)->sin_port);
      case AF_INET6:
        return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
      default:
        return -1;
    }
#else
  return ntohs(addr->sin_port);
#endif
}
