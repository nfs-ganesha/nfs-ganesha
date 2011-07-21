/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */

/**
 *
 * \file    solaris_port.h
 * \brief   A file used to add correct include when compiling on Solaris
 *
 */

#ifndef _SOLARIS_PORT_H
#define _SOLARIS_PORT_H

/* Do not use deprecated AUTH_DES */
#define  _RPC_AUTH_DES_H

/* Do not use RPCBIND by default */
#define  _RPCB_PROT_H_RPCGEN

#include <sys/select.h>
#define __FDS_BITS(set) ((set)->fds_bits)

/* #define       svc_getcaller(x) (&(x)->xp_rtaddr.buf) */
#define       svc_getcaller(x) (&(x)->xp_raddr)

#ifdef _USE_FUSE
#include <sys/time_impl.h>
#define INT_MAX 2147483647
#define S_BLKSIZE      512      /* Block size for `st_blocks'.  */

#endif                          /* _USE_FUSE */

#ifdef _USE_POSIX
#include <sys/time_impl.h>
#define S_BLKSIZE      512      /* Block size for `st_blocks'.  */
#define NAME_MAX  255
#define HOST_NAME_MAX 64
#define LOGIN_NAME_MAX 256
#endif

#ifdef _USE_SNMP
#define HOST_NAME_MAX 64
typedef unsigned long u_long;
typedef unsigned char u_char;
typedef unsigned short u_short;
#endif

#endif
