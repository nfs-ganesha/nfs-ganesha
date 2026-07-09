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
#include "nfsService.h"
#include "server_stats_grpc.h"

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
