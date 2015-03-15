/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SOL_SYS_TYPES_H
#define _SOL_SYS_TYPES_H

#include <sys/isa_defs.h>
#include <sys/feature_tests.h>
#include_next <sys/types.h>
#include <inttypes.h>
#include <sys/param.h> /* for NBBY */
#include <sys/types32.h>
#include <bits/types.h>

#ifndef __APPLE__
typedef enum boolean { B_FALSE, B_TRUE } boolean_t;
#endif

typedef unsigned char uchar_t;
typedef unsigned short ushort_t;
typedef unsigned int uint_t;
typedef unsigned long ulong_t;

typedef long long longlong_t;
typedef unsigned long long u_longlong_t;

typedef longlong_t offset_t;
typedef u_longlong_t u_offset_t;
typedef u_longlong_t len_t;
typedef longlong_t diskaddr_t;

typedef short pri_t;

typedef int zoneid_t;
typedef int projid_t;

typedef int major_t;
typedef int minor_t;

typedef ushort_t o_mode_t; /* old file attribute type */

typedef char *caddr_t;
typedef int daddr_t;

/*
 * Definitions remaining from previous partial support for 64-bit file
 * offsets.  This partial support for devices greater than 2gb requires
 * compiler support for long long.
 */
#ifdef _LONG_LONG_LTOH
typedef union {
	offset_t _f;    /* Full 64 bit offset value */
	struct {
		int32_t _l; /* lower 32 bits of offset value */
		int32_t _u; /* upper 32 bits of offset value */
	} _p;
} lloff_t;
#endif

#ifdef _LONG_LONG_HTOL
typedef union {
	offset_t _f;    /* Full 64 bit offset value */
	struct {
		int32_t _u; /* upper 32 bits of offset value */
		int32_t _l; /* lower 32 bits of offset value */
	} _p;
} lloff_t;
#endif

//<HACKED>

#ifndef MAXPATHLEN
#define MAXPATHLEN      4096
#endif

#ifndef MAXPATH
#define MAXPATH         4096
#endif

#ifndef __gid_t_defined
typedef __gid_t gid_t;
# define __gid_t_defined
#endif

#ifndef __mode_t_defined
typedef __mode_t mode_t;
# define __mode_t_defined
#endif

/* But these were defined by ISO C without the first `_'.  */
typedef	unsigned char u_int8_t;
typedef	unsigned short int u_int16_t;
typedef	unsigned int u_int32_t;
# if __WORDSIZE == 64
typedef unsigned long int u_int64_t;
# elif __GLIBC_HAVE_LONG_LONG
__extension__ typedef unsigned long long int u_int64_t;
# endif

#ifndef __ssize_t_defined
typedef __ssize_t ssize_t;
# define __ssize_t_defined
#endif

#ifdef	__USE_BSD
# ifndef __daddr_t_defined
typedef __daddr_t daddr_t;
typedef __caddr_t caddr_t;
#  define __daddr_t_defined
# endif
#endif

#if defined __USE_UNIX98 && !defined __blksize_t_defined
typedef __blksize_t blksize_t;
# define __blksize_t_defined
#endif

/* Types from the Large File Support interface.  */
#ifndef __USE_FILE_OFFSET64
# ifndef __blkcnt_t_defined
typedef __blkcnt_t blkcnt_t;	 /* Type to count number of disk blocks.  */
#  define __blkcnt_t_defined
# endif
# ifndef __fsblkcnt_t_defined
typedef __fsblkcnt_t fsblkcnt_t; /* Type to count file system blocks.  */
#  define __fsblkcnt_t_defined
# endif
# ifndef __fsfilcnt_t_defined
typedef __fsfilcnt_t fsfilcnt_t; /* Type to count file system inodes.  */
#  define __fsfilcnt_t_defined
# endif
#else
# ifndef __blkcnt_t_defined
typedef __blkcnt64_t blkcnt_t;	   /* Type to count number of disk blocks.  */
#  define __blkcnt_t_defined
# endif
# ifndef __fsblkcnt_t_defined
typedef __fsblkcnt64_t fsblkcnt_t; /* Type to count file system blocks.  */
#  define __fsblkcnt_t_defined
# endif
# ifndef __fsfilcnt_t_defined
typedef __fsfilcnt64_t fsfilcnt_t; /* Type to count file system inodes.  */
#  define __fsfilcnt_t_defined
# endif
#endif

#ifndef __ino_t_defined
# ifndef __USE_FILE_OFFSET64
typedef __ino_t ino_t;
# else
typedef __ino64_t ino_t;
# endif
# define __ino_t_defined
#endif
#if defined __USE_LARGEFILE64 && !defined __ino64_t_defined
typedef __ino64_t ino64_t;
# define __ino64_t_defined
#endif

#ifndef __off_t_defined
# ifndef __USE_FILE_OFFSET64
typedef __off_t off_t;
# else
typedef __off64_t off_t;
# endif
# define __off_t_defined
#endif
#if defined __USE_LARGEFILE64 && !defined __off64_t_defined
typedef __off64_t off64_t;
# define __off64_t_defined
#endif

#ifdef __USE_LARGEFILE64
typedef __blkcnt64_t blkcnt64_t;     /* Type to count number of disk blocks. */
typedef __fsblkcnt64_t fsblkcnt64_t; /* Type to count file system blocks.  */
typedef __fsfilcnt64_t fsfilcnt64_t; /* Type to count file system inodes.  */
#endif

#ifndef __uid_t_defined
typedef __uid_t uid_t;
# define __uid_t_defined
#endif

//</HACKED>
#include <sys/time.h>

#endif
