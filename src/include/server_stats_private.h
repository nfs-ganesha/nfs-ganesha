/* SPDX-License-Identifier: LGPL-3.0-or-later */

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

#ifdef _USE_NFS3
struct nfsv3_stats;
struct mnt_stats;
#endif
#ifdef _USE_NLM
struct nlmv4_stats;
#endif
#ifdef _USE_RQUOTA
struct rquota_stats;
#endif
struct nfsv40_stats;
struct nfsv41_stats;
struct nfsv42_stats;
struct deleg_stats;
#ifdef _USE_9P
struct _9p_stats;
#endif

#ifdef _USE_NFS3
struct clnt_allops_v3_stats;
#endif
struct clnt_allops_v4_stats;
#ifdef _USE_NLM
struct clnt_allops_nlm4_stats;
#endif

struct gsh_stats {
#ifdef _USE_NFS3
	struct nfsv3_stats *nfsv3;
	struct mnt_stats *mnt;
#endif
#ifdef _USE_NLM
	struct nlmv4_stats *nlm4;
#endif
#ifdef _USE_RQUOTA
	struct rquota_stats *rquota;
#endif
	struct nfsv40_stats *nfsv40;
	struct nfsv41_stats *nfsv41;
	struct nfsv41_stats *nfsv42;
	struct deleg_stats *deleg;
#ifdef _USE_9P
	struct _9p_stats *_9p;
#endif
};

struct gsh_clnt_allops_stats {
#ifdef _USE_NFS3
	struct clnt_allops_v3_stats *nfsv3;
#endif
	struct clnt_allops_v4_stats *nfsv4;
#ifdef _USE_NLM
	struct clnt_allops_nlm_stats *nlm4;
#endif
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
	struct gsh_clnt_allops_stats c_all; /* for all ops stats */
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

/**
 * @brief Auth stats information
 */
struct auth_stats {
	uint64_t total;
	uint64_t latency;
	uint64_t max;
	uint64_t min;
};

#ifdef USE_DBUS

/*
 * Common definitions
 */

#define TYPE_ID "q"
#define TYPE_STRING "s"

/*
 * Bits for exports/clients related info
 *
 * We may like the protocols related info like following:
 *
 * {
 *     string <Protocols>
 *     bool   <status>
 * }
 *      .
 *      .
 *      .
 */

#ifdef _USE_NFS3
#define STAT_TYPE_NFSV3 "(sb)"
#define STAT_TYPE_MNT "(sb)"
#else
#define STAT_TYPE_NFSV3 ""
#define STAT_TYPE_MNT ""
#endif

#ifdef _USE_NLM
#define STAT_TYPE_NLM "(sb)"
#else
#define STAT_TYPE_NLM ""
#endif

#ifdef _USE_RQUOTA
#define STAT_TYPE_RQUOTA "(sb)"
#else
#define STAT_TYPE_RQUOTA ""
#endif

#define STAT_TYPE_NFSV40 "(sb)"
#define STAT_TYPE_NFSV41 "(sb)"
#define STAT_TYPE_NFSV42 "(sb)"

#ifdef _USE_9P
#define STAT_TYPE_9P "(sb)"
#else
#define STAT_TYPE_9P ""
#endif

#define PROTOCOLS_CONTAINER \
	"(" STAT_TYPE_NFSV3 STAT_TYPE_MNT \
	STAT_TYPE_NLM STAT_TYPE_RQUOTA \
	STAT_TYPE_NFSV40 STAT_TYPE_NFSV41 \
	STAT_TYPE_NFSV42 STAT_TYPE_9P ")"

#define EXPORT_CONTAINER \
	"(" TYPE_ID TYPE_STRING \
	PROTOCOLS_CONTAINER "(tt))"

#define CLIENT_CONTAINER \
	"(" TYPE_STRING \
	PROTOCOLS_CONTAINER "(tt))"

#define EXPORTS_REPLY			\
{					\
	.name = "exports",		\
	.type = "a"EXPORT_CONTAINER,	\
	.direction = "out"		\
}

#define CLIENTS_REPLY			\
{					\
	.name = "clients",		\
	.type = "a"CLIENT_CONTAINER,	\
	.direction = "out"		\
}


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

#define CEIOSTATS_REPLY    \
{                          \
	.name = "read",    \
	.type = "(ttdt)",  \
	.direction = "out" \
},                         \
{                          \
	.name = "write",   \
	.type = "(ttdt)",  \
	.direction = "out" \
},                         \
{                          \
	.name = "other",   \
	.type = "(ttd)",   \
	.direction = "out" \
}

#define CELOSTATS_REPLY    \
{                          \
	.name = "layout",  \
	.type = "(ttt)",  \
	.direction = "out" \
}

#ifdef _USE_NFS3
#define CE_STATS_REPLY      \
{                           \
	.name = "clnt_v3",  \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
{                           \
	.name = "clnt_v40", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
{                           \
	.name = "clnt_v41", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
CELOSTATS_REPLY,            \
{                           \
	.name = "clnt_v42", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
CELOSTATS_REPLY
#else
#define CE_STATS_REPLY      \
CEIOSTATS_REPLY,            \
{                           \
	.name = "clnt_v40", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
{                           \
	.name = "clnt_v41", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
CELOSTATS_REPLY,            \
{                           \
	.name = "clnt_v42", \
	.type = "b",        \
	.direction = "out"  \
},                          \
CEIOSTATS_REPLY,            \
CELOSTATS_REPLY
#endif

#ifdef _USE_NFS3
#define CLNT_V3NLM_OPS_REPLY		\
{					\
	.name = "clnt_v3nlm_ops_stats",	\
	.type = "a(sttt)",		\
	.direction = "out"		\
}
#endif

#define CLNT_V4_OPS_REPLY		\
{					\
	.name = "clnt_v4_ops_stats",	\
	.type = "a(stt)",		\
	.direction = "out"		\
}

#define CLNT_CMP_OPS_REPLY		\
{					\
	.name = "clnt_cmp_ops_stats",	\
	.type = "ttt",			\
	.direction = "out"		\
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

#ifdef _USE_NFS3
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
},				\
{                               \
	.name = "auth_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
},				\
{                               \
	.name = "clnt_allops_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
}
#else
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
	.name = "v4_full_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
},				\
{                               \
	.name = "auth_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
},				\
{                               \
	.name = "clnt_allops_status",  \
	.type = "b(tt)",        \
	.direction = "out"      \
}
#endif

#ifdef _USE_NFS3
#define V3_FULL_REPLY		\
{				\
	.name = "v3_full_stats",	\
	.type = "a(stttddd)",	\
	.direction = "out"	\
}
#endif

#define V4_FULL_REPLY		\
{				\
	.name = "v4_full_stats",	\
	.type = "a(sttddd)",	\
	.direction = "out"	\
}

#define AUTH_REPLY		\
{                               \
	.name = "auth",		\
	.type = "a(tdddtdddtddd)",  \
	.direction = "out"      \
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


#ifdef _USE_9P
#define _9P_OP_ARG           \
{                            \
	.name = "_9p_opname",\
	.type = "s",         \
	.direction = "in"    \
}
#endif

#define OP_STATS_REPLY      \
{                           \
	.name = "op_stats", \
	.type = "(tt)",     \
	.direction = "out"  \
}

#define LRU_UTILIZATION_REPLY      \
{                           \
	.name = "lru_data_utilization", \
	.type = "stsussstst",     \
	.direction = "out"  \
}


extern struct timespec auth_stats_time;
#ifdef _USE_NFS3
extern struct timespec v3_full_stats_time;
#endif
extern struct timespec v4_full_stats_time;


void server_stats_summary(DBusMessageIter *iter, struct gsh_stats *st);
void server_dbus_client_io_ops(DBusMessageIter *iter,
				struct gsh_client *client);
void server_dbus_client_all_ops(DBusMessageIter *iter,
				struct gsh_client *client);
void server_dbus_export_details(DBusMessageIter *iter,
				struct gsh_export *g_export);
#ifdef _USE_NFS3
void server_dbus_v3_iostats(struct nfsv3_stats *v3p, DBusMessageIter *iter);
#endif
void server_dbus_v40_iostats(struct nfsv40_stats *v40p, DBusMessageIter *iter);
void server_dbus_v41_iostats(struct nfsv41_stats *v41p, DBusMessageIter *iter);
void server_dbus_v41_layouts(struct nfsv41_stats *v41p, DBusMessageIter *iter);
void server_dbus_v42_iostats(struct nfsv41_stats *v42p, DBusMessageIter *iter);
void server_dbus_v42_layouts(struct nfsv41_stats *v42p, DBusMessageIter *iter);
void server_dbus_nfsmon_iostats(struct export_stats *export_st,
				DBusMessageIter *iter);
void server_dbus_delegations(struct deleg_stats *ds, DBusMessageIter *iter);
void server_dbus_all_iostats(struct export_stats *export_statistics,
			     DBusMessageIter *iter);
void server_dbus_total_ops(struct export_stats *export_st,
			   DBusMessageIter *iter);
void global_dbus_total_ops(DBusMessageIter *iter);
void server_dbus_fast_ops(DBusMessageIter *iter);
void mdcache_dbus_show(DBusMessageIter *iter);
void mdcache_utilization(DBusMessageIter *iter);
#ifdef _USE_NFS3
void server_dbus_v3_full_stats(DBusMessageIter *iter);
#endif
void server_dbus_v4_full_stats(DBusMessageIter *iter);
void reset_server_stats(void);
void reset_export_stats(void);
void reset_client_stats(void);
void reset_gsh_stats(struct gsh_stats *st);
void reset_gsh_allops_stats(struct gsh_clnt_allops_stats *st);
#ifdef _USE_NFS3
void reset_v3_full_stats(void);
#endif
void reset_v4_full_stats(void);
void reset_auth_stats(void);
void reset_clnt_allops_stats(void);

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
void server_stats_allops_free(struct gsh_clnt_allops_stats *statsp);

void server_stats_init(void);

#endif				/* !SERVER_STATS_PRIVATE_H */
/** @} */
