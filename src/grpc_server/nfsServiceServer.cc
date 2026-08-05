/* Copyright (C) 2025, IBM
 * Contributor : Avani Rateria <arateria@redhat.com>
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

#include <string>
#include <vector>
#include "nfsService.h"
#include "server_stats_grpc.h"
#include "idmapper.h"
#include "uid2grp.h"
#include "netgroup_cache.h"
#include "mdcache.h"
#include "export_mgr.h"
#include "nfs_exports.h"
#include "cluster_members.h"

#ifdef LINUX
#include <mcheck.h> /* For mtrace/muntrace */
#endif

#ifndef __APPLE__
#include <malloc.h>
#endif

#include "nfs_lib.h"
#include "avltree.h"
#include "idmapper.h"
#include "export_mgr.h"
#include "nfs_exports.h"
#include "client_mgr.h"
#include "config_parsing.h"
#include "nfs_proto_functions.h"
#include "nfs_cbsim_grpc.h"

#define MAX_PROTOCOLS 8

grpc::Status GetClientIdService::GetClientIds(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsService::GetClientIdsResponse *response)
{
	hash_table_t *ht = ht_confirmed_client_id;

	for (uint32_t i = 0; i < ht->parameter.index_size; ++i) {
		struct rbt_head *head_rbt = &(ht->partitions[i].rbt);

		PTHREAD_RWLOCK_rdlock(&(ht->partitions[i].ht_lock));
		struct rbt_node *pn;
		RBT_LOOP(head_rbt, pn)
		{
			const struct hash_data *pdata =
				(hash_data *)RBT_OPAQ(pn);
			const nfs_client_id_t *pclientid =
				(nfs_client_id_t *)pdata->val.addr;
			const uint64_t clientid = pclientid->cid_clientid;
			// Add the client ID to the list
			response->add_client_ids(clientid);
			RBT_INCREMENT(pn);
		} // RBT_LOOP

		PTHREAD_RWLOCK_unlock(&(ht->partitions[i].ht_lock));
	} // for loop

	return grpc::Status::OK;
}

grpc::Status GetNfsGraceService::GetGracePeriod(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsService::GetNfsGraceResponse *response)
{
	// Set the response
	response->set_ingrace(nfs_in_grace());

	return grpc::Status::OK;
}

grpc::Status StartNfsGraceService::StartGraceWithEvent(
	grpc::ServerContext *context, const nfsService::GraceWithEvent *request,
	nfsService::GraceStatus *response)
{
	int ret;
	int event = request->event();
	int nodeid = request->nodeid();
	std::string ip_addr = request->ipaddr();
	std::string resp;
	nfs_grace_start_t gsp;

	// Carry out required action
	gsp.nodeid = nodeid;
	gsp.event = event;
	gsp.ipaddr = (char *)ip_addr.c_str();
	do {
		ret = nfs_start_grace(&gsp);
		/*
                 * grace could fail if there are refs taken.
                 * wait for no refs and retry.
                 */
		if (ret == -EAGAIN) {
			LogEvent(COMPONENT_GRPC, "Retry grace");
			nfs_wait_for_grace_norefs();
		} else if (ret) {
			LogCrit(COMPONENT_GRPC, "Start grace failed %d", ret);
			resp = "Unable to start grace";
			response->set_gracestarted(false);
			break;
		}
	} while (ret);
	// Send back the response
	if (!ret) {
		resp = "Grace started successfully";
		response->set_gracestarted(true);
	}
	response->set_response_msg(resp);

	return grpc::Status::OK;
}

grpc::Status GetSessionIdService::GetSessionIds(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsService::GetSessionIdsResponse *response)
{
	uint32_t i;
	hash_table_t *ht = ht_session_id;
	struct rbt_head *head_rbt;
	struct hash_data *pdata = NULL;
	struct rbt_node *pn;
	std::string session_id('\0', 2 * NFS4_SESSIONID_SIZE);
	nfs41_session_t *session_data;

	for (i = 0; i < ht->parameter.index_size; i++) {
		head_rbt = &(ht->partitions[i].rbt);
		PTHREAD_RWLOCK_rdlock(&(ht->partitions[i].ht_lock));
		RBT_LOOP(head_rbt, pn)
		{
			pdata = (hash_data *)RBT_OPAQ(pn);
			session_data = (nfs41_session_t *)pdata->val.addr;

			b64_ntop((unsigned char *)session_data->session_id,
				 NFS4_SESSIONID_SIZE, session_id.data(),
				 (2 * NFS4_SESSIONID_SIZE));
			// Set the response
			response->add_session_ids(session_id);
			RBT_INCREMENT(pn);
		}
		PTHREAD_RWLOCK_unlock(&(ht->partitions[i].ht_lock));
	}
	return grpc::Status::OK;
}

/**
 * @brief Fake/force a CB_RECALL for the given client id
 *
 * gRPC equivalent of org.ganesha.nfsd.cbsim.fake_recall. Callback
 * internals live in C (nfs_cbsim_grpc.c) so this TU stays free of
 * nfs_rpc_callback.h / C linkage issues.
 */
grpc::Status
FakeRecallService::FakeRecall(grpc::ServerContext *context,
			      const nfsService::FakeRecallRequest *request,
			      nfsProtoUtil::StatusResponse *response)
{
	bool success = false;
	char errmsg[256];

	(void)context;
	grpc_cbsim_fake_recall(request->client_id(), &success, errmsg,
			       sizeof(errmsg));
	response->set_success(success);
	response->set_error_msg(errmsg);
	return grpc::Status::OK;
}

/**
 * @brief Convert C-side I/O stats into protobuf response fields
 *
 * @param src [IN] gRPC-safe C stats snapshot
 * @param dst [OUT] protobuf I/O stats message
 */
static void fill_iostats_proto(const struct grpc_iostats *src,
			       nfsProtoUtil::IoStats *dst)
{
	dst->set_requested(src->requested);
	dst->set_transferred(src->transferred);
	dst->set_total_ops(src->total_ops);
	dst->set_errors(src->errors);
	dst->set_latency(src->latency);
	dst->set_queue_wait(src->queue_wait);
}

typedef bool (*grpc_cltmgr_get_io_fn)(const char *, struct grpc_iostats *,
				      struct grpc_iostats *, struct timespec *,
				      bool *, char *, size_t);

/**
 * @brief Common handler for cltmgr per-client I/O stats RPCs
 *
 * @param request [IN] protobuf request containing client IP address
 * @param response [OUT] protobuf response to be populated
 * @param get_io [IN] per-version C stats lookup function
 *
 * @return grpc::Status::OK; API failures are encoded in response status
 */
static grpc::Status
handle_client_iostats(const nfsProtoUtil::ClientIpRequest *request,
		      cltmgrService::ClientIoStatsResponse *response,
		      grpc_cltmgr_get_io_fn get_io)
{
	struct grpc_iostats read_out, write_out;
	struct timespec time_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	/* Delegate client lookup and stats extraction to C-side cltmgr
	 * code.
	 */
	get_io(request->ipaddr().c_str(), &read_out, &write_out, &time_out,
	       &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	/* Keep transport OK; API errors are reported in the response
	 * status.
	 */
	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	fill_iostats_proto(&read_out, response->mutable_read());
	fill_iostats_proto(&write_out, response->mutable_write());

	return grpc::Status::OK;
}

/**
 * @brief Get per-client NFSv3 read/write counters
 */
grpc::Status
ClientStatsService::GetNFSv3IO(grpc::ServerContext *context,
			       const nfsProtoUtil::ClientIpRequest *request,
			       cltmgrService::ClientIoStatsResponse *response)
{
	return handle_client_iostats(request, response, grpc_cltmgr_get_v3_io);
}

/**
 * @brief Get per-client NFSv4.0 read/write counters
 */
grpc::Status
ClientStatsService::GetNFSv40IO(grpc::ServerContext *context,
				const nfsProtoUtil::ClientIpRequest *request,
				cltmgrService::ClientIoStatsResponse *response)
{
	return handle_client_iostats(request, response, grpc_cltmgr_get_v40_io);
}

/**
 * @brief Get per-client NFSv4.1 read/write counters
 */
grpc::Status
ClientStatsService::GetNFSv41IO(grpc::ServerContext *context,
				const nfsProtoUtil::ClientIpRequest *request,
				cltmgrService::ClientIoStatsResponse *response)
{
	return handle_client_iostats(request, response, grpc_cltmgr_get_v41_io);
}

/**
 * @brief Get per-client NFSv4.2 read/write counters
 */
grpc::Status
ClientStatsService::GetNFSv42IO(grpc::ServerContext *context,
				const nfsProtoUtil::ClientIpRequest *request,
				cltmgrService::ClientIoStatsResponse *response)
{
	return handle_client_iostats(request, response, grpc_cltmgr_get_v42_io);
}

/**
 * @brief Convert C-side layout stats into protobuf response fields
 */
static void fill_layout_stats_proto(const struct grpc_layout_stats *src,
				    nfsProtoUtil::LayoutStats *dst)
{
	dst->set_total(src->total);
	dst->set_errors(src->errors);
	dst->set_delays(src->delays);
}

typedef bool (*grpc_cltmgr_get_layouts_fn)(const char *, struct grpc_layouts *,
					   struct timespec *, bool *, char *,
					   size_t);

/**
 * @brief Common handler for cltmgr per-client layout stats RPCs
 */
static grpc::Status
handle_client_layouts(const nfsProtoUtil::ClientIpRequest *request,
		      cltmgrService::ClientLayoutsResponse *response,
		      grpc_cltmgr_get_layouts_fn get_layouts)
{
	struct grpc_layouts layouts_out;
	struct timespec time_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	get_layouts(request->ipaddr().c_str(), &layouts_out, &time_out,
		    &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	fill_layout_stats_proto(&layouts_out.getdevinfo,
				response->mutable_getdevinfo());
	fill_layout_stats_proto(&layouts_out.layout_get,
				response->mutable_layout_get());
	fill_layout_stats_proto(&layouts_out.layout_commit,
				response->mutable_layout_commit());
	fill_layout_stats_proto(&layouts_out.layout_return,
				response->mutable_layout_return());
	fill_layout_stats_proto(&layouts_out.layout_recall,
				response->mutable_layout_recall());

	return grpc::Status::OK;
}

grpc::Status ClientStatsService::GetNFSv41Layouts(
	grpc::ServerContext *context,
	const nfsProtoUtil::ClientIpRequest *request,
	cltmgrService::ClientLayoutsResponse *response)
{
	return handle_client_layouts(request, response,
				     grpc_cltmgr_get_v41_layouts);
}

grpc::Status ClientStatsService::GetNFSv42Layouts(
	grpc::ServerContext *context,
	const nfsProtoUtil::ClientIpRequest *request,
	cltmgrService::ClientLayoutsResponse *response)
{
	return handle_client_layouts(request, response,
				     grpc_cltmgr_get_v42_layouts);
}

/**
 * @brief Get per-client delegation counters
 */
grpc::Status ClientStatsService::GetDelegations(
	grpc::ServerContext *context,
	const nfsProtoUtil::ClientIpRequest *request,
	cltmgrService::ClientDelegationsResponse *response)
{
	struct grpc_delegation_stats deleg_out;
	struct timespec time_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_get_delegations(request->ipaddr().c_str(), &deleg_out,
				    &time_out, &success, errmsg,
				    sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	nfsProtoUtil::DelegationStats *deleg =
		response->mutable_delegation_stats();

	deleg->set_curr_deleg_grants(deleg_out.curr_deleg_grants);
	deleg->set_tot_recalls(deleg_out.tot_recalls);
	deleg->set_failed_recalls(deleg_out.failed_recalls);
	deleg->set_num_revokes(deleg_out.num_revokes);

	return grpc::Status::OK;
}

/**
 * @brief Fill one VersionCeStats protobuf block from C snapshot
 */
static void fill_version_ce_stats_proto(const struct grpc_version_ce_stats *src,
					nfsProtoUtil::VersionCeStats *dst)
{
	dst->set_available(src->available);
	if (!src->available)
		return;

	nfsProtoUtil::CeIoStats *read = dst->mutable_read();
	nfsProtoUtil::CeIoStats *write = dst->mutable_write();
	nfsProtoUtil::CeOpStats *other = dst->mutable_other();

	read->set_total_ops(src->read.total_ops);
	read->set_errors(src->read.errors);
	read->set_bytes_transferred(src->read.bytes_transferred);
	write->set_total_ops(src->write.total_ops);
	write->set_errors(src->write.errors);
	write->set_bytes_transferred(src->write.bytes_transferred);
	other->set_total_ops(src->other.total_ops);
	other->set_errors(src->other.errors);

	if (src->has_layout) {
		nfsProtoUtil::CeLayoutStats *layout = dst->mutable_layout();

		layout->set_total(src->layout.total);
		layout->set_errors(src->layout.errors);
	}
}

/**
 * @brief Get per-client multi-protocol I/O op statistics
 */
grpc::Status
ClientStatsService::GetClientIOops(grpc::ServerContext *context,
				   const nfsProtoUtil::ClientIpRequest *request,
				   cltmgrService::ClientIoOpsResponse *response)
{
	struct grpc_client_io_ops ops_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_get_client_io_ops(request->ipaddr().c_str(), &ops_out,
				      &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(ops_out.time.tv_sec);
	time->set_tv_nsec(ops_out.time.tv_nsec);

#ifdef _USE_NFS3
	fill_version_ce_stats_proto(&ops_out.v3, response->mutable_clnt_v3());
#endif
	fill_version_ce_stats_proto(&ops_out.v40, response->mutable_clnt_v40());
	fill_version_ce_stats_proto(&ops_out.v41, response->mutable_clnt_v41());
	fill_version_ce_stats_proto(&ops_out.v42, response->mutable_clnt_v42());

	return grpc::Status::OK;
}

/**
 * @brief Get per-client all-ops statistics
 */
grpc::Status ClientStatsService::GetClientAllops(
	grpc::ServerContext *context,
	const nfsProtoUtil::ClientIpRequest *request,
	cltmgrService::ClientAllOpsResponse *response)
{
	struct grpc_client_allops *allops_out = NULL;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_get_client_allops(request->ipaddr().c_str(), &allops_out,
				      &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(allops_out->time.tv_sec);
	time->set_tv_nsec(allops_out->time.tv_nsec);

	response->set_clnt_v3(allops_out->clnt_v3);
	for (uint32_t i = 0; i < allops_out->v3_count; i++) {
		nfsProtoUtil::ClientOpEntry *entry =
			response->add_clnt_v3_ops_stats();

		entry->set_op_name(allops_out->v3_ops[i].op_name);
		entry->set_total(allops_out->v3_ops[i].total);
		entry->set_errors(allops_out->v3_ops[i].errors);
		entry->set_dups(allops_out->v3_ops[i].dups);
	}

	response->set_clnt_nlm(allops_out->clnt_nlm);
	for (uint32_t i = 0; i < allops_out->nlm_count; i++) {
		nfsProtoUtil::ClientOpEntry *entry =
			response->add_clnt_nlm_ops_stats();

		entry->set_op_name(allops_out->nlm_ops[i].op_name);
		entry->set_total(allops_out->nlm_ops[i].total);
		entry->set_errors(allops_out->nlm_ops[i].errors);
		entry->set_dups(allops_out->nlm_ops[i].dups);
	}

	response->set_clnt_v4(allops_out->clnt_v4);
	for (uint32_t i = 0; i < allops_out->v4_count; i++) {
		nfsProtoUtil::ClientV4OpEntry *entry =
			response->add_clnt_v4_ops_stats();

		entry->set_op_name(allops_out->v4_ops[i].op_name);
		entry->set_total(allops_out->v4_ops[i].total);
		entry->set_errors(allops_out->v4_ops[i].errors);
	}

	response->set_clnt_cmp(allops_out->clnt_cmp);
	if (allops_out->clnt_cmp) {
		nfsProtoUtil::CompoundOpStats *cmp =
			response->mutable_clnt_cmp_ops_stats();

		cmp->set_total(allops_out->cmp.total);
		cmp->set_errors(allops_out->cmp.errors);
		cmp->set_ops_in_cmp(allops_out->cmp.ops_in_cmp);
	}

	grpc_cltmgr_free_client_allops(allops_out);

	return grpc::Status::OK;
}

/**
 * @brief Get per-client 9p read/write counters
 */
grpc::Status
ClientStatsService::Get9pIO(grpc::ServerContext *context,
			    const nfsProtoUtil::ClientIpRequest *request,
			    cltmgrService::ClientIoStatsResponse *response)
{
	return handle_client_iostats(request, response, grpc_cltmgr_get_9p_io);
}

/**
 * @brief Get per-client 9p transport counters
 */
grpc::Status
ClientStatsService::Get9pTrans(grpc::ServerContext *context,
			       const nfsProtoUtil::ClientIpRequest *request,
			       cltmgrService::ClientTransportResponse *response)
{
	struct grpc_transport_stats trans_out;
	struct timespec time_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_get_9p_trans(request->ipaddr().c_str(), &trans_out,
				 &time_out, &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	nfsProtoUtil::TransportStats *trans = response->mutable_transport();

	trans->set_rx_bytes(trans_out.rx_bytes);
	trans->set_rx_pkt(trans_out.rx_pkt);
	trans->set_rx_err(trans_out.rx_err);
	trans->set_tx_bytes(trans_out.tx_bytes);
	trans->set_tx_pkt(trans_out.tx_pkt);
	trans->set_tx_err(trans_out.tx_err);

	return grpc::Status::OK;
}

/**
 * @brief Get per-client 9p operation counters
 */
grpc::Status
ClientStatsService::Get9pOpStats(grpc::ServerContext *context,
				 const nfsProtoUtil::Client9pOpRequest *request,
				 cltmgrService::ClientOpStatsResponse *response)
{
	struct grpc_op_stats op_out;
	struct timespec time_out;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_get_9p_opstats(request->ipaddr().c_str(),
				   request->op_name().c_str(), &op_out,
				   &time_out, &success, errmsg, sizeof(errmsg));

	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	nfsProtoUtil::OpStats *op = response->mutable_op_stats();

	op->set_total_ops(op_out.total_ops);
	op->set_errors(op_out.errors);

	return grpc::Status::OK;
}

/**
 * @brief Add a client entry for the given IP address
 */
grpc::Status
ClientMgrService::AddClient(grpc::ServerContext *context,
			    const nfsProtoUtil::ClientIpRequest *request,
			    nfsProtoUtil::StatusResponse *response)
{
	bool success = false;
	char errmsg[256];

	grpc_cltmgr_add_client(request->ipaddr().c_str(), &success, errmsg,
			       sizeof(errmsg));
	response->set_success(success);
	response->set_error_msg(errmsg);
	return grpc::Status::OK;
}

/**
 * @brief Remove the client entry for the given IP address
 */
grpc::Status
ClientMgrService::RemoveClient(grpc::ServerContext *context,
			       const nfsProtoUtil::ClientIpRequest *request,
			       nfsProtoUtil::StatusResponse *response)
{
	bool success = false;
	char errmsg[256];

	grpc_cltmgr_remove_client(request->ipaddr().c_str(), &success, errmsg,
				  sizeof(errmsg));
	response->set_success(success);
	response->set_error_msg(errmsg);
	return grpc::Status::OK;
}

/**
 * @brief Return a snapshot of all known clients with protocol and state
 *        counters
 */
grpc::Status ClientMgrService::ShowClients(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	cltmgrService::ShowClientsResponse *response)
{
	struct grpc_show_clients *show = NULL;
	bool success = false;
	char errmsg[256];

	(void)request;
	grpc_cltmgr_show_clients(&show, &success, errmsg, sizeof(errmsg));
	if (!success || show == NULL)
		return grpc::Status::OK;

	nfsProtoUtil::Timestamp *time = response->mutable_time();

	time->set_tv_sec(show->time.tv_sec);
	time->set_tv_nsec(show->time.tv_nsec);

	for (uint32_t i = 0; i < show->client_count; i++) {
		const struct grpc_client_info *src = &show->clients[i];
		nfsProtoUtil::ClientInfo *dst = response->add_clients();

		dst->set_ipaddr(src->ipaddr);
		dst->set_total_ops(src->total_ops);

		for (uint32_t p = 0; p < src->protocol_count; p++) {
			nfsProtoUtil::ProtocolActivity *pa =
				dst->add_protocols();

			pa->set_name(src->protocols[p].name);
			pa->set_active(src->protocols[p].active);
		}

		for (uint32_t s = 0; s < src->state_count; s++) {
			nfsProtoUtil::StateStatEntry *ss =
				dst->add_state_stats();

			ss->set_state_type(src->state_stats[s].state_type);
			ss->set_count(src->state_stats[s].count);
		}

		nfsProtoUtil::Timestamp *lu = dst->mutable_last_update();

		lu->set_tv_sec(src->last_update.tv_sec);
		lu->set_tv_nsec(src->last_update.tv_nsec);
	}

	grpc_cltmgr_free_show_clients(show);
	return grpc::Status::OK;
}

/**
 * @brief Destroy all NFSv4.1/4.2 connections for the given client IP.
 *
 * Named DisconnectNfsv41Client for D-Bus parity
 * (org.ganesha.nfsd.clientmgr.DisconnectNfsv41Client).
 * Implementation covers all minor versions != 0 (v4.1 and v4.2).
 */
grpc::Status ClientMgrService::DisconnectNfsv41Client(
	grpc::ServerContext *context,
	const nfsProtoUtil::ClientIpRequest *request,
	cltmgrService::DisconnectClientResponse *response)
{
	int32_t destroyed = 0;
	bool success = false;
	char errmsg[256];
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_cltmgr_disconnect_nfsv41_client(request->ipaddr().c_str(),
					     &destroyed, &success, errmsg,
					     sizeof(errmsg));
	status->set_success(success);
	status->set_error_msg(errmsg);
	response->set_connections_destroyed(destroyed);
	return grpc::Status::OK;
}

/**
 * @brief Replace Cluster_Members peer list used for internal NSM fan-out
 */
grpc::Status ClusterMembersService::SetClusterMembers(
	grpc::ServerContext *context,
	const nfsService::SetClusterMembersRequest *request,
	nfsProtoUtil::StatusResponse *response)
{
	bool success = false;
	char errmsg[256];
	const int n = request->ipaddrs_size();
	std::vector<char *> ips;

	ips.reserve(n);
	for (int i = 0; i < n; i++)
		ips.push_back(const_cast<char *>(request->ipaddrs(i).c_str()));

	grpc_set_cluster_members(n > 0 ? ips.data() : NULL, (size_t)n, &success,
				 errmsg, sizeof(errmsg));
	response->set_success(success);
	response->set_error_msg(errmsg);
	return grpc::Status::OK;
}

/**
 * @brief Show current Cluster_Members peer list
 */
grpc::Status ClusterMembersService::ShowClusterMembers(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsService::ShowClusterMembersResponse *response)
{
	bool success = false;
	char errmsg[256];
	char **ips = NULL;
	size_t count = 0;
	nfsProtoUtil::StatusResponse *status = response->mutable_status();

	grpc_show_cluster_members(&ips, &count, &success, errmsg,
				  sizeof(errmsg));

	if (success) {
		for (size_t i = 0; i < count; i++)
			response->add_ipaddrs(ips[i]);
	}

	free_cluster_members_list(ips, count);
	status->set_success(success);
	status->set_error_msg(errmsg);
	return grpc::Status::OK;
}

/* Shutdown Ganesha */
grpc::Status nfsAdminService::ShutdownGanesha(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	admin_halt();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Flushing manage gids cache */
grpc::Status nfsAdminService::PurgeGids(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	uid2grp_clear_cache();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Flushing netgroup cache */
grpc::Status nfsAdminService::PurgeNetGroups(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	ng_clear_cache();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Updating open fd limit */
grpc::Status nfsAdminService::InitFdLimit(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	init_fds_limit();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Flushing idmapper cache */
grpc::Status nfsAdminService::PurgeIdmapperCache(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	idmapper_clear_cache();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Flushing idmapper negative cache */
grpc::Status nfsAdminService::PurgeIdmapperNegativeCache(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	idmapper_negative_cache_clear();
	response->set_success(true);
	return grpc::Status::OK;
}

/* Enable  malloc trace */
grpc::Status
nfsAdminService::MallocTrace(grpc::ServerContext *context,
			     const nfsService::MallocTraceRequest *request,
			     nfsProtoUtil::MessageResponse *response)
{
#ifdef LINUX
	LogEvent(COMPONENT_GRPC, "enabling malloc trace to %s.",
		 request->filename().c_str());
	setenv("MALLOC_TRACE", request->filename().c_str(), 1);
	mtrace();
	response->set_success(true);
#else
	response->set_error("malloc trace is not supported");
	response->set_success(false);
#endif
	return grpc::Status::OK;
}

grpc::Status nfsAdminService::MallocUntrace(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::MessageResponse *response)
{
#ifdef LINUX
	LogEvent(COMPONENT_GRPC, "disabling malloc trace.");
	muntrace();
	response->set_success(true);
#else
	response->set_error("malloc untrace is not supported");
	response->set_success(false);
#endif
	return grpc::Status::OK;
}

grpc::Status nfsAdminService::TrimEnableDisable(
	grpc::ServerContext *context,
	const nfsService::TrimEnableDisableRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	if (request->enable()) {
		LogEvent(COMPONENT_MEMLEAKS, "enabling malloc_Trim");
		nfs_param.core_param.malloc_trim = true;
	} else {
		LogEvent(COMPONENT_MEMLEAKS, "disabling malloc_Trim");
		nfs_param.core_param.malloc_trim = false;
	}

	response->set_success(true);
	return grpc::Status::OK;
}

grpc::Status nfsAdminService::TrimCall(grpc::ServerContext *context,
				       const nfsProtoUtil::EmptyRequest *request,
				       nfsProtoUtil::ActionResponse *response)
{
	LogEvent(COMPONENT_MEMLEAKS, "Calling malloc_Trim");
	malloc_trim(0);

	response->set_success(true);
	return grpc::Status::OK;
}

grpc::Status nfsAdminService::TrimStatus(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsService::TrimStatusResponse *response)
{
	char hostname[64 + 1] = { 0 };
	char name[100];
	FILE *fp;

	response->set_enable(true);

	/* log malloc_info() as a side effect! */
	(void)gethostname(hostname, sizeof(hostname));
	snprintf(name, sizeof(name), "/tmp/mallinfo-%s.%d.txt", hostname,
		 getpid());
	fp = fopen(name, "w");
	if (fp != NULL) {
		malloc_info(0, fp);
		fclose(fp);
	}
	if (!nfs_param.core_param.malloc_trim) {
		response->set_enable(false);
	}

	response->set_success(true);
	return grpc::Status::OK;
}

grpc::Status nfsAdminService::ReReadConfig(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	nfsProtoUtil::ActionResponse *response)
{
	response->set_success(reread_config());
	return grpc::Status::OK;
}

typedef bool (*grpc_export_get_io_fn)(uint16_t export_id, struct grpc_iostats *,
				      struct grpc_iostats *, struct timespec *,
				      bool *, char *, size_t);

/**
 * @brief Common handler for per-export I/O stats RPCs
 */
static grpc::Status
handle_export_iostats(const nfsProtoUtil::ExportIdRequest *request,
		      exportService::ExportIoStatsResponse *response,
		      grpc_export_get_io_fn get_io)
{
	struct grpc_iostats read_out {
	}, write_out{};
	struct timespec time_out {
	};
	bool success = false;
	char errmsg[256];

	get_io(request->export_id(), &read_out, &write_out, &time_out, &success,
	       errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	fill_iostats_proto(&read_out, response->mutable_read());
	fill_iostats_proto(&write_out, response->mutable_write());

	return grpc::Status::OK;
}

/**
 * @brief Get per-export NFSv3/NFSv4.0/NFSv4.1/NFSv4.2  read/write counters
 */
grpc::Status
ExportStatsService::GetNFSv3IO(grpc::ServerContext *context,
			       const nfsProtoUtil::ExportIdRequest *request,
			       exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response, grpc_export_get_v3_io);
}

grpc::Status
ExportStatsService::GetNFSv40IO(grpc::ServerContext *context,
				const nfsProtoUtil::ExportIdRequest *request,
				exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response, grpc_export_get_v40_io);
}

grpc::Status
ExportStatsService::GetNFSv41IO(grpc::ServerContext *context,
				const nfsProtoUtil::ExportIdRequest *request,
				exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response, grpc_export_get_v41_io);
}

grpc::Status
ExportStatsService::GetNFSv42IO(grpc::ServerContext *context,
				const nfsProtoUtil::ExportIdRequest *request,
				exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response, grpc_export_get_v42_io);
}

grpc::Status
ExportStatsService::GetNFSMonIO(grpc::ServerContext *context,
				const nfsProtoUtil::ExportIdRequest *request,
				exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response,
				     grpc_export_get_nfsmon_io);
}

typedef bool (*grpc_export_get_layouts_fn)(uint32_t, struct grpc_layouts *,
					   struct timespec *, bool *, char *,
					   size_t);
/**
 * @brief Common handler for export layout statistics RPCs.
 */
static grpc::Status
handle_export_layouts(const nfsProtoUtil::ExportIdRequest *request,
		      exportService::ExportLayoutsResponse *response,
		      grpc_export_get_layouts_fn get_layouts)
{
	struct grpc_layouts layouts {
	};
	struct timespec ts {
	};
	bool success = false;
	char errmsg[256];

	get_layouts(request->export_id(), &layouts, &ts, &success, errmsg,
		    sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	fill_layout_stats_proto(&layouts.getdevinfo,
				response->mutable_getdevinfo());
	fill_layout_stats_proto(&layouts.layout_get,
				response->mutable_layout_get());
	fill_layout_stats_proto(&layouts.layout_commit,
				response->mutable_layout_commit());
	fill_layout_stats_proto(&layouts.layout_return,
				response->mutable_layout_return());
	fill_layout_stats_proto(&layouts.layout_recall,
				response->mutable_layout_recall());

	return grpc::Status::OK;
}

/**
 * @brief Return NFSv4.1 layout statistics for an export.
 */
grpc::Status ExportStatsService::GetNFSv41Layouts(
	grpc::ServerContext *, const nfsProtoUtil::ExportIdRequest *request,
	exportService::ExportLayoutsResponse *response)
{
	return handle_export_layouts(request, response,
				     grpc_export_get_v41_layout_stats);
}

/**
 * @brief Return NFSv4.2 layout statistics for an export.
 */
grpc::Status ExportStatsService::GetNFSv42Layouts(
	grpc::ServerContext *, const nfsProtoUtil::ExportIdRequest *request,
	exportService::ExportLayoutsResponse *response)
{
	return handle_export_layouts(request, response,
				     grpc_export_get_v42_layout_stats);
}

#ifdef _USE_9P
/**
 * @brief Return  9P I/O statistics for an export.
 */
grpc::Status
ExportStatsService::Get9pIO(grpc::ServerContext *context,
			    const nfsProtoUtil::ExportIdRequest *request,
			    exportService::ExportIoStatsResponse *response)
{
	return handle_export_iostats(request, response, grpc_export_get_9p_io);
}

/**
 * @brief Return 9P protocol operation statistics for a specific export.
 */
grpc::Status
ExportStatsService::Get9pOpStats(grpc::ServerContext *context,
				 const nfsProtoUtil::Export9pOpRequest *request,
				 exportService::ExportOpStatsResponse *response)
{
	struct grpc_op_stats op_out {
	};
	struct timespec time_out {
	};
	bool success = false;
	char errmsg[256];

	grpc_export_get_9p_opstats(request->export_id(),
				   request->op_name().c_str(), &op_out,
				   &time_out, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(time_out.tv_sec);
	time->set_tv_nsec(time_out.tv_nsec);

	auto *op = response->mutable_op_stats();
	op->set_total_ops(op_out.total_ops);
	op->set_errors(op_out.errors);

	return grpc::Status::OK;
}
#endif

/**
 * @brief Retrieves the total NFS operation statistics from the server.
 */
grpc::Status
ExportStatsService::GetTotalOPS(grpc::ServerContext *context,
				const nfsProtoUtil::ExportIdRequest *request,
				exportService::GetTotalOPSResponse *response)
{
	struct grpc_total_ops ops = {};
	struct timespec ts = {};
	bool success = false;
	char errmsg[128];

	grpc_export_get_total_ops(request->export_id(), &ops, &ts, &success,
				  errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	auto *total = response->mutable_total_ops();

#ifdef _USE_NFS3
	total->set_nfsv3(ops.nfsv3);
#endif

	total->set_nfsv40(ops.nfsv40);
	total->set_nfsv41(ops.nfsv41);
	total->set_nfsv42(ops.nfsv42);

	return grpc::Status::OK;
}

/**
 * @brief Retrieves the aggregated global NFS operation statistics.
 */
grpc::Status ExportStatsService::GetGlobalOPS(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetGlobalOPSResponse *response)
{
	grpc_global_total_ops ops = {};
	struct timespec ts = {};
	bool success = false;
	char errmsg[128];

	grpc_get_global_total_ops(&ops, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	auto *total = response->mutable_total_ops();
	auto *nfs = total->mutable_nfs();

#ifdef _USE_NFS3
	nfs->set_nfsv3(ops.nfs.nfsv3);
#endif

	nfs->set_nfsv40(ops.nfs.nfsv40);
	nfs->set_nfsv41(ops.nfs.nfsv41);
	nfs->set_nfsv42(ops.nfs.nfsv42);

#ifdef _USE_NLM
	total->set_nlm4(ops.nlm4);
#endif

#ifdef _USE_NFS3
	total->set_mntv1(ops.mntv1);
	total->set_mntv3(ops.mntv3);
#endif

#ifdef _USE_RQUOTA
	total->set_rquota(ops.rquota);
#endif

	return grpc::Status::OK;
}

/**
 * @brief Retrieves server-wide NFS I/O statistics.
 */
grpc::Status ExportStatsService::GetNFSIO(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetNFSIOResponse *response)
{
	grpc_export_io_list list{};
	struct timespec ts {
	};
	bool success = false;
	char errmsg[128];

	grpc_get_all_export_iostats(&list, &ts, &success, errmsg,
				    sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	for (size_t i = 0; i < list.count; ++i) {
		auto *entry = response->add_entries();

		entry->set_export_id(list.entries[i].export_id);
		entry->set_version(list.entries[i].version);

		fill_iostats_proto(&list.entries[i].read,
				   entry->mutable_read());

		fill_iostats_proto(&list.entries[i].write,
				   entry->mutable_write());
	}

	return grpc::Status::OK;
}

/**
 * @brief Populate gRPC IDMapper user cache entries.
 */
grpc::Status CacheMgrService::ShowIdMapperUsers(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowIdMapperUsersResponse *response)
{
	struct timespec ts;
	struct grpc_idmapper_user users[1024];
	struct grpc_idmapper_user_list list;

	now(&ts);

	response->set_timestamp_sec(ts.tv_sec);
	response->set_timestamp_nsec(ts.tv_nsec);

	list.entries = users;
	list.count = 0;

	grpc_fill_idmapper_users(&list);

	for (uint32_t i = 0; i < list.count; i++) {
		auto *entry = response->add_entries();

		entry->set_name(list.entries[i].name);
		entry->set_uid(list.entries[i].uid);
		entry->set_gid_set(list.entries[i].gid_set);
		entry->set_gid(list.entries[i].gid);
	}

	return grpc::Status::OK;
}

/**
 * @brief Populate gRPC IDMapper group cache entries.
 */
grpc::Status CacheMgrService::ShowIdMapperGroups(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowIdMapperGroupsResponse *response)
{
	struct timespec ts;
	struct grpc_idmapper_group groups[1024];
	struct grpc_idmapper_group_list list;

	now(&ts);

	response->set_timestamp_sec(ts.tv_sec);
	response->set_timestamp_nsec(ts.tv_nsec);

	list.entries = groups;
	list.count = 0;

	grpc_fill_idmapper_groups(&list);

	for (uint32_t i = 0; i < list.count; i++) {
		auto *entry = response->add_entries();

		entry->set_name(list.entries[i].name);
		entry->set_gid(list.entries[i].gid);
	}

	return grpc::Status::OK;
}

typedef void (*grpc_negative_fill_fn)(struct grpc_negative_cache_list *list);

template <typename ResponseType>
static grpc::Status FillNegativeCache(grpc_negative_fill_fn fill_fn,
				      ResponseType *response)
{
	struct timespec ts;
	struct grpc_negative_cache_entry entries[1024];
	struct grpc_negative_cache_list list;

	now(&ts);

	response->set_timestamp_sec(ts.tv_sec);
	response->set_timestamp_nsec(ts.tv_nsec);

	list.entries = entries;
	list.count = 0;

	fill_fn(&list);

	for (uint32_t i = 0; i < list.count; i++) {
		auto *entry = response->add_entries();

		entry->set_name(list.entries[i].name);
		entry->set_epoch(list.entries[i].epoch);
	}

	return grpc::Status::OK;
}

/**
 * @brief Populate gRPC responses for the IDMapper negative
 * user, group, and UID caches.
 */
grpc::Status CacheMgrService::ShowNegativeUsers(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowNegativeUsersResponse *response)
{
	return FillNegativeCache(grpc_fill_negative_users, response);
}

grpc::Status CacheMgrService::ShowNegativeGroups(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowNegativeGroupsResponse *response)
{
	return FillNegativeCache(grpc_fill_negative_groups, response);
}

grpc::Status CacheMgrService::ShowNegativeUIDs(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowNegativeUIDsResponse *response)
{
	return FillNegativeCache(grpc_fill_negative_uids, response);
}

/**
 * @brief Return the gRPC response for uid2grp cache entries.
 */
grpc::Status CacheMgrService::ShowUid2Grp(
	grpc::ServerContext *, const nfsProtoUtil::EmptyRequest *,
	cacheMgr::ShowUid2GrpResponse *response)
{
	struct timespec ts;
	struct grpc_uid2grp_entry entries[1024];
	struct grpc_uid2grp_list list;

	now(&ts);

	response->set_timestamp_sec(ts.tv_sec);
	response->set_timestamp_nsec(ts.tv_nsec);

	list.entries = entries;
	list.count = 0;

	grpc_fill_uid2grp(&list);

	for (uint32_t i = 0; i < list.count; i++) {
		auto *entry = response->add_entries();

		entry->set_name(list.entries[i].name);
		entry->set_uid(list.entries[i].uid);
	}

	return grpc::Status::OK;
}

grpc::Status
ExportService::DisplayExport(grpc::ServerContext *context,
			     const nfsProtoUtil::ExportIdRequest *request,
			     exportService::DisplayExportResponse *response)
{
	char *errormsg;
	struct gsh_export *export_obj =
		lookup_export_by_id(request->export_id(), &errormsg);
	if (!export_obj) {
		response->set_success(false);
		response->set_error_message(errormsg);
		return grpc::Status::OK;
	}

	struct tmp_export_paths tmp;
	tmp_get_exp_paths(&tmp, export_obj);

	response->set_export_id(export_obj->export_id);

	const char *full = TMP_FULLPATH(&tmp);
	if (full && full[0] != '\0') {
		response->set_full_path(full);
	}

	const char *pseudo = TMP_PSEUDOPATH(&tmp);
	if (pseudo && pseudo[0] != '\0')
		response->set_pseudo_path(pseudo);

	struct glist_head *glist;

	PTHREAD_RWLOCK_rdlock(&export_obj->exp_lock);

	glist_for_each(glist, &export_obj->clients) {
		struct base_client_entry *client;
		struct exportlist_client_entry *expclient;

		client = glist_entry(glist, struct base_client_entry, cle_list);
		expclient = container_of(client, struct exportlist_client_entry,
					 client_entry);

		client = glist_entry(glist, struct base_client_entry, cle_list);

		expclient = container_of(client, struct exportlist_client_entry,
					 client_entry);
		auto *out = response->add_clients();

		out->set_client_name(client->str ? client->str : "");

		if (client->type == NETWORK_CLIENT && client->cidr != NULL) {
			unsigned char addr[16] = { 0 };
			unsigned char mask[16] = { 0 };

			out->set_client_type("NETWORK_CLIENT");

			out->set_cidr_version(cidr_version(client->cidr));

			cidr_ipaddr_to_chars(client->cidr, addr);
			out->set_cidr_addr(std::string(
				reinterpret_cast<char *>(addr), sizeof(addr)));

			cidr_mask_to_chars(client->cidr, mask);
			out->set_cidr_mask(std::string(
				reinterpret_cast<char *>(mask), sizeof(mask)));

			out->set_cidr_proto(cidr_proto(client->cidr));
		} else {
			switch (client->type) {
			case NETGROUP_CLIENT:
				out->set_client_type("NETGROUP_CLIENT");
				break;
			case GSSPRINCIPAL_CLIENT:
				out->set_client_type("GSSPRINCIPAL_CLIENT");
				break;
			case MATCH_ANY_CLIENT:
				out->set_client_type("MATCH_ANY_CLIENT");
				break;
			case WILDCARDHOST_CLIENT:
				out->set_client_type("WILDCARDHOST_CLIENT");
				break;
			default:
				out->set_client_type("UNKNOWN_CLIENT");
				break;
			}

			/* Match the D-Bus behavior for non-network clients. */
			out->set_cidr_version(0);
			out->set_cidr_addr("");
			out->set_cidr_mask("");
			out->set_cidr_proto(0);
		}

		out->set_anonymous_uid(expclient->client_perms.anonymous_uid);
		out->set_anonymous_gid(expclient->client_perms.anonymous_gid);
		out->set_expire_time_attr(
			expclient->client_perms.expire_time_attr);

		char client_perm_buf[1024] = "\0";
		struct display_buffer client_dspbuf = { sizeof(client_perm_buf),
							client_perm_buf,
							client_perm_buf };

		StrExportOptions(&client_dspbuf, &expclient->client_perms);

		out->set_client_permissions(client_perm_buf);

		char export_perm_buf[1024] = "\0";
		struct display_buffer export_dspbuf = { sizeof(export_perm_buf),
							export_perm_buf,
							export_perm_buf };

		StrExportOptions(&export_dspbuf, &export_obj->export_perms);

		response->set_export_permissions(export_perm_buf);
	}

	PTHREAD_RWLOCK_unlock(&export_obj->exp_lock);

	tmp_put_exp_paths(&tmp);
	put_gsh_export(export_obj);

	response->set_success(true);
	return grpc::Status::OK;
}

/*
 * Populate per-export protocol activity summary and total operation
 * count, mirroring the information returned by the DBus ShowExports API.
 */
grpc::Status ExportService::ShowExports(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::ShowExportsResponse *response)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	response->set_timestamp_sec(ts.tv_sec);
	response->set_timestamp_nsec(ts.tv_nsec);
	pthread_rwlock_t *lock = get_export_id_lock();
	struct glist_head *exportlist_head = get_exportlist_head();
	PTHREAD_RWLOCK_rdlock(lock);
	struct glist_head *node;
	glist_for_each(node, exportlist_head) {
		struct gsh_export *export_obj =
			glist_entry(node, struct gsh_export, exp_list);

		struct tmp_export_paths tmp = { nullptr, nullptr };
		tmp_get_exp_paths(&tmp, export_obj);
		const char *full = TMP_FULLPATH(&tmp);
		const char *pseudo = TMP_PSEUDOPATH(&tmp);
		auto *out = response->add_exports();
		out->set_export_id(
			static_cast<uint32_t>(export_obj->export_id));
		if (full != nullptr)
			out->set_full_path(full);

		if (pseudo != nullptr)
			out->set_pseudo_path(pseudo);

		if (export_obj->FS_tag != nullptr)
			out->set_fs_tag(export_obj->FS_tag);

		tmp_put_exp_paths(&tmp);

		/*
                 * Fill protocol summary
                 */
		struct grpc_protocol_activity protocols[MAX_PROTOCOLS];
		uint32_t protocol_count = 0;
		uint64_t total_ops = 0;

		if (server_grpc_fill_export_stats_summary(export_obj, protocols,
							  &protocol_count,
							  &total_ops)) {
			out->set_total_ops(total_ops);

			for (uint32_t i = 0; i < protocol_count; i++) {
				auto *pa = out->add_protocols();
				pa->set_name(protocols[i].name);
				pa->set_active(protocols[i].active);
			}

			auto *ts = out->mutable_last_update();
			ts->set_tv_sec(export_obj->last_update.tv_sec);
			ts->set_tv_nsec(export_obj->last_update.tv_nsec);
		}
	}

	PTHREAD_RWLOCK_unlock(lock);
	response->set_success(true);
	return grpc::Status::OK;
}

/**
 * @brief gRPC API to report global per-operation statistics for
 * all supported protocols.
 * Returns operation names and their invocation counts.
 */
grpc::Status ExportStatsService::GetFastOPS(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetFastOPSResponse *response)
{
	grpc_fast_ops stats{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_get_fast_ops(&stats, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	for (uint32_t i = 0; i < stats.num_protocols; i++) {
		auto *proto = response->add_protocols();
		proto->set_protocol(stats.protocols[i].protocol);

		for (uint32_t j = 0; j < stats.protocols[i].num_ops; j++) {
			auto *op = proto->add_operations();
			op->set_operation(stats.protocols[i].ops[j].op_name);
			op->set_count(stats.protocols[i].ops[j].count);
		}
	}

	return grpc::Status::OK;
}

/**
 * @brief gRPC API to get NFSv3 Detailed stats
 */
grpc::Status ExportStatsService::GetFULLV3Stats(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetFULLV3StatsResponse *response)
{
	grpc_full_stats stats{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_get_v3_full_stats(&stats, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	for (uint32_t i = 0; i < stats.num_ops; i++) {
		auto *op = response->add_stats();

		op->set_operation(stats.ops[i].op_name);
		op->set_total(stats.ops[i].total);
		op->set_errors(stats.ops[i].errors);
		op->set_duplicates(stats.ops[i].dups);
		op->set_avg_latency(stats.ops[i].avg_latency);
		op->set_min_latency(stats.ops[i].min_latency);
		op->set_max_latency(stats.ops[i].max_latency);
	}

	response->set_message(stats.message);

	return grpc::Status::OK;
}

/**
 * @brief gRPC API to get NFSv4 Detailed stats
 */
grpc::Status ExportStatsService::GetFULLV4Stats(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetFULLV4StatsResponse *response)
{
	grpc_full_stats stats{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_get_v4_full_stats(&stats, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	for (uint32_t i = 0; i < stats.num_ops; i++) {
		auto *op = response->add_stats();

		op->set_operation(stats.ops[i].op_name);
		op->set_total(stats.ops[i].total);
		op->set_errors(stats.ops[i].errors);
		op->set_avg_latency(stats.ops[i].avg_latency);
		op->set_min_latency(stats.ops[i].min_latency);
		op->set_max_latency(stats.ops[i].max_latency);
	}

	response->set_message(stats.message);

	return grpc::Status::OK;
}

/**
 * @brief Reset all server statistics.
 */
grpc::Status ExportStatsService::ResetStats(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::ResetStatsResponse *response)
{
	struct timespec ts = {};
	bool success = false;
	char errmsg[128];

	grpc_reset_stats(&ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	return grpc::Status::OK;
}

/**
 * @brief gRPC API to enable statistics counting
 */
grpc::Status ExportStatsService::EnableStats(
	grpc::ServerContext *context,
	const exportService::EnableStatsRequest *request,
	exportService::EnableStatsResponse *response)
{
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_enable_stats(static_cast<grpc_statistics_type>(
				  request->stat_type()),
			  &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	return grpc::Status::OK;
}

/**
 * @brief gRPC API to disable statistics counting
 */
grpc::Status ExportStatsService::DisableStats(
	grpc::ServerContext *context,
	const exportService::DisableStatsRequest *request,
	exportService::DisableStatsResponse *response)
{
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_disable_stats(static_cast<grpc_statistics_type>(
				   request->stat_type()),
			   &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	return grpc::Status::OK;
}

static void FillStatsStatus(const grpc_stats_status &src,
			    exportService::StatsStatus *dst)
{
	dst->set_enabled(src.enabled);

	auto *time = dst->mutable_time();
	time->set_tv_sec(src.timestamp.tv_sec);
	time->set_tv_nsec(src.timestamp.tv_nsec);
}

/**
 * gRPC API to know current status of stats counting
 */
grpc::Status ExportStatsService::StatusStats(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::StatusStatsResponse *response)
{
	grpc_all_stats_status stats{};
	bool success{};
	char errmsg[128];

	grpc_status_stats(&stats, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	FillStatsStatus(stats.nfs, response->mutable_nfs());
	FillStatsStatus(stats.fsal, response->mutable_fsal());

#ifdef _USE_NFS3
	FillStatsStatus(stats.v3_full, response->mutable_v3_full());
#endif

	FillStatsStatus(stats.v4_full, response->mutable_v4_full());

	FillStatsStatus(stats.auth, response->mutable_auth());

	FillStatsStatus(stats.client_all_ops,
			response->mutable_client_all_ops());

	return grpc::Status::OK;
}

static void FillAuthStats(const grpc_auth_stats &src,
			  exportService::AuthStats *dst)
{
	dst->set_total(src.total);
	dst->set_avg_latency(src.avg_latency);
	dst->set_max_latency(src.max_latency);
	dst->set_min_latency(src.min_latency);
}

/**
 * gRPC API to collect Auth stats for group cache and winbind
 */
grpc::Status ExportStatsService::GetAuthStats(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::GetAuthStatsResponse *response)
{
	grpc_all_auth_stats stats{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_get_auth_stats(&stats, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	FillAuthStats(stats.group_cache, response->mutable_group_cache());

	FillAuthStats(stats.winbind, response->mutable_winbind());

	FillAuthStats(stats.dns, response->mutable_dns());

	return grpc::Status::OK;
}

/**
 * gRPC API to show cache.
 */
grpc::Status ExportStatsService::ShowMDCache(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::ShowMDCacheResponse *response)
{
	grpc_mdcache_stats cache{};
	grpc_lru_utilization lru{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_show_mdcache_stats(&cache, &lru, &ts, &success, errmsg,
				sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	auto *cache_pb = response->mutable_cache();
	cache_pb->set_cache_requests(cache.cache_requests);
	cache_pb->set_cache_hits(cache.cache_hits);
	cache_pb->set_cache_misses(cache.cache_misses);
	cache_pb->set_cache_conflicts(cache.cache_conflicts);
	cache_pb->set_cache_adds(cache.cache_adds);
	cache_pb->set_cache_mapping(cache.cache_mapping);

	auto *lru_pb = response->mutable_lru();
	lru_pb->set_entries_used(lru.entries_used);
	lru_pb->set_chunks_used(lru.chunks_used);

	return grpc::Status::OK;
}

/**
 * Returns a summary of current file descriptor usage and configured
 * FD watermarks.
 */
grpc::Status ExportStatsService::ShowFDUsage(
	grpc::ServerContext *context, const nfsProtoUtil::EmptyRequest *request,
	exportService::ShowFDUsageResponse *response)
{
	grpc_fd_usage_summary summary{};
	struct timespec ts {
	};
	bool success{};
	char errmsg[128];

	grpc_show_fd_usage(&summary, &ts, &success, errmsg, sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	auto *time = response->mutable_time();
	time->set_tv_sec(ts.tv_sec);
	time->set_tv_nsec(ts.tv_nsec);

	auto *pb = response->mutable_summary();

	pb->set_fd_limit(summary.fd_limit);
	pb->set_fd_lowat(summary.fd_lowat);
	pb->set_fd_hiwat(summary.fd_hiwat);
	pb->set_fd_hard_limit(summary.fd_hard_limit);

	pb->set_fd_state(static_cast<exportService::FdState>(summary.fd_state));

	pb->set_global_fds(summary.global_fds);
	pb->set_state_fds(summary.state_fds);
	pb->set_v4_open_states(summary.v4_open_states);

	/* LRU utilization */
	pb->set_open_fds(summary.open_fds);

	return grpc::Status::OK;
}

/**
 * @brief gRPC API to retrieve detailed statistics for
 * the specified export.
 */
grpc::Status ExportStatsService::GetExportDetails(
	grpc::ServerContext *context,
	const nfsProtoUtil::ExportIdRequest *request,
	exportService::GetExportDetailsResponse *response)
{
	struct grpc_client_io_ops stats {
	};
	bool success = false;
	char errmsg[256];

	grpc_get_export_details(request->export_id(), &stats, &success, errmsg,
				sizeof(errmsg));

	auto *status = response->mutable_status();
	status->set_success(success);
	status->set_error_msg(errmsg);

	if (!success)
		return grpc::Status::OK;

	auto *time = response->mutable_time();
	time->set_tv_sec(stats.time.tv_sec);
	time->set_tv_nsec(stats.time.tv_nsec);

#ifdef _USE_NFS3
	fill_version_ce_stats_proto(&stats.v3, response->mutable_nfsv3());
#endif

	fill_version_ce_stats_proto(&stats.v40, response->mutable_nfsv40());

	fill_version_ce_stats_proto(&stats.v41, response->mutable_nfsv41());

	fill_version_ce_stats_proto(&stats.v42, response->mutable_nfsv42());

	return grpc::Status::OK;
}

grpc::Status
ExportService::AddExport(grpc::ServerContext *context,
			 const exportService::ExportRequest *request,
			 exportService::ExportResponse *response)
{
	const std::string &file_path = request->file_path();
	const std::string &export_expr = request->export_expression();
	int rc = 0, exp_cnt = 0;
	bool status = true;
	config_file_t config_struct = NULL;
	struct config_node_list *config_list = NULL, *lp, *lp_next;
	struct config_error_type err_type;
	struct error_detail conf_errs = { NULL, 0, NULL };
	struct stat st;
	char *file_path_mut = nullptr;
	char *export_expr_mut = nullptr;
	response->set_success(false);
	// Lock export admin
	if (EXPORT_ADMIN_TRYLOCK() != 0) {
		response->set_message(
			"Another export admin operation is in progress, try again later");
		return grpc::Status::OK;
	}
	// Validate path is regular file
	rc = stat(file_path.c_str(), &st);
	if (rc < 0 || (st.st_mode & S_IFMT) != S_IFREG) {
		response->set_message("Invalid config file path: " + file_path);
		status = false;
		goto out_unlock;
	}
	// Initialize error type for parser
	if (!init_error_type(&err_type)) {
		response->set_message("Failed to initialize error type");
		goto out_unlock;
	}
	// Parse config file
	file_path_mut = strdup(file_path.c_str());
	config_struct = config_ParseFile(file_path_mut, &err_type);
	free(file_path_mut);
	// Check for parse errors
	if (!cur_exp_config_error_is_harmless(&err_type)) {
		std::string err_detail = err_type_str(&err_type);
		response->set_message("Error parsing config file: " +
				      err_detail);
		status = false;
		goto out_unlock;
	}
	// Find export nodes matching expression
	export_expr_mut = strdup(export_expr.c_str());
	rc = find_config_nodes(config_struct, export_expr_mut, &config_list,
			       &err_type);
	free(export_expr_mut);
	if (rc != 0) {
		response->set_message("Failed to find export nodes: " +
				      std::string(strerror(rc)));
		status = false;
		goto out_unlock;
	}
	// Load exports and count success
	for (lp = config_list; lp != NULL; lp = lp_next) {
		lp_next = lp->next;
		if (status) {
			rc = load_config_from_node(lp->tree_node,
						   &add_export_param, NULL,
						   false, &err_type);
			if (rc == 0 ||
			    cur_exp_config_error_is_harmless(&err_type)) {
				exp_cnt++;
			} else if (!err_type.exists) {
				status = false;
			}
		}
		gsh_free(lp, MEM_COMP_CONFIG);
	}
	// Report results
	if (status) {
		std::string msg = std::to_string(exp_cnt) + " exports added";
		if (exp_cnt > 0 && conf_errs.buf) {
			msg += ". Errors found:\n" + std::string(conf_errs.buf);
		}
		response->set_success(true);
		response->set_message(msg);
	} else {
		std::string err_detail = err_type_str(&err_type);
		response->set_success(false);
		response->set_message(
			std::to_string(exp_cnt) +
			" exports added but with errors: " + err_detail);
	}
out_unlock:
	if (conf_errs.fp)
		fclose(conf_errs.fp);
	if (conf_errs.buf)
		gsh_free(conf_errs.buf, MEM_COMP_CONFIG);
	config_Free(config_struct);

	EXPORT_ADMIN_UNLOCK();

	return grpc::Status::OK;
}

grpc::Status
ExportService::RemoveExport(grpc::ServerContext *context,
			    const nfsProtoUtil::ExportIdRequest *request,
			    exportService::ExportResponse *response)
{
	// Variable declarations
	struct gsh_export *export_obj = NULL;
	char *errormsg = NULL;

	// Lock for admin operation
	if (EXPORT_ADMIN_TRYLOCK() != 0) {
		response->set_success(false);
		response->set_message(
			"another export admin operation is in progress");
		put_gsh_export(export_obj);
		return grpc::Status::OK;
	}

	// Lookup export by ID
	export_obj = lookup_export_by_id(request->export_id(), &errormsg);
	if (export_obj == NULL) {
		response->set_success(false);
		response->set_message("lookup_export failed: " +
				      std::string(errormsg));
		return grpc::Status::OK;
	}

	// Check for invalid ID 0 or pseudo root
	if (export_obj->export_id == 0 ||
	    strcmp(export_obj->pseudopath->gr_val, "/") == 0) {
		put_gsh_export(export_obj);
		response->set_success(false);
		response->set_message(
			"Cannot remove the pseudo root export or export with id 0");
		return grpc::Status::OK;
	}

	//TODO: yet to reconsider checking mounted subexports
	// Check for mounted subexports
	PTHREAD_RWLOCK_rdlock(&export_obj->exp_lock);
	bool is_empty = glist_empty(&export_obj->mounted_exports_list);
	PTHREAD_RWLOCK_unlock(&export_obj->exp_lock);
	if (!is_empty) {
		put_gsh_export(export_obj);
		response->set_success(false);
		response->set_message("Cannot remove export with submounts");
		EXPORT_ADMIN_UNLOCK();
		return grpc::Status::OK;
	}

	// Prepare operation context and release
	struct req_op_context op_context;
	init_op_context_simple(&op_context, export_obj,
			       export_obj->fsal_export);

	uint64_t generation = export_obj->config_gen;

	/* Mark this export as older than the current configuration by
	 * setting config_gen to 0. During synchronization, the prune
	 * logic treats it as no longer present in the latest config,
	 * therefore export is removed.
	 */
	export_obj->config_gen = 0;
	export_obj->update_prune_unmount = true;

	synchronize_exports(generation);

	release_op_context();

	// Unlock admin lock
	EXPORT_ADMIN_UNLOCK();
	response->set_success(true);
	response->set_message("Export removed successfully");
	return grpc::Status::OK;
}

grpc::Status
ExportService::UpdateExport(grpc::ServerContext *context,
			    const exportService::ExportRequest *request,
			    exportService::ExportResponse *response)
{
	int rc = 0, exp_cnt = 0;
	bool status = true;
	const std::string &file_path = request->file_path();
	const std::string &export_expr = request->export_expression();
	config_file_t config_struct = NULL;
	struct config_node_list *config_list = NULL, *lp = NULL,
				*lp_next = NULL;
	struct config_error_type err_type;
	char *err_detail = NULL;
	struct error_detail conf_errs = { NULL, 0, NULL };
	response->set_success(false);
	response->set_message("");
	if (file_path.empty()) {
		response->set_message("Pathname is empty");
		return grpc::Status::OK;
	}
	if (export_expr.empty()) {
		response->set_message("export expression is empty");
		return grpc::Status::OK;
	}
	if (EXPORT_ADMIN_TRYLOCK() != 0) {
		response->set_message(
			"Another export admin operation is in progress, try again later");
		return grpc::Status::OK;
	}
	if (!init_error_type(&err_type)) {
		response->set_message(
			"failed to initialize parser error state");
		goto out;
	}
	config_struct = config_ParseFile(const_cast<char *>(file_path.c_str()),
					 &err_type);
	if (!cur_exp_config_error_is_harmless(&err_type)) {
		err_detail = err_type_str(&err_type);
		(void)report_config_errors(&err_type, &conf_errs,
					   config_errs_to_log);
		response->set_message(
			"Error while parsing " + file_path + " because of " +
			std::string(err_detail ? err_detail : "unknown") +
			" errors. Details:\n" +
			std::string(conf_errs.buf ? conf_errs.buf : ""));
		status = false;
		goto out;
	}
	rc = find_config_nodes(config_struct,
			       const_cast<char *>(export_expr.c_str()),
			       &config_list, &err_type);
	if (rc != 0) {
		(void)report_config_errors(&err_type, &conf_errs,
					   config_errs_to_log);
		response->set_message("Error finding exports: " + export_expr +
				      " because " + std::string(strerror(rc)));
		status = false;
		goto out;
	}
	for (lp = config_list; lp != NULL; lp = lp_next) {
		lp_next = lp->next;
		if (status) {
			rc = load_config_from_node(lp->tree_node,
						   &update_export_param, NULL,
						   false, &err_type);
			if (rc == 0 ||
			    cur_exp_config_error_is_harmless(&err_type))
				exp_cnt++;
			else if (!err_type.exists)
				status = false;
		}
		gsh_free(lp, MEM_COMP_CONFIG);
	}

	if (status) {
		uint64_t generation = get_config_generation(config_struct);
		/* Mark all existing exports as updated to the current
		 * configuration generation so synchronization preserves
		 * them and only removes exports that were explicitly
		 * marked for removal.
		 */
		update_all_export_generations(generation);
		synchronize_exports(generation);
	}

	(void)report_config_errors(&err_type, &conf_errs, config_errs_to_log);
	if (status) {
		if (exp_cnt > 0) {
			std::string msg =
				std::to_string(exp_cnt) + " exports updated";
			if (conf_errs.buf != NULL &&
			    strlen(conf_errs.buf) > 0) {
				msg += ". Errors found:\n";
				msg += conf_errs.buf;
			}
			response->set_success(true);
			response->set_message(msg);
		} else if (err_type.exists) {
			response->set_message("Selected entries in " +
					      file_path + " already active!!!");
			status = false;
		} else {
			response->set_message(
				"No new export entries found in " + file_path);
			status = false;
		}
	} else {
		err_detail = err_detail ? err_detail : err_type_str(&err_type);
		response->set_message(
			std::to_string(exp_cnt) + " export entries in " +
			file_path + " updated because " +
			std::string(err_detail ? err_detail : "unknown") +
			" errors. Details:\n" +
			std::string(conf_errs.buf ? conf_errs.buf : ""));
	}
out:
	if (conf_errs.fp != NULL)
		fclose(conf_errs.fp);
	if (conf_errs.buf != NULL)
		gsh_free(conf_errs.buf, MEM_COMP_CONFIG);
	if (err_detail != NULL)
		gsh_free(err_detail, MEM_COMP_CONFIG);
	config_Free(config_struct);
	EXPORT_ADMIN_UNLOCK();
	return grpc::Status::OK;
}
