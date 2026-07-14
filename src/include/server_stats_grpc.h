/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) 2026, IBM . All rights reserved.
 * Author: Suhas Athani <Suhas.Athani@ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.  see <http://www.gnu.org/licenses/
 *
 * ---------------------------------------
 */

/*
 * gRPC-safe declarations for client and export statistics.
 *
 * This header is the C/C++ boundary for cltmgr and exportmgr stats RPCs.
 * C++ gRPC handlers must include this file instead of server_stats_private.h,
 * which is not C++-safe (uses the C++ keyword "export" as a field name
 * and pulls in D-Bus types).
 *
 * Implementation lives in:
 *   - support/server_stats.c  (stats field extraction)
 *   - support/client_mgr.c    (client lookup and orchestration)
 *   - support/export_mgr.c    (export lookup and orchestration)
 */

#ifndef SERVER_STATS_GRPC_H
#define SERVER_STATS_GRPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Opaque forward declarations; full structs are private to server_stats.c */
struct gsh_stats;
struct export_stats;
struct gsh_clnt_allops_stats;

/*
 * Portable I/O stats snapshot passed from C to C++ before protobuf fill.
 * Layout matches nfsProtoUtil.IoStats and D-Bus (tttttt) IOSTATS_REPLY.
 */
struct grpc_iostats {
	uint64_t requested;
	uint64_t transferred;
	uint64_t total_ops;
	uint64_t errors;
	uint64_t latency;
	uint64_t queue_wait;
};

struct grpc_total_ops {
	uint64_t nfsv3;
	uint64_t nfsv40;
	uint64_t nfsv41;
	uint64_t nfsv42;
};

struct grpc_global_total_ops {
	struct grpc_total_ops nfs;
	uint64_t nlm4;
	uint64_t mntv1;
	uint64_t mntv3;
	uint64_t rquota;
};

#define GRPC_MAX_EXPORT_IO_ENTRIES 1024

struct grpc_export_io {
	uint16_t export_id;
	char version[8];
	struct grpc_iostats read;
	struct grpc_iostats write;
};

struct grpc_export_io_list {
	struct grpc_export_io entries[GRPC_MAX_EXPORT_IO_ENTRIES];
	size_t count;
};

/* Mirrors nfsProtoUtil.LayoutStats / D-Bus layout struct (ttt). */
struct grpc_layout_stats {
	uint64_t total;
	uint64_t errors;
	uint64_t delays;
};

/* Mirrors nfsProtoUtil.ClientLayoutsResponse layout fields. */
struct grpc_layouts {
	struct grpc_layout_stats getdevinfo;
	struct grpc_layout_stats layout_get;
	struct grpc_layout_stats layout_commit;
	struct grpc_layout_stats layout_return;
	struct grpc_layout_stats layout_recall;
};

/* Mirrors nfsProtoUtil.DelegationStats / D-Bus DELEG_REPLY (tttt). */
struct grpc_delegation_stats {
	uint32_t curr_deleg_grants;
	uint32_t tot_recalls;
	uint32_t failed_recalls;
	uint32_t num_revokes;
};

/* Mirrors nfsProtoUtil.CeIoStats / D-Bus CEIOSTATS read|write (ttdt). */
struct grpc_ce_iostats {
	uint64_t total_ops;
	uint64_t errors;
	uint64_t bytes_transferred;
};

/* Mirrors nfsProtoUtil.CeOpStats / D-Bus CEIOSTATS other (ttd). */
struct grpc_ce_opstats {
	uint64_t total_ops;
	uint64_t errors;
};

/* Mirrors nfsProtoUtil.CeLayoutStats / D-Bus CELOSTATS (ttt) for clients. */
struct grpc_ce_layoutstats {
	uint64_t total;
	uint64_t errors;
};

/* Mirrors nfsProtoUtil.VersionCeStats. */
struct grpc_version_ce_stats {
	bool available;
	struct grpc_ce_iostats read;
	struct grpc_ce_iostats write;
	struct grpc_ce_opstats other;
	bool has_layout;
	struct grpc_ce_layoutstats layout;
};

/* Mirrors nfsProtoUtil.ClientIoOpsResponse payload. */
struct grpc_client_io_ops {
	struct timespec time;
#ifdef _USE_NFS3
	struct grpc_version_ce_stats v3;
#endif
	struct grpc_version_ce_stats v40;
	struct grpc_version_ce_stats v41;
	struct grpc_version_ce_stats v42;
};

/* Mirrors nfsProtoUtil.ClientOpEntry / D-Bus a(sttt). */
struct grpc_client_op_entry {
	char op_name[64];
	uint64_t total;
	uint64_t errors;
	uint64_t dups;
};

/* Mirrors nfsProtoUtil.ClientV4OpEntry / D-Bus a(stt). */
struct grpc_client_v4_op_entry {
	char op_name[64];
	uint64_t total;
	uint64_t errors;
};

/* Mirrors nfsProtoUtil.CompoundOpStats / D-Bus CLNT_CMP_OPS_REPLY (ttt). */
struct grpc_compound_op_stats {
	uint64_t total;
	uint64_t errors;
	uint64_t ops_in_cmp;
};

/*
 * Heap-allocated all-ops snapshot; freed by grpc_cltmgr_free_client_allops().
 * Entry counts reflect only operations with non-zero totals (D-Bus parity).
 */
struct grpc_client_allops {
	int comp;
	struct timespec time;
	bool clnt_v3;
	uint32_t v3_count;
	struct grpc_client_op_entry *v3_ops;
	bool clnt_nlm;
	uint32_t nlm_count;
	struct grpc_client_op_entry *nlm_ops;
	bool clnt_v4;
	uint32_t v4_count;
	struct grpc_client_v4_op_entry *v4_ops;
	bool clnt_cmp;
	struct grpc_compound_op_stats cmp;
};

/* Mirrors nfsProtoUtil.TransportStats / D-Bus TRANSPORT_REPLY. */
struct grpc_transport_stats {
	uint64_t rx_bytes;
	uint64_t rx_pkt;
	uint64_t rx_err;
	uint64_t tx_bytes;
	uint64_t tx_pkt;
	uint64_t tx_err;
};

/* Mirrors nfsProtoUtil.OpStats / D-Bus OP_STATS_REPLY (tt). */
struct grpc_op_stats {
	uint64_t total_ops;
	uint64_t errors;
};

/* Max protocol flags emitted by server_grpc_fill_stats_summary(). */
#define GRPC_MAX_PROTOCOLS 8

/* Always 3: Open, Lock, Delegation. */
#define GRPC_MAX_STATE_STATS 3

/* Mirrors nfsProtoUtil.ProtocolActivity. */
struct grpc_protocol_activity {
	char name[16];
	bool active;
};

/* Mirrors nfsProtoUtil.StateStatEntry. */
struct grpc_state_stat_entry {
	char state_type[16]; /* "Open", "Lock", "Delegation" */
	uint64_t count;
};

/* Mirrors nfsProtoUtil.ClientInfo / D-Bus CLIENT_CONTAINER. */
struct grpc_client_info {
	char ipaddr[128]; /* SOCK_NAME_MAX */
	uint32_t protocol_count;
	struct grpc_protocol_activity protocols[GRPC_MAX_PROTOCOLS];
	uint64_t total_ops;
	uint32_t state_count;
	struct grpc_state_stat_entry state_stats[GRPC_MAX_STATE_STATS];
	struct timespec last_update;
};

/*
 * Heap-allocated snapshot returned by grpc_cltmgr_show_clients().
 * Free with grpc_cltmgr_free_show_clients() after reading.
 */
struct grpc_show_clients {
	struct timespec time;
	uint32_t client_count;
	struct grpc_client_info *clients; /* heap; free via helper */
};

#define MAX_FAST_OPS 512

struct grpc_fast_op {
	char op_name[64];
	uint64_t count;
};

struct grpc_protocol_fast_ops {
	char protocol[16];
	uint32_t num_ops;
	struct grpc_fast_op ops[MAX_FAST_OPS];
};

struct grpc_fast_ops {
	uint32_t num_protocols;
	struct grpc_protocol_fast_ops protocols[5];
};

struct grpc_full_op_stats {
	char op_name[32];
	uint64_t total;
	uint64_t errors;
	uint64_t dups; /* Used only by NFSv3 */
	double avg_latency;
	double min_latency;
	double max_latency;
};

struct grpc_full_stats {
	uint32_t num_ops;
	struct grpc_full_op_stats ops[NFS_V42_NB_OPERATION];
	char message[32];
};

struct grpc_stats_status {
	bool enabled;
	struct timespec timestamp;
};

struct grpc_all_stats_status {
	struct grpc_stats_status nfs;
	struct grpc_stats_status fsal;
#ifdef _USE_NFS3
	struct grpc_stats_status v3_full;
#endif
	struct grpc_stats_status v4_full;
	struct grpc_stats_status auth;
	struct grpc_stats_status client_all_ops;
};

enum grpc_statistics_type {
	STATISTICS_TYPE_ALL = 0,
	STATISTICS_TYPE_NFS = 1,
	STATISTICS_TYPE_FSAL = 2,
	STATISTICS_TYPE_V3_FULL = 3,
	STATISTICS_TYPE_V4_FULL = 4,
	STATISTICS_TYPE_AUTH = 5,
	STATISTICS_TYPE_CLIENT_ALL_OPS = 6,
};

struct grpc_auth_stats {
	uint64_t total;
	double avg_latency;
	double max_latency;
	double min_latency;
};

struct grpc_all_auth_stats {
	struct grpc_auth_stats group_cache;
	struct grpc_auth_stats winbind;
	struct grpc_auth_stats dns;
};

struct grpc_mdcache_stats {
	uint64_t cache_requests;
	uint64_t cache_hits;
	uint64_t cache_misses;
	uint64_t cache_conflicts;
	uint64_t cache_adds;
	uint64_t cache_mapping;
};

struct grpc_lru_utilization {
	uint64_t entries_used;
	uint64_t chunks_used;
};

struct grpc_fd_usage_summary {
	uint32_t fd_limit;
	uint32_t fd_lowat;
	uint32_t fd_hiwat;
	uint32_t fd_hard_limit;
	uint32_t fd_state;
	uint32_t global_fds;
	uint32_t state_fds;
	uint64_t v4_open_states;
	uint64_t open_fds;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Extract read/write stats for a client; returns false if no version
 * activity.
 */
bool server_grpc_fill_v3_iostats(struct gsh_stats *st,
				 struct grpc_iostats *read_out,
				 struct grpc_iostats *write_out);

bool server_grpc_fill_v40_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

bool server_grpc_fill_v41_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

bool server_grpc_fill_v42_iostats(struct gsh_stats *st,
				  struct grpc_iostats *read_out,
				  struct grpc_iostats *write_out);

bool server_grpc_fill_nfsmon_iostats(struct export_stats *export_st,
				     struct grpc_iostats *read_out,
				     struct grpc_iostats *write_out);

bool server_grpc_fill_total_ops(struct export_stats *export_st,
				struct grpc_total_ops *ops);

bool server_grpc_fill_global_total_ops(struct grpc_global_total_ops *ops);

bool server_grpc_fill_all_iostats(struct export_stats *export_st,
				  struct grpc_export_io_list *list);

bool server_grpc_fill_v41_layouts(struct gsh_stats *st,
				  struct grpc_layouts *layouts_out);

bool server_grpc_fill_v42_layouts(struct gsh_stats *st,
				  struct grpc_layouts *layouts_out);

bool server_grpc_fill_delegations(struct gsh_stats *st,
				  struct grpc_delegation_stats *deleg_out);

bool server_grpc_fill_9p_iostats(struct gsh_stats *st,
				 struct grpc_iostats *read_out,
				 struct grpc_iostats *write_out);

bool server_grpc_fill_9p_transport(struct gsh_stats *st,
				   struct grpc_transport_stats *trans_out);

bool server_grpc_fill_9p_opstats(struct gsh_stats *st, uint8_t opcode,
				 struct grpc_op_stats *op_out);

bool server_grpc_fill_client_io_ops(struct gsh_stats *st,
				    struct timespec *client_time,
				    struct grpc_client_io_ops *out);

bool server_grpc_fill_ce_stats(struct gsh_stats *st, struct timespec *time,
			       struct grpc_client_io_ops *out);

struct grpc_client_allops *server_grpc_fill_client_allops(
	struct gsh_stats *st, struct gsh_clnt_allops_stats *c_all,
	struct timespec *client_time);

void grpc_cltmgr_free_client_allops(struct grpc_client_allops *allops);

bool grpc_parse_9p_opname(const char *opname, uint8_t *opcode_out);

/*
 * Per-version entry points called from nfsServiceServer.cc.
 *
 * Each function mirrors the corresponding D-Bus handler in client_mgr.c
 * (e.g. get_nfsv41_stats_io). On success, read_out/write_out and time_out
 * are filled and *success is set true with errmsg "OK". On failure,
 * *success is false and errmsg carries the D-Bus-equivalent message.
 * The return value is always true (errors are encoded in *success/errmsg,
 * matching D-Bus behaviour where the method call itself succeeds).
 */
bool grpc_cltmgr_get_v3_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v40_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v41_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v42_io(const char *ipaddr, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

/*
 * Per-version export statistics entry points.
 *
 * Mirrors the DBus export statistics methods (GetNFSv3IO,
 * GetNFSv40IO, GetNFSv41IO, GetNFSv42IO).
 */

bool grpc_export_get_v3_io(uint16_t export_id, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_export_get_v40_io(uint16_t export_id, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_export_get_v41_io(uint16_t export_id, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_export_get_v42_io(uint16_t export_id, struct grpc_iostats *read_out,
			    struct grpc_iostats *write_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_export_get_nfsmon_io(uint16_t exportid, struct grpc_iostats *read_out,
			       struct grpc_iostats *write_out,
			       struct timespec *time_out, bool *success,
			       char *errmsg, size_t errmsg_len);

bool grpc_export_get_total_ops(uint16_t export_id, struct grpc_total_ops *ops,
			       struct timespec *time_out, bool *success,
			       char *errmsg, size_t errmsg_len);

bool grpc_get_global_total_ops(struct grpc_global_total_ops *ops,
			       struct timespec *time_out, bool *success,
			       char *errmsg, size_t errmsg_len);

bool grpc_get_all_export_iostats(struct grpc_export_io_list *list,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v41_layouts(const char *ipaddr,
				 struct grpc_layouts *layouts_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_v42_layouts(const char *ipaddr,
				 struct grpc_layouts *layouts_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_delegations(const char *ipaddr,
				 struct grpc_delegation_stats *deleg_out,
				 struct timespec *time_out, bool *success,
				 char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_client_io_ops(const char *ipaddr,
				   struct grpc_client_io_ops *ops_out,
				   bool *success, char *errmsg,
				   size_t errmsg_len);

bool grpc_cltmgr_get_client_allops(const char *ipaddr,
				   struct grpc_client_allops **allops_out,
				   bool *success, char *errmsg,
				   size_t errmsg_len);

bool grpc_cltmgr_get_9p_io(const char *ipaddr, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_9p_trans(const char *ipaddr,
			      struct grpc_transport_stats *trans_out,
			      struct timespec *time_out, bool *success,
			      char *errmsg, size_t errmsg_len);

bool grpc_cltmgr_get_9p_opstats(const char *ipaddr, const char *opname,
				struct grpc_op_stats *op_out,
				struct timespec *time_out, bool *success,
				char *errmsg, size_t errmsg_len);

/*
 * Extract per-client protocol-activity flags and total op count.
 * Mirrors server_stats_summary() for gRPC, without D-Bus types.
 */
void server_grpc_fill_stats_summary(struct gsh_stats *st,
				    struct grpc_protocol_activity *protos,
				    uint32_t *proto_count,
				    uint64_t *total_ops_out);

bool grpc_cltmgr_add_client(const char *ipaddr, bool *success, char *errmsg,
			    size_t errmsg_len);

bool grpc_cltmgr_remove_client(const char *ipaddr, bool *success, char *errmsg,
			       size_t errmsg_len);

bool grpc_cltmgr_show_clients(struct grpc_show_clients **out, bool *success,
			      char *errmsg, size_t errmsg_len);

void grpc_cltmgr_free_show_clients(struct grpc_show_clients *show);

bool grpc_cltmgr_disconnect_nfsv41_client(const char *ipaddr,
					  int32_t *connections_destroyed,
					  bool *success, char *errmsg,
					  size_t errmsg_len);

bool grpc_export_get_v41_layout_stats(uint32_t export_id,
				      struct grpc_layouts *layouts,
				      struct timespec *time_out, bool *success,
				      char *errmsg, size_t errmsg_len);

bool grpc_export_get_v42_layout_stats(uint32_t export_id,
				      struct grpc_layouts *layouts,
				      struct timespec *time_out, bool *success,
				      char *errmsg, size_t errmsg_len);

bool grpc_export_get_9p_io(uint16_t export_id, struct grpc_iostats *read,
			   struct grpc_iostats *write,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_export_get_9p_opstats(uint32_t export_id, const char *opname,
				struct grpc_op_stats *op_out,
				struct timespec *time_out, bool *success,
				char *errmsg, size_t errmsg_len);

bool grpc_export_get_v3_io(uint16_t exportid, struct grpc_iostats *read_out,
			   struct grpc_iostats *write_out,
			   struct timespec *time_out, bool *success,
			   char *errmsg, size_t errmsg_len);

bool grpc_get_fast_ops(struct grpc_fast_ops *fast_ops,
		       struct timespec *time_out, bool *success, char *errmsg,
		       size_t errmsg_len);

bool grpc_get_v3_full_stats(struct grpc_full_stats *stats_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_get_v4_full_stats(struct grpc_full_stats *stats_out,
			    struct timespec *time_out, bool *success,
			    char *errmsg, size_t errmsg_len);

bool grpc_reset_stats(struct timespec *time_out, bool *success, char *errmsg,
		      size_t errmsg_len);

bool grpc_enable_stats(enum grpc_statistics_type stat_type,
		       struct timespec *time_out, bool *success, char *errmsg,
		       size_t errmsg_len);

bool grpc_disable_stats(uint32_t stat_type, struct timespec *time_out,
			bool *success, char *errmsg, size_t errmsg_len);

bool grpc_status_stats(struct grpc_all_stats_status *status_out, bool *success,
		       char *errmsg, size_t errmsg_len);

bool grpc_get_auth_stats(struct grpc_all_auth_stats *stats_out,
			 struct timespec *time_out, bool *success, char *errmsg,
			 size_t errmsg_len);

bool grpc_show_mdcache_stats(struct grpc_mdcache_stats *cache_out,
			     struct grpc_lru_utilization *lru_out,
			     struct timespec *time_out, bool *success,
			     char *errmsg, size_t errmsg_len);

bool grpc_get_export_details(uint32_t export_id, struct grpc_client_io_ops *out,
			     bool *success, char *errmsg, size_t errmsg_len);

bool grpc_show_fd_usage(struct grpc_fd_usage_summary *summary_out,
			struct timespec *time_out, bool *success, char *errmsg,
			size_t errmsg_len);
#ifdef __cplusplus
}
#endif

#endif /* SERVER_STATS_GRPC_H */
