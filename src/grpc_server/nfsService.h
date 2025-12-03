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
#include "nfsProtoUtil.pb.h"
#include "nfsProtoUtil.grpc.pb.h"
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
#endif //NFSSERVICE_H
