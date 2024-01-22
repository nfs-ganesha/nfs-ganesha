/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Google Inc., 2022
 * Author: Bjorn Leffler leffler@google.com
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <unistd.h>

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <algorithm>

#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/family.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"

#include "monitoring.h"
#include "nfs_convert.h"
#include "log.h"

#include "monitoring_internal.h"

/*
 * This file contains the C++ monitoring implementation for Ganesha.
 */

// 24 size buckets: 2 bytes to 16 MB as powers of 2.
static const std::initializer_list<double> requestSizeBuckets =
{2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768,
 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216};

// 30 time buckets: 0.1 ms to 12 seconds. Generated with 50% increases.
static const std::initializer_list<double> latencyBuckets =
{0.1, 0.15, 0.225, 0.337, 0.506, 0.759, 1.13, 1.70, 2.56, 3.84, 5.76, 8.64,
 12.9, 19.4, 29.1, 43.7, 65.6, 98.5, 147, 221, 332, 498, 748, 1122, 1683, 2525,
 3787, 5681, 8522, 12783};

static const char kClient[] = "client";
static const char kExport[] = "export";
static const char kOperation[] = "operation";
static const char kStatus[] = "status";
static const char kVersion[] = "version";

namespace ganesha_monitoring {

class Metrics {
 public:
  Metrics(prometheus::Registry &registry);

  // Counters
  prometheus::Family<prometheus::Counter> &mdcacheCacheHitsTotal;
  prometheus::Family<prometheus::Counter> &mdcacheCacheMissesTotal;
  prometheus::Family<prometheus::Counter> &mdcacheCacheHitsByExportTotal;
  prometheus::Family<prometheus::Counter> &mdcacheCacheMissesByExportTotal;
  prometheus::Family<prometheus::Counter> &rpcsReceivedTotal;
  prometheus::Family<prometheus::Counter> &rpcsCompletedTotal;
  prometheus::Family<prometheus::Counter> &errorsByVersionOperationStatus;

  // Per client metrics.
  // Only track request and throughput rates to reduce memory overhead.
  // NFS request metrics below also generate latency percentiles, etc.
  prometheus::Family<prometheus::Counter> &clientRequestsTotal;
  prometheus::Family<prometheus::Counter> &clientBytesReceivedTotal;
  prometheus::Family<prometheus::Counter> &clientBytesSentTotal;

  // Gauges
  prometheus::Family<prometheus::Gauge> &rpcsInFlight;
  prometheus::Family<prometheus::Gauge> &lastClientUpdate;

  // Per {operation} NFS request metrics.
  prometheus::Family<prometheus::Counter> &requestsTotalByOperation;
  prometheus::Family<prometheus::Counter> &bytesReceivedTotalByOperation;
  prometheus::Family<prometheus::Counter> &bytesSentTotalByOperation;
  prometheus::Family<prometheus::Histogram> &requestSizeByOperation;
  prometheus::Family<prometheus::Histogram> &responseSizeByOperation;
  prometheus::Family<prometheus::Histogram> &latencyByOperation;

  // Per {operation, export_id} NFS request metrics.
  prometheus::Family<prometheus::Counter> &requestsTotalByOperationExport;
  prometheus::Family<prometheus::Counter> &bytesReceivedTotalByOperationExport;
  prometheus::Family<prometheus::Counter> &bytesSentTotalByOperationExport;
  prometheus::Family<prometheus::Histogram> &requestSizeByOperationExport;
  prometheus::Family<prometheus::Histogram> &responseSizeByOperationExport;
  prometheus::Family<prometheus::Histogram> &latencyByOperationExport;
};

Metrics::Metrics(prometheus::Registry &registry) :
  // Counters
  mdcacheCacheHitsTotal(
      prometheus::BuildCounter()
      .Name("mdcache_cache_hits_total")
      .Help("Counter for total cache hits in mdcache.")
      .Register(registry)),
  mdcacheCacheMissesTotal(
      prometheus::BuildCounter()
      .Name("mdcache_cache_misses_total")
      .Help("Counter for total cache misses in mdcache.")
      .Register(registry)),
  mdcacheCacheHitsByExportTotal(
      prometheus::BuildCounter()
      .Name("mdcache_cache_hits_by_export_total")
      .Help("Counter for total cache hits in mdcache, by export.")
      .Register(registry)),
  mdcacheCacheMissesByExportTotal(
      prometheus::BuildCounter()
      .Name("mdcache_cache_misses_by_export_total")
      .Help("Counter for total cache misses in mdcache, by export.")
      .Register(registry)),
  rpcsReceivedTotal(
      prometheus::BuildCounter()
      .Name("rpcs_received_total")
      .Help("Counter for total RPCs received.")
      .Register(registry)),
  rpcsCompletedTotal(
      prometheus::BuildCounter()
      .Name("rpcs_completed_total")
      .Help("Counter for total RPCs completed.")
      .Register(registry)),
  errorsByVersionOperationStatus(
      prometheus::BuildCounter()
      .Name("nfs_errors_total")
      .Help("Error count by version, operation and status.")
      .Register(registry)),

  // Per client metrics.
  clientRequestsTotal(
      prometheus::BuildCounter()
      .Name("client_requests_total")
      .Help("Total requests by client.")
      .Register(registry)),
  clientBytesReceivedTotal(
      prometheus::BuildCounter()
      .Name("client_bytes_received_total")
      .Help("Total request bytes by client.")
      .Register(registry)),
  clientBytesSentTotal(
      prometheus::BuildCounter()
      .Name("client_bytes_sent_total")
      .Help("Total response bytes sent by client.")
      .Register(registry)),

  // Gauges
  rpcsInFlight(
      prometheus::BuildGauge()
      .Name("rpcs_in_flight")
      .Help("Number of NFS requests received or in flight.")
      .Register(registry)),
  lastClientUpdate(
      prometheus::BuildGauge()
      .Name("last_client_update")
      .Help("Last update timestamp, per client.")
      .Register(registry)),

  // Per {operation} NFS request metrics.
  requestsTotalByOperation(
      prometheus::BuildCounter()
      .Name("nfs_requests_total")
      .Help("Total requests.")
      .Register(registry)),
  bytesReceivedTotalByOperation(
      prometheus::BuildCounter()
      .Name("nfs_bytes_received_total")
      .Help("Total request bytes.")
      .Register(registry)),
  bytesSentTotalByOperation(
      prometheus::BuildCounter()
      .Name("nfs_bytes_sent_total")
      .Help("Total response bytes.")
      .Register(registry)),
  requestSizeByOperation(
      prometheus::BuildHistogram()
      .Name("nfs_request_size_bytes")
      .Help("Request size in bytes.")
      .Register(registry)),
  responseSizeByOperation(
      prometheus::BuildHistogram()
      .Name("nfs_response_size_bytes")
      .Help("Response size in bytes.")
      .Register(registry)),
  latencyByOperation(
      prometheus::BuildHistogram()
      .Name("nfs_latency_ms")
      .Help("Request latency in ms.")
      .Register(registry)),

  // Per {operation, export_id} NFS request metrics.
  requestsTotalByOperationExport(
      prometheus::BuildCounter()
      .Name("nfs_requests_by_export_total")
      .Help("Total requests by export.")
      .Register(registry)),
  bytesReceivedTotalByOperationExport(
      prometheus::BuildCounter()
      .Name("nfs_bytes_received_by_export_total")
      .Help("Total request bytes by export.")
      .Register(registry)),
  bytesSentTotalByOperationExport(
      prometheus::BuildCounter()
      .Name("nfs_bytes_sent_by_export_total")
      .Help("Total response bytes by export.")
      .Register(registry)),
  requestSizeByOperationExport(
      prometheus::BuildHistogram()
      .Name("nfs_request_size_by_export_bytes")
      .Help("Request size by export in bytes.")
      .Register(registry)),
  responseSizeByOperationExport(
      prometheus::BuildHistogram()
      .Name("nfs_response_size_by_export_bytes")
      .Help("Response size by export in bytes.")
      .Register(registry)),
  latencyByOperationExport(
      prometheus::BuildHistogram()
      .Name("nfs_latency_ms_by_export")
      .Help("Request latency by export in ms.")
      .Register(registry)) {
}

static std::unique_ptr<Metrics> metrics;

static std::string trimIPv6Prefix(const std::string input) {
  const std::string prefix("::ffff:");
  if (input.find(prefix) == 0) {
    return input.substr(prefix.size());
  }
  return input;
}

static std::map<export_id_t, std::string> exportLabels;

const std::string GetExportLabel(export_id_t export_id) {
  if (exportLabels.find(export_id) == exportLabels.end()) {
    std::ostringstream ss;
    ss << "export_id=" << export_id;
    exportLabels[export_id] = ss.str();
  }
  return exportLabels[export_id];
}

std::unique_ptr<prometheus::Exposer> exposer;
std::shared_ptr<prometheus::Registry> registry;

static void toLowerCase(std::string &s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
}

static void observeNfsRequest(const char *operation,
                              const nsecs_elapsed_t request_time,
                              const char* version,
                              const char* statusLabel,
                              const export_id_t export_id,
                              const char* client_ip) {
  const int64_t latency_ms = request_time / NS_PER_MSEC;
  std::string operationLowerCase = std::string(operation);
  toLowerCase(operationLowerCase);
  if (client_ip != NULL) {
    std::string client(client_ip);
    int64_t epoch =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    client = trimIPv6Prefix(client);
    metrics->clientRequestsTotal
        .Add({{kClient, client},
              {kOperation, operationLowerCase}})
        .Increment();
    metrics->lastClientUpdate.Add({{kClient, client}}).Set(epoch);
  }
  metrics->errorsByVersionOperationStatus
      .Add({{kVersion, version},
            {kOperation, operationLowerCase},
            {kStatus, statusLabel}})
      .Increment();

  // Observe metrics.
  metrics->requestsTotalByOperation
    .Add({{kOperation, operationLowerCase}})
    .Increment();
  metrics->latencyByOperation
    .Add({{kOperation, operationLowerCase}}, latencyBuckets)
    .Observe(latency_ms);

  if (export_id == 0) {
    return;
  }

  // Observe metrics, by export.
  const std::string exportLabel = GetExportLabel(export_id);
  metrics->requestsTotalByOperationExport
    .Add({{kOperation, operationLowerCase}, {kExport, exportLabel}})
    .Increment();
  metrics->latencyByOperationExport
    .Add({{kOperation, operationLowerCase},
          {kExport, exportLabel}}, latencyBuckets)
    .Observe(latency_ms);
}

/*
 * Functions used from NFS Ganesha below.
 */

extern "C" {

void monitoring_register_export_label(const export_id_t export_id,
                                      const char* label) {
  exportLabels[export_id] = std::string(label);
}

void monitoring_init(const uint16_t port) {
  static bool initialised;
  if (initialised)
    return;
  std::ostringstream ss;
  ss << "0.0.0.0:" << port;
  std::string hostPort = ss.str();
  LogEvent(COMPONENT_INIT, "Init monitoring at %s", hostPort.c_str());
  exposer.reset(new prometheus::Exposer(hostPort));
  registry = std::make_shared<prometheus::Registry>();
  exposer->RegisterCollectable(registry);
  metrics.reset(new Metrics(*registry));
  initialised = true;
}

void monitoring_nfs3_request(const uint32_t proc,
                             const nsecs_elapsed_t request_time,
                             const nfsstat3 nfs_status,
                             const export_id_t export_id,
                             const char* client_ip) {
  const char* version = "nfs3";
  const char *operation = nfsproc3_to_str(proc);
  const char *statusLabel = nfsstat3_to_str(nfs_status);
  observeNfsRequest(operation, request_time, version, statusLabel, export_id,
                    client_ip);
}

void monitoring_nfs4_request(const uint32_t op,
                             const nsecs_elapsed_t request_time,
                             const nfsstat4 status,
                             const export_id_t export_id,
                             const char* client_ip) {
  const char* version = "nfs4";
  const char *operation = nfsop4_to_str(op);
  const char *statusLabel = nfsstat4_to_str(status);
  observeNfsRequest(operation, request_time, version, statusLabel, export_id,
                    client_ip);
}

void monitoring_nfs_io(const size_t bytes_requested,
                       const size_t bytes_transferred,
                       const bool success,
                       const bool is_write,
                       const export_id_t export_id,
                       const char* client_ip) {
  const std::string operation(is_write ? "write" : "read");
  const size_t bytes_received = (is_write ? 0 : bytes_transferred);
  const size_t bytes_sent = (is_write ? bytes_transferred : 0);
  if (client_ip != NULL) {
    std::string client(client_ip);
    client = trimIPv6Prefix(client);
    metrics->clientBytesReceivedTotal
        .Add({{kClient, client},
              {kOperation, operation}})
        .Increment(bytes_received);
    metrics->clientBytesSentTotal
        .Add({{kClient, client},
              {kOperation, operation}})
        .Increment(bytes_sent);
  }

  // Observe metrics.
  metrics->bytesReceivedTotalByOperation
    .Add({{kOperation, operation}})
    .Increment(bytes_received);
  metrics->bytesSentTotalByOperation
    .Add({{kOperation, operation}})
    .Increment(bytes_sent);
  metrics->requestSizeByOperation
    .Add({{kOperation, operation}}, requestSizeBuckets)
    .Observe(bytes_requested);
  metrics->responseSizeByOperation
    .Add({{kOperation, operation}}, requestSizeBuckets)
    .Observe(bytes_sent);

  // Ignore export id 0. It's never used for actual exports, but can happen
  // during the setup phase, or when the export id is unknown.
  if (export_id == 0) return;

  // Observe by export metrics.
  const std::string exportLabel = GetExportLabel(export_id);
  metrics->bytesReceivedTotalByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}})
    .Increment(bytes_received);
  metrics->bytesSentTotalByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}})
    .Increment(bytes_sent);
  metrics->requestSizeByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}},
         requestSizeBuckets)
    .Observe(bytes_requested);
  metrics->responseSizeByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}},
         requestSizeBuckets)
    .Observe(bytes_sent);
}

void monitoring_mdcache_cache_hit(const char *operation,
                                  const export_id_t export_id) {
  metrics->mdcacheCacheHitsTotal.Add({{kOperation, operation}}).Increment();
  if (export_id != 0) {
    const std::string exportLabel = GetExportLabel(export_id);
    metrics->mdcacheCacheHitsByExportTotal
            .Add({{kExport, exportLabel},
                  {kOperation, operation}})
        .Increment();
  }
}

void monitoring_mdcache_cache_miss(const char *operation,
                                   const export_id_t export_id) {
  metrics->mdcacheCacheMissesTotal
      .Add({{kOperation, operation}})
      .Increment();
  if (export_id != 0) {
    metrics->mdcacheCacheMissesByExportTotal
        .Add({{kExport, GetExportLabel(export_id)},
              {kOperation, operation}})
        .Increment();
  }
}

void monitoring_rpc_received() {
  metrics->rpcsReceivedTotal.Add({}).Increment();
}

void monitoring_rpc_completed() {
  metrics->rpcsCompletedTotal.Add({}).Increment();
}

void monitoring_rpcs_in_flight(const uint64_t value) {
  metrics->rpcsInFlight.Add({}).Set(value);
}

}  // extern "C"

}  // namespace ganesha_monitoring
