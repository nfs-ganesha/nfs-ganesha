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

#include <grpcpp/grpcpp.h>
#include <iostream>
#include "GrpcServerInit.h"
#include "GrpcServer.h"
#include "gsh_rpc.h"
#include <grpcpp/ext/proto_server_reflection_plugin.h>

/*
 *     gRPC (gRPC Remote Procedure Call) is an open-source framework that
 *     enables communication between services through remote procedure
 *     calls (RPCs). It is built on top of HTTP/2 and uses Protocol Buffers
 *     (protobuf) as the interface definition language (IDL).
 *     The below infra facilitates intra server as well as inter server
 *     communication.
 */

GrpcServer ganesha_grpc_server;

/* stop gRPC server */
GrpcServer::~GrpcServer()
{
	stop();
}

/* start gRPC server */
void GrpcServer::start(uint16_t port, std::string server_crt,
		       std::string server_key, std::string ca_crt,
		       std::string ip_addr, uint16_t ip_family)
{
	std::call_once(start_once_, [this, port,
				     server_crt = std::move(server_crt),
				     server_key = std::move(server_key),
				     ca_crt = std::move(ca_crt),
				     ip_addr = std::move(ip_addr),
				     ip_family]() {
		server_thread_ = std::thread(
			[this, port, server_crt = std::move(server_crt),
			 server_key = std::move(server_key),
			 ca_crt = std::move(ca_crt),
			 ip_addr = std::move(ip_addr), ip_family]() {
				gRPCServerStart(port, std::move(server_crt),
						std::move(server_key),
						std::move(ca_crt),
						std::move(ip_addr), ip_family);
			});
	});
}

/* gRPC Server thread function */
void GrpcServer::gRPCServerStart(uint16_t port, std::string server_crt,
				 std::string server_key, std::string ca_crt,
				 std::string ip_addr, uint16_t ip_family)
{
	{ /* Taking a lock */
		const std::lock_guard<std::mutex> lock(mutex_);

		/* If default address is provided and
		** v6 ip is allowed than the sever will
		** automatically accept IPv6 connections
		** default port number is 50051
        */

		if (server_) {
			LogDebug(COMPONENT_GRPC,
				 "gRPC server is already running");
			return;
		}

		if (ip_family == AF_INET6) {
			server_address_ =
				"[" + ip_addr + "]:" + std::to_string(port);
		} else {
			server_address_ = ip_addr + ":" + std::to_string(port);
		}

		grpc::SslServerCredentialsOptions::PemKeyCertPair
			key_cert_pair = { std::move(server_key),
					  std::move(server_crt) };

		grpc::SslServerCredentialsOptions ssl_opts;
		ssl_opts.pem_key_cert_pairs.push_back(std::move(key_cert_pair));

		/* To validate client certificates */
		ssl_opts.pem_root_certs = std::move(ca_crt);
		ssl_opts.client_certificate_request =
			GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY;

		std::shared_ptr<grpc::ServerCredentials> server_creds =
			grpc::SslServerCredentials(ssl_opts);

		grpc::ServerBuilder builder;
		/* Adding the listening port */
		builder.AddListeningPort(server_address_, server_creds);

		/* Register the service with the builder */
		builder.RegisterService(&showClientService);

		builder.RegisterService(&nfsIngrace);

		builder.RegisterService(&getClientSessionIds);

		builder.RegisterService(&fakeRecallService);

		builder.RegisterService(&startNfsGrace);

		builder.RegisterService(&clientStatsService);

		builder.RegisterService(&clientMgrService);

		builder.RegisterService(&clusterMembersService);

		builder.RegisterService(&nfsAdmin);

		builder.RegisterService(&exportStatsService);

		builder.RegisterService(&exportService);

		builder.RegisterService(&cachemgr);

		builder.RegisterService(&qosMgrService);

		/* Reflection Service to enable grpc CLI */
		grpc::reflection::InitProtoReflectionServerBuilderPlugin();

		/* Start the server */
		server_ = builder.BuildAndStart();

		if (!server_) {
			LogFatal(COMPONENT_GRPC,
				 "Failed to start gRPC server on %s",
				 server_address_.c_str());
		}
	}

	server_->Wait();
}

/* Stop the gRPC server */
void GrpcServer::stop()
{
	{
		const std::lock_guard<std::mutex> lock(mutex_);

		if (server_) {
			server_->Shutdown();
			server_->Wait();
			server_.reset();
		}
	}

	if (server_thread_.joinable()) {
		/* Wait for the server thread to finish */
		server_thread_.join();
	}
}

extern "C" {

/* The event is triggered when NFS is initialized */
void grpc__init(uint16_t port, char *server_crt, char *server_key, char *ca_crt,
		sockaddr_t *addr)
{
	static bool initialized = false;
	char ipstring[SOCK_NAME_MAX] = "\0";

	if (initialized)
		return;

	struct display_buffer dspbuf = { sizeof(ipstring), ipstring, ipstring };
	display_sockip(&dspbuf, addr);

	LogDebug(COMPONENT_GRPC,
		 "Path to server certificate: %s "
		 "Path to server key : %s"
		 "Path to ca certificate : %s",
		 server_crt, server_key, ca_crt);

	std::string key = read_cert_file(server_key);
	std::string cert = read_cert_file(server_crt);
	std::string ca = read_cert_file(ca_crt);

	/* If the key or certificated files are not found
    ** than gRPC cannot run securely, hence exiting.
    */
	if (key.empty() || cert.empty() || ca.empty()) {
		LogWarn(COMPONENT_GRPC,
			"Failed to get server key or server certificate or CA certificate."
			"gRPC server failed to start");

		return;
	}

	/* Start gRPC server */
	ganesha_grpc_server.start(port, cert, key, ca, ipstring,
				  addr->ss_family);

	initialized = true;
}

/* Shutdown gRPC */
void grpc_shutdown()
{
	ganesha_grpc_server.stop();
}

} /* extern C */
