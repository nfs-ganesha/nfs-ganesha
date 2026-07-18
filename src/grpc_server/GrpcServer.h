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
 *
 * @brief gRPC library for NFS Ganesha.
 */

#ifndef GRPC_SERVER_H
#define GRPC_SERVER_H

#include <string_view>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include "nfsService.h"

/* gRPC Server */
class GrpcServer {
    public:
	GrpcServer() = default;
	void start(uint16_t port, std::string server_crt,
		   std::string server_key, std::string ca_crt,
		   std::string ip_addr, uint16_t ip_family);

	void stop(void);
	~GrpcServer();

	// Delete copy/move constructor/assignment
	GrpcServer(const GrpcServer &) = delete;
	GrpcServer &operator=(const GrpcServer &) = delete;
	GrpcServer(GrpcServer &&) = delete;
	GrpcServer &operator=(GrpcServer &&) = delete;

    private:
	std::mutex mutex_;
	std::unique_ptr<grpc::Server> server_;
	std::thread server_thread_;
	std::once_flag start_once_;
	std::string server_address_;

	void gRPCServerStart(uint16_t port, std::string server_crt,
			     std::string server_key, std::string ca_crt,
			     std::string ip_addr, uint16_t ip_family);

	/* TODO: Add a separate class for the services*/
	GetClientIdService showClientService;
	GetNfsGraceService nfsIngrace;
	GetSessionIdService getClientSessionIds;
	StartNfsGraceService startNfsGrace;
	ClientStatsService clientStatsService;
	nfsAdminService nfsAdmin;
	ExportStatsService exportStatsService;
};

extern GrpcServer ganesha_grpc_server;

#endif /* GANESHA_SERVER_H */
