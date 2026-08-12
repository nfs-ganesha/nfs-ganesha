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

#include <iostream>
#include <grpcpp/grpcpp.h>
#include <nfsService.pb.h>
#include <nfsService.grpc.pb.h>
#include <nfsService.h>
#include <string>

using std::string_view;

std::shared_ptr<grpc::Channel> getCreds(string_view server_address)
{
	// For storing Client credentials
	grpc::SslCredentialsOptions ssl_opts;

	ssl_opts.pem_root_certs =
		read_cert_file(nfs_param.grpc_param.grpc_ca_cert);
	ssl_opts.pem_cert_chain =
		read_cert_file(nfs_param.grpc_param.grpc_client_cert);
	ssl_opts.pem_private_key =
		read_cert_file(nfs_param.grpc_param.grpc_client_key);

	auto creds = grpc::SslCredentials(ssl_opts);

	// Creating an secure channel to communicate with the server
	return grpc::CreateChannel(std::string(server_address), creds);
}

/* These is initial example only. Additional cases will be added in a
** follow-up patch.
*/
void GetClientIds(const char *server_address, const char *ca_crt,
		  const char *client_cert, const char *client_key)
{
	std::shared_ptr<grpc::Channel> channel;

	// Creating a request and response
	nfsProtoUtil::EmptyRequest request;
	nfsService::GetClientIdsResponse response;
	grpc::ClientContext context;

	channel = getCreds(server_address);

	std::unique_ptr<nfsService::GetClientId::Stub> stub =
		nfsService::GetClientId::NewStub(channel);

	// Make the gRPC call
	grpc::Status status = stub->GetClientIds(&context, request, &response);
}

/* These is initial example only. Additional cases will be added in a
** follow-up patch.
*/
void GetNfsGracePeriod(const std::string &server_address, const char *ca_crt,
		       const char *client_cert, const char *client_key)
{
	// Creating a request and response
	nfsProtoUtil::EmptyRequest request;
	nfsService::GetNfsGraceResponse response;
	grpc::ClientContext context;

	// Creating a secure channel to communicate with the server
	std::shared_ptr<grpc::Channel> channel;

	channel = getCreds(server_address);

	std::unique_ptr<nfsService::GetNfsGrace::Stub> stub =
		nfsService::GetNfsGrace::NewStub(channel);

	// Make the gRPC call
	grpc::Status status =
		stub->GetGracePeriod(&context, request, &response);
}

/* These is initial example only. Additional cases will be added in a
** follow-up patch.
*/
void GetClientSessionIds(const std::string &server_address, const char *ca_crt,
			 const char *client_cert, const char *client_key)
{
	// Creating a request and response
	nfsProtoUtil::EmptyRequest request;
	nfsService::GetSessionIdsResponse response;
	grpc::ClientContext context;

	// Creating a secure channel to communicate with the server
	std::shared_ptr<grpc::Channel> channel;

	channel = getCreds(server_address);

	std::unique_ptr<nfsService::GetSessionId::Stub> stub =
		nfsService::GetSessionId::NewStub(channel);

	// Make the gRPC call
	grpc::Status status = stub->GetSessionIds(&context, request, &response);
}

void FakeRecall(const std::string &server_address, uint64_t client_id)
{
	std::shared_ptr<grpc::Channel> channel;

	channel = getCreds(server_address);

	std::unique_ptr<nfsService::FakeRecall::Stub> stub =
		nfsService::FakeRecall::NewStub(channel);

	nfsService::FakeRecallRequest request;
	nfsProtoUtil::StatusResponse response;
	grpc::ClientContext context;

	request.set_client_id(client_id);

	grpc::Status status = stub->FakeRecall(&context, request, &response);
}

void StartGraceWithEvent(const std::string &server_address)
{
	// Creating a channel to communicate with the server
	std::shared_ptr<grpc::Channel> channel;

	channel = getCreds(server_address);

	std::unique_ptr<nfsService::StartNfsGrace::Stub> stub =
		nfsService::StartNfsGrace::NewStub(channel);

	// Creating a request and response
	nfsService::GraceWithEvent request;
	nfsService::GraceStatus response;
	grpc::ClientContext context;

	// Make the gRPC call
	grpc::Status status =
		stub->StartGraceWithEvent(&context, request, &response);
}
