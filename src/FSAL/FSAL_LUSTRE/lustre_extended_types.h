/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:noexpandtab:shiftwidth=4:tabstop=4:
 */
/*
 * Copyright (C) 2009 CEA/DAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the CeCILL License.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license (http://www.cecill.info) and that you
 * accept its terms.
 */

/**
 * \file   lustre_extended_types.h
 * \brief  Specific types for handling lustre data.
 */
#ifndef _LUSTRE_EXTENDED_TYPES_H
#define _LUSTRE_EXTENDED_TYPES_H

#ifndef LPX64
#define LPX64 "%#llx"
#endif

#ifndef LPX64i
#define LPX64i "%llx"
#endif

#ifndef LPU64
#define LPU64 "%llu"
#endif

#include <sys/types.h>
#include <asm/types.h>

#include <assert.h>
#define LASSERT assert

#include "config.h"

#ifdef HAVE_INCLUDE_LUSTREAPI_H
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#else
#ifdef HAVE_INCLUDE_LIBLUSTREAPI_H
#include <lustre/liblustreapi.h>
#include <lustre/lustre_user.h>
#include <linux/quota.h>
#endif
#endif

#ifndef LOV_MAX_STRIPE_COUNT
/* in old lustre versions, this is not defined in lustre_user.h */
#define LOV_MAX_STRIPE_COUNT 160
#endif

#ifndef DFID_NOBRACE
#define DFID_NOBRACE LPX64":0x%x:0x%x"
#endif

#ifndef XATTR_NAME_LOV
#define XATTR_NAME_LOV "trusted.lov"
#endif

extern int llapi_get_poollist(const char *name, char **poollist,
							 int list_size,
							char *buffer,
							 int buffer_size);
extern int llapi_get_poolmembers(const char *poolname,
						 char **members,
						int list_size,
						char *buffer,
						int buffer_size);

#ifdef HAVE_CHANGELOG_EXTEND_REC
    #define CL_REC_TYPE struct changelog_ext_rec
#else
    #define CL_REC_TYPE struct changelog_rec
#endif

/* The following stuff is to decode link EA from userspace */

#include <byteswap.h>
#include <assert.h>

/* undefined types in lustre_idl */
#define be32_to_cpu(x) (bswap_32(x))
#define be64_to_cpu(x) ((__u64)bswap_64(x))
#define CLASSERT assert
#define LASSERTF(a, b, c) assert(a)
typedef void *lnet_nid_t;
typedef time_t cfs_time_t;

/* Workaround for lustre 2.6.0 (waiting for LU-3613 to land):
 * definitions and structures to handle link EA.
 */
#ifndef XATTR_NAME_LINK
#define XATTR_NAME_LINK "trusted.link"
#endif

struct link_ea_header {
	__u32 leh_magic;
	__u32 leh_reccount;
	__u64 leh_len;      /* total size */
	/* future use */
	__u32 padding1;
	__u32 padding2;
};

/* Hardlink data is name and parent fid.
 * Stored in this crazy struct for maximum packing and endian-neutrality
 */
struct link_ea_entry {
	/* __u16 stored big-endian, unaligned */
	unsigned char      lee_reclen[2];
	unsigned char      lee_parent_fid[sizeof(struct lu_fid)];
	char               lee_name[0];
} __attribute__((packed));

static inline void fid_be_to_cpu(struct lu_fid *dst, const struct lu_fid *src)
{
	dst->f_seq = be64_to_cpu(src->f_seq);
	dst->f_oid = be32_to_cpu(src->f_oid);
	dst->f_ver = be32_to_cpu(src->f_ver);
}

enum fid_seq {
	FID_SEQ_OST_MDT0    = 0,
	FID_SEQ_LLOG        = 1, /* unnamed llogs */
	FID_SEQ_ECHO        = 2,
	FID_SEQ_OST_MDT1    = 3,
	FID_SEQ_OST_MAX     = 9, /* Max MDT count before OST_on_FID */
	FID_SEQ_LLOG_NAME   = 10, /* named llogs */
	FID_SEQ_RSVD        = 11,
	FID_SEQ_IGIF        = 12,
	FID_SEQ_IGIF_MAX    = 0x0ffffffffULL,
	FID_SEQ_IDIF        = 0x100000000ULL,
	FID_SEQ_IDIF_MAX    = 0x1ffffffffULL,
	FID_SEQ_START       = 0x200000000ULL,
	FID_SEQ_LOCAL_FILE  = 0x200000001ULL,
	FID_SEQ_DOT_LUSTRE  = 0x200000002ULL,
	FID_SEQ_LOCAL_NAME  = 0x200000003ULL,
	FID_SEQ_SPECIAL     = 0x200000004ULL,
	FID_SEQ_QUOTA       = 0x200000005ULL,
	FID_SEQ_QUOTA_GLB   = 0x200000006ULL,
	FID_SEQ_ROOT        = 0x200000007ULL,  /* Located on MDT0 */
	FID_SEQ_NORMAL      = 0x200000400ULL,
	FID_SEQ_LOV_DEFAULT = 0xffffffffffffffffULL
};

static inline int fid_seq_is_rsvd(const __u64 seq)
{
	return (seq > FID_SEQ_OST_MDT0 && seq <= FID_SEQ_RSVD);
};

static inline int fid_seq_is_idif(const __u64 seq)
{
	return seq >= FID_SEQ_IDIF && seq <= FID_SEQ_IDIF_MAX;
}

static inline int fid_is_idif(const struct lu_fid *fid)
{
	return fid_seq_is_idif(fid->f_seq);
}

static inline int fid_seq_is_igif(const __u64 seq)
{
	return seq >= FID_SEQ_IGIF && seq <= FID_SEQ_IGIF_MAX;
}

static inline int fid_is_igif(const struct lu_fid *fid)
{
	return fid_seq_is_igif(fid->f_seq);
}

static inline int fid_is_sane(const struct lu_fid *fid)
{
	return fid != NULL &&
		((fid->f_seq >= FID_SEQ_START && fid->f_ver == 0) ||
		fid_is_igif(fid) || fid_is_idif(fid) ||
		fid_seq_is_rsvd(fid->f_seq));
}

struct lu_buf {
	void   *lb_buf;
	ssize_t lb_len;
};

struct linkea_data {
	/*
	 * Buffer to keep link EA body.
	 */
	struct lu_buf           *ld_buf;
	/*
	 * The matched header, entry and its lenght in the EA
	 */
	struct link_ea_header   *ld_leh;
	struct link_ea_entry    *ld_lee;
	int                     ld_reclen;
};

#define LINKEA_NEXT_ENTRY(ldata)        \
	(struct link_ea_entry *)((char *)ldata.ld_lee + ldata.ld_reclen)

#define LINKEA_FIRST_ENTRY(ldata)       \
	(struct link_ea_entry *)(ldata.ld_leh + 1)

#endif
