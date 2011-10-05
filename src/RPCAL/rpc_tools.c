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

#include "rpcal.h"
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

pthread_mutex_t *mutex_cond_xprt;
pthread_cond_t *condvar_xprt;
SVCXPRT **Xports;
fd_set Svc_fdset;

const char *str_sock_type(int st)
{
  static char buf[16];
  switch (st)
    {
      case SOCK_STREAM: return "SOCK_STREAM";
      case SOCK_DGRAM:  return "SOCK_DGRAM ";
      case SOCK_RAW:    return "SOCK_RAW   ";
    }
  sprintf(buf, "%d", st);
  return buf;
}

const char *str_ip_proto(int p)
{
  static char buf[16];
  switch (p)
    {
      case IPPROTO_IP:  return "IPPROTO_IP ";
      case IPPROTO_TCP: return "IPPROTO_TCP";
      case IPPROTO_UDP: return "IPPROTO_UDP";
    }
  sprintf(buf, "%d", p);
  return buf;
}

const char *str_af(int af)
{
  static char buf[16];
  switch (af)
    {
      case AF_INET:  return "AF_INET ";
      case AF_INET6: return "AF_INET6";
    }
  sprintf(buf, "%d", af);
  return buf;
}

const char *xprt_type_to_str(xprt_type_t type)
{
  switch(type)
    {
      case XPRT_UNKNOWN:    return "UNKNOWN";
      case XPRT_UDP:        return "udp";
      case XPRT_TCP:        return "tcp";
      case XPRT_RENDEZVOUS: return "rendezvous";
    }
  return "INVALID";
}

/**
 *
 * copy_xprt_addr: copies and transport address into an address field.
 *
 * copies and transport address into an address field.
 *
 * @param addr [OUT] address field to fill in.
 * @param xprt [IN]  transport to get address from.
 *
 * @return 1 if ok, 0 if failure.
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

  memcpy(addr, phostaddr, sizeof(sockaddr_t));
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
unsigned long hash_sockaddr(sockaddr_t *addr, ignore_port_t ignore_port)
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
          if(ignore_port == CHECK_PORT)
            {
              port = paddr->sin_port;
              addr_hash ^= (port<<16);
            }
          break;
        }
      case AF_INET6:
        {
          struct sockaddr_in6 *paddr = (struct sockaddr_in6 *)addr;

          addr_hash = paddr->sin6_addr.s6_addr32[0] ^
                      paddr->sin6_addr.s6_addr32[1] ^
                      paddr->sin6_addr.s6_addr32[2] ^
                      paddr->sin6_addr.s6_addr32[3];
          if(ignore_port == CHECK_PORT)
            {
              port = paddr->sin6_port;
              addr_hash ^= (port<<16);
            }
          break;
        }
      default:
        break;
    }
#else
  addr_hash = addr->sin_addr.s_addr;
  if(ignore_port == CHECK_PORT)
    {
      port = addr->sin_port;
      addr_hash ^= (port<<16);
    }
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
        break;
      default:
        port = -1;
    }
#else
  name = inet_ntop(addr->sin_family, &addr->sin_addr, buf, len);
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

  memset(buf, 0, len);

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
  name = inet_ntop(addr->sin_family, &addr->sin_addr, buf, len);
#endif

  if(name == NULL)
    {
      strncpy(buf, "<unknown>", len);
      return 0;
    }
  return 1;
}

/**
 *
 * cmp_sockaddr: compare 2 sockaddrs, including ports
 *
 * @param addr_1 [IN] first address
 * @param addr_2 [IN] second address
 * @param ignore_port [IN] 1 if you want to ignore port
 *       comparison, 0 if you need port comparisons
 *
 * @return 1 if addresses match, 0 if they don't
 *
 */
int cmp_sockaddr(sockaddr_t *addr_1,
                 sockaddr_t *addr_2,
                 ignore_port_t ignore_port)
{
#ifdef _USE_TIRPC
  if(addr_1->ss_family != addr_2->ss_family)
    return 0;
#else
  if(addr_1->sin_family != addr_2->sin_family)
    return 0;
#endif

#ifdef _USE_TIRPC
  switch (addr_1->ss_family)
#else
  switch (addr_1->sin_family)
#endif
    {
      case AF_INET:
        {
          struct sockaddr_in *paddr1 = (struct sockaddr_in *)addr_1;
          struct sockaddr_in *paddr2 = (struct sockaddr_in *)addr_2;

          return (paddr1->sin_addr.s_addr == paddr2->sin_addr.s_addr
                  && (ignore_port == IGNORE_PORT || paddr1->sin_port == paddr2->sin_port));
        }
#ifdef _USE_TIRPC
      case AF_INET6:
        {
          struct sockaddr_in6 *paddr1 = (struct sockaddr_in6 *)addr_1;
          struct sockaddr_in6 *paddr2 = (struct sockaddr_in6 *)addr_2;

          return (memcmp(
                         paddr1->sin6_addr.s6_addr,
                         paddr2->sin6_addr.s6_addr,
                         sizeof(paddr2->sin6_addr.s6_addr)) == 0)
                  && (ignore_port == IGNORE_PORT || paddr1->sin6_port == paddr2->sin6_port);
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

void socket_setoptions(int socketFd)
{
  unsigned int SbMax = (1 << 30);       /* 1GB */

  while(SbMax > 1048576)
    {
      if((setsockopt(socketFd, SOL_SOCKET, SO_SNDBUF, (char *)&SbMax, sizeof(SbMax)) < 0)
         || (setsockopt(socketFd, SOL_SOCKET, SO_RCVBUF, (char *)&SbMax, sizeof(SbMax)) <
             0))
        {
          SbMax >>= 1;          /* SbMax = SbMax/2 */
          continue;
        }

      break;
    }

  return;
}                               /* socket_setoptions_ctrl */

#ifdef _USE_TIRPC
#define SIZE_AI_ADDR sizeof(struct sockaddr)
#else
#define SIZE_AI_ADDR sizeof(struct sockaddr_in)
#endif

int ipstring_to_sockaddr(const char *str, sockaddr_t *addr)
{
  struct addrinfo *info, hints, *p;
  int rc;
  char ipname[SOCK_NAME_MAX];

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICHOST;
#ifdef _USE_TIRPC
  hints.ai_family = AF_UNSPEC;
#else
  hints.ai_family = AF_INET;
#endif
  hints.ai_socktype = SOCK_RAW;
  hints.ai_protocol = 0;
  rc = getaddrinfo(str, NULL, &hints, &info);
  if (rc == 0 && info != NULL)
    {
      p = info;
      if(isFullDebug(COMPONENT_RPC))
        {
          while (p != NULL)
            {
              sprint_sockaddr((sockaddr_t *)p->ai_addr, ipname, sizeof(ipname));
              LogFullDebug(COMPONENT_RPC,
                           "getaddrinfo %s returned %s family=%s socktype=%s protocol=%s",
                           str, ipname,
                           str_af(p->ai_family),
                           str_sock_type(p->ai_socktype),
                           str_ip_proto(p->ai_protocol));
              p = p->ai_next;
            }
        }
      memcpy(addr, info->ai_addr, SIZE_AI_ADDR);
      freeaddrinfo(info);
    }
  else
    {
      switch (rc)
        {
          case EAI_SYSTEM:
            LogFullDebug(COMPONENT_RPC,
                         "getaddrinfo %s returned %d(%s)",
                         str, errno, strerror(errno));
          default:
            LogFullDebug(COMPONENT_RPC,
                         "getaddrinfo %s returned %d(%s)",
                         str, rc, gai_strerror(rc));
        }
    }
  return rc;
}

pthread_mutex_t clnt_create_mutex = PTHREAD_MUTEX_INITIALIZER;

CLIENT *Clnt_create(char *host,
                    unsigned long prog,
                    unsigned long vers,
                    char *proto)
{
  CLIENT *clnt;
  pthread_mutex_lock(&clnt_create_mutex);
  clnt = clnt_create(host, prog, vers, proto);
  if(clnt == NULL)
    {
      const char *err = clnt_spcreateerror("clnt_create failed");
      LogDebug(COMPONENT_RPC, "%s", err);
    }
  pthread_mutex_unlock(&clnt_create_mutex);
  return clnt;
}

void Clnt_destroy(CLIENT *clnt)
{
  pthread_mutex_lock(&clnt_create_mutex);
  clnt_destroy(clnt);
  pthread_mutex_unlock(&clnt_create_mutex);
}

void InitRPC(int num_sock)
{
  /* Allocate resources that are based on the maximum number of open file descriptors */
  Xports = (SVCXPRT **) Mem_Alloc_Label(num_sock * sizeof(SVCXPRT *), "Xports array");
  if(Xports == NULL)
    LogFatal(COMPONENT_RPC,
             "Xports array allocation failed");

  memset(Xports, 0, num_sock * sizeof(SVCXPRT *));
  mutex_cond_xprt = (pthread_mutex_t *) Mem_Alloc_Label(num_sock * sizeof(pthread_mutex_t ), "mutex_cond_xprt array");
  memset(mutex_cond_xprt, 0, num_sock * sizeof(pthread_mutex_t ));
  condvar_xprt = (pthread_cond_t *) Mem_Alloc_Label(num_sock * sizeof(pthread_cond_t ), "condvar_xprt array");
  memset(condvar_xprt, 0, num_sock * sizeof(pthread_cond_t ));

  FD_ZERO(&Svc_fdset);

#ifdef _USE_TIRPC
  /* RW_lock need to be initialized */
  rw_lock_init(&Svc_fd_lock);
#endif
}
