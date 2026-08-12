/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Copyright (C) 2025, IBM
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

#ifndef NFSSERVICE_H
#define NFSSERVICE_H

#include <pthread.h>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <grpcpp/grpcpp.h>
#include "nfsService.pb.h"
#include "nfsService.grpc.pb.h"
#include "exportService.pb.h"
#include "exportService.grpc.pb.h"
#include "nfsProtoUtil.pb.h"
#include "nfsProtoUtil.grpc.pb.h"
#include "cltmgrService.pb.h"
#include "cltmgrService.grpc.pb.h"
#include "exportService.pb.h"
#include "exportService.grpc.pb.h"
#include "cacheMgrService.pb.h"
#include "cacheMgrService.grpc.pb.h"
#include "nfsServiceUtil.h"
#include "nfs_core.h"
#include "sal_functions.h"
#include "config.h"
#include "gsh_config.h"

class GetClientIdService final : public nfsService::GetClientId::Service {
    public:
	grpc::Status
	GetClientIds(grpc::ServerContext *context,
		     const nfsProtoUtil::EmptyRequest *request,
		     nfsService::GetClientIdsResponse *response) override;
};

class GetNfsGraceService final : public nfsService::GetNfsGrace::Service {
    public:
	grpc::Status
	GetGracePeriod(grpc::ServerContext *context,
		       const nfsProtoUtil::EmptyRequest *request,
		       nfsService::GetNfsGraceResponse *response) override;
};

class StartNfsGraceService final : public nfsService::StartNfsGrace::Service {
    public:
	grpc::Status
	StartGraceWithEvent(grpc::ServerContext *context,
			    const nfsService::GraceWithEvent *request,
			    nfsService::GraceStatus *response) override;
};

class GetSessionIdService final : public nfsService::GetSessionId::Service {
    public:
	grpc::Status
	GetSessionIds(grpc::ServerContext *context,
		      const nfsProtoUtil::EmptyRequest *request,
		      nfsService::GetSessionIdsResponse *response) override;
};

/**
 * @brief gRPC equivalent of org.ganesha.nfsd.cbsim.fake_recall
 *
 * Forces a demonstration CB_RECALL on the callback channel for the
 * given NFSv4 client id.
 */
class FakeRecallService final : public nfsService::FakeRecall::Service {
    public:
	grpc::Status
	FakeRecall(grpc::ServerContext *context,
		   const nfsService::FakeRecallRequest *request,
		   nfsProtoUtil::StatusResponse *response) override;
};

class ClientStatsService final : public cltmgrService::ClientStats::Service {
    public:
	grpc::Status
	GetNFSv3IO(grpc::ServerContext *context,
		   const nfsProtoUtil::ClientIpRequest *request,
		   cltmgrService::ClientIoStatsResponse *response) override;

	grpc::Status
	GetNFSv40IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ClientIpRequest *request,
		    cltmgrService::ClientIoStatsResponse *response) override;

	grpc::Status
	GetNFSv41IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ClientIpRequest *request,
		    cltmgrService::ClientIoStatsResponse *response) override;

	grpc::Status
	GetNFSv42IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ClientIpRequest *request,
		    cltmgrService::ClientIoStatsResponse *response) override;

	grpc::Status GetNFSv41Layouts(
		grpc::ServerContext *context,
		const nfsProtoUtil::ClientIpRequest *request,
		cltmgrService::ClientLayoutsResponse *response) override;

	grpc::Status GetNFSv42Layouts(
		grpc::ServerContext *context,
		const nfsProtoUtil::ClientIpRequest *request,
		cltmgrService::ClientLayoutsResponse *response) override;

	grpc::Status GetDelegations(
		grpc::ServerContext *context,
		const nfsProtoUtil::ClientIpRequest *request,
		cltmgrService::ClientDelegationsResponse *response) override;

	grpc::Status
	GetClientIOops(grpc::ServerContext *context,
		       const nfsProtoUtil::ClientIpRequest *request,
		       cltmgrService::ClientIoOpsResponse *response) override;

	grpc::Status
	GetClientAllops(grpc::ServerContext *context,
			const nfsProtoUtil::ClientIpRequest *request,
			cltmgrService::ClientAllOpsResponse *response) override;

	grpc::Status
	Get9pIO(grpc::ServerContext *context,
		const nfsProtoUtil::ClientIpRequest *request,
		cltmgrService::ClientIoStatsResponse *response) override;

	grpc::Status
	Get9pTrans(grpc::ServerContext *context,
		   const nfsProtoUtil::ClientIpRequest *request,
		   cltmgrService::ClientTransportResponse *response) override;

	grpc::Status
	Get9pOpStats(grpc::ServerContext *context,
		     const nfsProtoUtil::Client9pOpRequest *request,
		     cltmgrService::ClientOpStatsResponse *response) override;
};

/**
 * @brief gRPC service class for org.ganesha.nfsd.clientmgr
 *
 * Provides client management operations: AddClient, RemoveClient,
 * ShowClients, and DisconnectNfsv41Client. Mirrors the D-Bus interface
 * on /org/ganesha/nfsd/ClientMgr.
 */
class ClientMgrService final : public cltmgrService::ClientMgr::Service {
    public:
	grpc::Status AddClient(grpc::ServerContext *context,
			       const nfsProtoUtil::ClientIpRequest *request,
			       nfsProtoUtil::StatusResponse *response) override;

	grpc::Status
	RemoveClient(grpc::ServerContext *context,
		     const nfsProtoUtil::ClientIpRequest *request,
		     nfsProtoUtil::StatusResponse *response) override;

	grpc::Status
	ShowClients(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    cltmgrService::ShowClientsResponse *response) override;

	grpc::Status DisconnectNfsv41Client(
		grpc::ServerContext *context,
		const nfsProtoUtil::ClientIpRequest *request,
		cltmgrService::DisconnectClientResponse *response) override;
};

class nfsAdminService final : public nfsService::nfsAdmin::Service {
    public:
	grpc::Status
	ShutdownGanesha(grpc::ServerContext *context,
			const nfsProtoUtil::EmptyRequest *request,
			nfsProtoUtil::ActionResponse *response) override;

	grpc::Status PurgeGids(grpc::ServerContext *context,
			       const nfsProtoUtil::EmptyRequest *request,
			       nfsProtoUtil::ActionResponse *response) override;

	grpc::Status
	PurgeNetGroups(grpc::ServerContext *context,
		       const nfsProtoUtil::EmptyRequest *request,
		       nfsProtoUtil::ActionResponse *response) override;

	grpc::Status
	InitFdLimit(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    nfsProtoUtil::ActionResponse *response) override;

	grpc::Status
	PurgeIdmapperCache(grpc::ServerContext *context,
			   const nfsProtoUtil::EmptyRequest *request,
			   nfsProtoUtil::ActionResponse *response) override;

	grpc::Status PurgeIdmapperNegativeCache(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		nfsProtoUtil::ActionResponse *response) override;

	grpc::Status
	MallocTrace(grpc::ServerContext *context,
		    const nfsService::MallocTraceRequest *request,
		    nfsProtoUtil::MessageResponse *response) override;

	grpc::Status
	MallocUntrace(grpc::ServerContext *context,
		      const nfsProtoUtil::EmptyRequest *request,
		      nfsProtoUtil::MessageResponse *response) override;

	grpc::Status
	TrimEnableDisable(grpc::ServerContext *context,
			  const nfsService::TrimEnableDisableRequest *request,
			  nfsProtoUtil::ActionResponse *response) override;

	grpc::Status TrimCall(grpc::ServerContext *context,
			      const nfsProtoUtil::EmptyRequest *request,
			      nfsProtoUtil::ActionResponse *response) override;

	grpc::Status
	TrimStatus(grpc::ServerContext *context,
		   const nfsProtoUtil::EmptyRequest *request,
		   nfsService::TrimStatusResponse *response) override;

	grpc::Status
	ReReadConfig(grpc::ServerContext *context,
		     const nfsProtoUtil::EmptyRequest *request,
		     nfsProtoUtil::ActionResponse *response) override;
};

class ExportStatsService final : public exportService::ExportStats::Service {
    public:
	grpc::Status
	GetNFSv3IO(grpc::ServerContext *context,
		   const nfsProtoUtil::ExportIdRequest *request,
		   exportService::ExportIoStatsResponse *response) override;

	grpc::Status
	GetNFSv40IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ExportIdRequest *request,
		    exportService::ExportIoStatsResponse *response) override;

	grpc::Status
	GetNFSv41IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ExportIdRequest *request,
		    exportService::ExportIoStatsResponse *response) override;

	grpc::Status
	GetNFSv42IO(grpc::ServerContext *context,
		    const nfsProtoUtil::ExportIdRequest *request,
		    exportService::ExportIoStatsResponse *response) override;
	grpc::Status
	GetNFSMonIO(grpc::ServerContext *context,
		    const nfsProtoUtil::ExportIdRequest *request,
		    exportService::ExportIoStatsResponse *response) override;

	grpc::Status
	GetTotalOPS(grpc::ServerContext *context,
		    const nfsProtoUtil::ExportIdRequest *request,
		    exportService::GetTotalOPSResponse *response) override;

	grpc::Status
	GetGlobalOPS(grpc::ServerContext *context,
		     const nfsProtoUtil::EmptyRequest *request,
		     exportService::GetGlobalOPSResponse *response) override;

	grpc::Status
	GetNFSIO(grpc::ServerContext *context,
		 const nfsProtoUtil::EmptyRequest *request,
		 exportService::GetNFSIOResponse *response) override;

	grpc::Status GetNFSv41Layouts(
		grpc::ServerContext *context,
		const nfsProtoUtil::ExportIdRequest *request,
		exportService::ExportLayoutsResponse *response) override;

	grpc::Status GetNFSv42Layouts(
		grpc::ServerContext *context,
		const nfsProtoUtil::ExportIdRequest *request,
		exportService::ExportLayoutsResponse *response) override;
#ifdef _USE_9P
	grpc::Status
	Get9pIO(grpc::ServerContext *context,
		const nfsProtoUtil::ExportIdRequest *request,
		exportService::ExportIoStatsResponse *response) override;

	grpc::Status
	Get9pOpStats(grpc::ServerContext *context,
		     const nfsProtoUtil::Export9pOpRequest *request,
		     exportService::ExportOpStatsResponse *response) override;
#endif
	grpc::Status
	GetFastOPS(grpc::ServerContext *context,
		   const nfsProtoUtil::EmptyRequest *request,
		   exportService::GetFastOPSResponse *response) override;

	grpc::Status GetFULLV3Stats(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		exportService::GetFULLV3StatsResponse *response) override;

	grpc::Status GetFULLV4Stats(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		exportService::GetFULLV4StatsResponse *response) override;

	grpc::Status
	ResetStats(grpc::ServerContext *context,
		   const nfsProtoUtil::EmptyRequest *request,
		   exportService::ResetStatsResponse *response) override;

	grpc::Status
	EnableStats(grpc::ServerContext *context,
		    const exportService::EnableStatsRequest *request,
		    exportService::EnableStatsResponse *response) override;

	grpc::Status
	DisableStats(grpc::ServerContext *context,
		     const exportService::DisableStatsRequest *request,
		     exportService::DisableStatsResponse *response) override;

	grpc::Status
	StatusStats(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    exportService::StatusStatsResponse *response) override;
	grpc::Status
	GetAuthStats(grpc::ServerContext *context,
		     const nfsProtoUtil::EmptyRequest *request,
		     exportService::GetAuthStatsResponse *response) override;

	grpc::Status
	ShowMDCache(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    exportService::ShowMDCacheResponse *response) override;

	grpc::Status
	ShowFDUsage(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    exportService::ShowFDUsageResponse *response) override;

	grpc::Status GetExportDetails(
		grpc::ServerContext *context,
		const nfsProtoUtil::ExportIdRequest *request,
		exportService::GetExportDetailsResponse *response) override;
};

class ExportService final : public exportService::ExportService::Service {
    public:
	grpc::Status
	DisplayExport(grpc::ServerContext *context,
		      const nfsProtoUtil::ExportIdRequest *request,
		      exportService::DisplayExportResponse *response) override;

	grpc::Status
	ShowExports(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    exportService::ShowExportsResponse *response) override;
	grpc::Status
		AddExport(grpc::ServerContext *context,
			const exportService::ExportRequest *request,
			exportService::ExportResponse *response) override;

	grpc::Status
	RemoveExport(grpc::ServerContext *context,
		     const nfsProtoUtil::ExportIdRequest *request,
		     exportService::ExportResponse *response) override;

	grpc::Status
	UpdateExport(grpc::ServerContext *context,
		     const exportService::ExportRequest *request,
		     exportService::ExportResponse *response) override;
};

class CacheMgrService final : public cacheMgr::CacheMgrService::Service {
    public:
	grpc::Status ShowIdMapperUsers(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		cacheMgr::ShowIdMapperUsersResponse *response) override;

	grpc::Status ShowIdMapperGroups(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		cacheMgr::ShowIdMapperGroupsResponse *response) override;

	grpc::Status ShowNegativeUsers(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		cacheMgr::ShowNegativeUsersResponse *response) override;

	grpc::Status ShowNegativeGroups(
		grpc::ServerContext *context,
		const nfsProtoUtil::EmptyRequest *request,
		cacheMgr::ShowNegativeGroupsResponse *response) override;

	grpc::Status
	ShowNegativeUIDs(grpc::ServerContext *context,
			 const nfsProtoUtil::EmptyRequest *request,
			 cacheMgr::ShowNegativeUIDsResponse *response) override;

	grpc::Status
	ShowUid2Grp(grpc::ServerContext *context,
		    const nfsProtoUtil::EmptyRequest *request,
		    cacheMgr::ShowUid2GrpResponse *response) override;
};
#endif //NFSSERVICE_H
