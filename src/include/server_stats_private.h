
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2013
 * Author: Jim Lieb jlieb@panasas.com
 *
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/**
 * @defgroup Server statistics management
 * @{
 */

/**
 * @file server_stats.h
 * @author Jim Lieb <jlieb@panasas.com>
 * @brief Server statistics private interfaces
 */

#ifndef SERVER_STATS_PRIVATE_H
#define SERVER_STATS_PRIVATE_H

#include "sal_data.h"

/**
 * @brief Server request statistics
 *
 * These are the stats we keep
 */

/* Forward references to build pointers to private defs.
 */

struct nfsv3_stats;
struct mnt_stats;
struct nlmv4_stats;
struct rquota_stats;
struct nfsv40_stats;
struct nfsv41_stats;
struct nfsv42_stats;
struct deleg_stats;
struct _9p_stats;

struct gsh_stats {
	struct nfsv3_stats *nfsv3;
	struct mnt_stats *mnt;
	struct nlmv4_stats *nlm4;
	struct rquota_stats *rquota;
	struct nfsv40_stats *nfsv40;
	struct nfsv41_stats *nfsv41;
	struct nfsv41_stats *nfsv42;
	struct deleg_stats *deleg;
	struct _9p_stats *_9p;
};

/**
 * @brief Server by client IP statistics
 *
 * toplevel structure for statistics gathering.  This is
 * only shared between client_mgr.c and server_stats.c
 * client_mgr.c needs to know about it to properly size
 * the allocation.
 *
 * NOTE: see below.  The client member must be the
 *       last because the gsh_client structure has
 *       a variable length array at the end (for the
 *       key).
 */

struct server_stats {
	struct gsh_stats st;
	struct gsh_client client;	/* must be last element! */
};

/**
 * @brief Server by export id statistics
 *
 * Top level structure only shared between export_mgr.c and
 * server_stats.c
 */

struct export_stats {
	struct gsh_stats st;
	struct gsh_export export;
};

#ifdef USE_DBUS

/* Bits for introspect arg structures
 */

#define EXPORT_ID_ARG    \
{                        \
	.name = "exp_id",\
	.type = "q",     \
	.direction = "in"\
}

#define TIMESTAMP_REPLY    \
{                          \
	.name = "time",    \
	.type = "(tt)",    \
	.direction = "out" \
}

#define IOSTATS_REPLY      \
{                          \
	.name = "read",    \
	.type = "(tttttt)",\
	.direction = "out" \
},                         \
{                          \
	.name = "write",   \
	.type = "(tttttt)",\
	.direction = "out" \
}

#define TRANSPORT_REPLY    \
{                          \
	.name = "rx_bytes",\
	.type = "(t)",     \
	.direction = "out" \
},                         \
{                          \
	.name = "rx_pkt",  \
	.type = "(t)",     \
	.direction = "out" \
},                         \
{                          \
	.name = "rx_err",  \
	.type = "(t)",     \
	.direction = "out" \
},                         \
{                          \
	.name = "tx_bytes",\
	.type = "(t)",     \
	.direction = "out" \
},                         \
{                          \
	.name = "tx_pkt",  \
	.type = "(t)",     \
	.direction = "out" \
},                         \
{                          \
	.name = "tx_err",  \
	.type = "(t)",     \
	.direction = "out" \
}

#define TOTAL_OPS_REPLY      \
{                            \
	.name = "op",        \
	.type = "a(st)",     \
	.direction = "out"   \
}

/* We are passing back FSAL name so that ganesha_stats can show it as per
 * the FSAL name
 * The fsal_stats is an array with below items in it
 * OP_NAME, NUMBER_OF_OP, AVG_RES_TIME, MIN_RES_TIME & MAX_RES_TIME
 */
#define FSAL_OPS_REPLY      \
{                               \
	.name = "fsal_name",         \
	.type = "s",            \
	.direction = "out"       \
},				\
{                            \
	.name = "fsal_stats",        \
	.type = "a(stddd)",     \
	.direction = "out"   \
}

#define STATS_STATUS_REPLY      \
{                               \
	.name = "nfs_status",   \
	.type = "b(tt)",        \
	.direction = "out"      \
},                              \
{                               \
	.name = "fsal_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
},				\
{                               \
	.name = "v3_full_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
},				\
{                               \
	.name = "v4_full_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
}


#define V3_FULL_REPLY		\
{				\
	.name = "v3_full_stats",	\
	.type = "a(stttdddddd)",	\
	.direction = "out"	\
}

#define V4_FULL_REPLY		\
{				\
	.name = "v4_full_stats",	\
	.type = "a(sttdddddd)",	\
	.direction = "out"	\
}

#define LAYOUTS_REPLY		\
{				\
	.name = "getdevinfo",	\
	.type = "(ttt)",	\
	.direction = "out"	\
},				\
{				\
	.name = "layout_get",	\
	.type = "(ttt)",	\
	.direction = "out"	\
},				\
{				\
	.name = "layout_commit",\
	.type = "(ttt)",	\
	.direction = "out"	\
},				\
{				\
	.name = "layout_return",\
	.type = "(ttt)",	\
	.direction = "out"	\
},				\
{				\
	.name = "layout_recall",\
	.type = "(ttt)",	\
	.direction = "out"	\
}

/* number of delegations, number of sent recalls,
 * number of failed recalls, number of revokes */
#define DELEG_REPLY		       \
{				       \
	.name = "delegation_stats",    \
	.type = "(tttt)",	       \
	.direction = "out"	       \
}

#define NFS_ALL_IO_REPLY_ARRAY_TYPE "(qs(tttttt)(tttttt))"
#define NFS_ALL_IO_REPLY			\
{						\
	.name = "iostats",			\
	.type = DBUS_TYPE_ARRAY_AS_STRING	\
		NFS_ALL_IO_REPLY_ARRAY_TYPE,	\
	.direction = "out"			\
}						\


#define _9P_OP_ARG           \
{                            \
	.name = "_9p_opname",\
	.type = "s",         \
	.direction = "in"    \
}


#define OP_STATS_REPLY      \
{                           \
	.name = "op_stats", \
	.type = "(tt)",     \
	.direction = "out"  \
}


void server_stats_summary(DBusMessageIter * iter, struct gsh_stats *st);
void server_dbus_v3_iostats(struct nfsv3_stats *v3p, DBusMessageIter *iter);
void server_dbus_v40_iostats(struct nfsv40_stats *v40p, DBusMessageIter *iter);
void server_dbus_v41_iostats(struct nfsv41_stats *v41p, DBusMessageIter *iter);
void server_dbus_v41_layouts(struct nfsv41_stats *v41p, DBusMessageIter *iter);
void server_dbus_v42_iostats(struct nfsv41_stats *v42p, DBusMessageIter *iter);
void server_dbus_v42_layouts(struct nfsv41_stats *v42p, DBusMessageIter *iter);
void server_dbus_delegations(struct deleg_stats *ds, DBusMessageIter *iter);
void server_dbus_all_iostats(struct export_stats *export_statistics,
			     DBusMessageIter *iter);
void server_dbus_total_ops(struct export_stats *export_st,
			   DBusMessageIter *iter);
void global_dbus_total_ops(DBusMessageIter *iter);
void server_dbus_fast_ops(DBusMessageIter *iter);
void mdcache_dbus_show(DBusMessageIter *iter);
void server_dbus_v3_full_stats(DBusMessageIter *iter);
void server_dbus_v4_full_stats(DBusMessageIter *iter);
void reset_server_stats(void);
void reset_export_stats(void);
void reset_client_stats(void);
void reset_gsh_stats(struct gsh_stats *st);
void reset_v3_full_stats(void);
void reset_v4_full_stats(void);

#ifdef _USE_9P
void server_dbus_9p_iostats(struct _9p_stats *_9pp, DBusMessageIter *iter);
void server_dbus_9p_transstats(struct _9p_stats *_9pp, DBusMessageIter *iter);
void server_dbus_9p_tcpstats(struct _9p_stats *_9pp, DBusMessageIter *iter);
void server_dbus_9p_rdmastats(struct _9p_stats *_9pp, DBusMessageIter *iter);
void server_dbus_9p_opstats(struct _9p_stats *_9pp, u8 opcode,
			    DBusMessageIter *iter);
#endif

extern struct glist_head fsal_list;

#endif				/* USE_DBUS */

void server_stats_free(struct gsh_stats *statsp);

void server_stats_init(void);

#endif				/* !SERVER_STATS_PRIVATE_H */
/** @} */
