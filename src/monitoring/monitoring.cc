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
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"

#include "monitoring.h"
#include "exposer.h"

/*
 * This file contains the C++ monitoring implementation for Ganesha.
 */

static const char kClient[] = "client";
static const char kExport[] = "export";
static const char kOperation[] = "operation";
static const char kStatus[] = "status";
static const char kVersion[] = "version";

namespace ganesha_monitoring {

using CounterInt = prometheus::Counter<int64_t>;
using GaugeInt = prometheus::Gauge<int64_t>;
using HistogramInt = prometheus::Histogram<int64_t>;
using HistogramDouble = prometheus::Histogram<double>;
using LabelsMap = std::map<const std::string, const std::string>;

static prometheus::Registry registry;
static Exposer exposer(registry);

// 24 size buckets: 2 bytes to 16 MB as powers of 2.
static const HistogramInt::BucketBoundaries requestSizeBuckets =
{2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768,
 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608, 16777216};

// 30 time buckets: 0.1 ms to 12 seconds. Generated with 50% increases.
static const HistogramDouble::BucketBoundaries latencyBuckets =
{0.1, 0.15, 0.225, 0.337, 0.506, 0.759, 1.13, 1.70, 2.56, 3.84, 5.76, 8.64,
 12.9, 19.4, 29.1, 43.7, 65.6, 98.5, 147, 221, 332, 498, 748, 1122, 1683, 2525,
 3787, 5681, 8522, 12783};

class DynamicMetrics {
 public:
  DynamicMetrics(prometheus::Registry &registry);

  // Counters
  CounterInt::Family &mdcacheCacheHitsTotal;
  CounterInt::Family &mdcacheCacheMissesTotal;
  CounterInt::Family &mdcacheCacheHitsByExportTotal;
  CounterInt::Family &mdcacheCacheMissesByExportTotal;
  CounterInt::Family &rpcsReceivedTotal;
  CounterInt::Family &rpcsCompletedTotal;
  CounterInt::Family &errorsByVersionOperationStatus;

  // Per client metrics.
  // Only track request and throughput rates to reduce memory overhead.
  // NFS request metrics below also generate latency percentiles, etc.
  CounterInt::Family &clientRequestsTotal;
  CounterInt::Family &clientBytesReceivedTotal;
  CounterInt::Family &clientBytesSentTotal;

  // Gauges
  GaugeInt::Family &rpcsInFlight;
  GaugeInt::Family &lastClientUpdate;

  // Per {operation} NFS request metrics.
  CounterInt::Family &requestsTotalByOperation;
  CounterInt::Family &bytesReceivedTotalByOperation;
  CounterInt::Family &bytesSentTotalByOperation;
  HistogramInt::Family &requestSizeByOperation;
  HistogramInt::Family &responseSizeByOperation;
  HistogramDouble::Family &latencyByOperation;

  // Per {operation, export_id} NFS request metrics.
  CounterInt::Family &requestsTotalByOperationExport;
  CounterInt::Family &bytesReceivedTotalByOperationExport;
  CounterInt::Family &bytesSentTotalByOperationExport;
  HistogramInt::Family &requestSizeByOperationExport;
  HistogramInt::Family &responseSizeByOperationExport;
  HistogramDouble::Family &latencyByOperationExport;
};

DynamicMetrics::DynamicMetrics(prometheus::Registry &registry) :
  // Counters
  mdcacheCacheHitsTotal(
      prometheus::Builder<CounterInt>()
      .Name("mdcache_cache_hits_total")
      .Help("Counter for total cache hits in mdcache.")
      .Register(registry)),
  mdcacheCacheMissesTotal(
      prometheus::Builder<CounterInt>()
      .Name("mdcache_cache_misses_total")
      .Help("Counter for total cache misses in mdcache.")
      .Register(registry)),
  mdcacheCacheHitsByExportTotal(
      prometheus::Builder<CounterInt>()
      .Name("mdcache_cache_hits_by_export_total")
      .Help("Counter for total cache hits in mdcache, by export.")
      .Register(registry)),
  mdcacheCacheMissesByExportTotal(
      prometheus::Builder<CounterInt>()
      .Name("mdcache_cache_misses_by_export_total")
      .Help("Counter for total cache misses in mdcache, by export.")
      .Register(registry)),
  rpcsReceivedTotal(
      prometheus::Builder<CounterInt>()
      .Name("rpcs_received_total")
      .Help("Counter for total RPCs received.")
      .Register(registry)),
  rpcsCompletedTotal(
      prometheus::Builder<CounterInt>()
      .Name("rpcs_completed_total")
      .Help("Counter for total RPCs completed.")
      .Register(registry)),
  errorsByVersionOperationStatus(
      prometheus::Builder<CounterInt>()
      .Name("nfs_errors_total")
      .Help("Error count by version, operation and status.")
      .Register(registry)),

  // Per client metrics.
  clientRequestsTotal(
      prometheus::Builder<CounterInt>()
      .Name("client_requests_total")
      .Help("Total requests by client.")
      .Register(registry)),
  clientBytesReceivedTotal(
      prometheus::Builder<CounterInt>()
      .Name("client_bytes_received_total")
      .Help("Total request bytes by client.")
      .Register(registry)),
  clientBytesSentTotal(
      prometheus::Builder<CounterInt>()
      .Name("client_bytes_sent_total")
      .Help("Total response bytes sent by client.")
      .Register(registry)),

  // Gauges
  rpcsInFlight(
      prometheus::Builder<GaugeInt>()
      .Name("rpcs_in_flight")
      .Help("Number of NFS requests received or in flight.")
      .Register(registry)),
  lastClientUpdate(
      prometheus::Builder<GaugeInt>()
      .Name("last_client_update")
      .Help("Last update timestamp, per client.")
      .Register(registry)),

  // Per {operation} NFS request metrics.
  requestsTotalByOperation(
      prometheus::Builder<CounterInt>()
      .Name("nfs_requests_total")
      .Help("Total requests.")
      .Register(registry)),
  bytesReceivedTotalByOperation(
      prometheus::Builder<CounterInt>()
      .Name("nfs_bytes_received_total")
      .Help("Total request bytes.")
      .Register(registry)),
  bytesSentTotalByOperation(
      prometheus::Builder<CounterInt>()
      .Name("nfs_bytes_sent_total")
      .Help("Total response bytes.")
      .Register(registry)),
  requestSizeByOperation(
      prometheus::Builder<HistogramInt>()
      .Name("nfs_request_size_bytes")
      .Help("Request size in bytes.")
      .Register(registry)),
  responseSizeByOperation(
      prometheus::Builder<HistogramInt>()
      .Name("nfs_response_size_bytes")
      .Help("Response size in bytes.")
      .Register(registry)),
  latencyByOperation(
      prometheus::Builder<HistogramDouble>()
      .Name("nfs_latency_ms")
      .Help("Request latency in ms.")
      .Register(registry)),

  // Per {operation, export_id} NFS request metrics.
  requestsTotalByOperationExport(
      prometheus::Builder<CounterInt>()
      .Name("nfs_requests_by_export_total")
      .Help("Total requests by export.")
      .Register(registry)),
  bytesReceivedTotalByOperationExport(
      prometheus::Builder<CounterInt>()
      .Name("nfs_bytes_received_by_export_total")
      .Help("Total request bytes by export.")
      .Register(registry)),
  bytesSentTotalByOperationExport(
      prometheus::Builder<CounterInt>()
      .Name("nfs_bytes_sent_by_export_total")
      .Help("Total response bytes by export.")
      .Register(registry)),
  requestSizeByOperationExport(
      prometheus::Builder<HistogramInt>()
      .Name("nfs_request_size_by_export_bytes")
      .Help("Request size by export in bytes.")
      .Register(registry)),
  responseSizeByOperationExport(
      prometheus::Builder<HistogramInt>()
      .Name("nfs_response_size_by_export_bytes")
      .Help("Response size by export in bytes.")
      .Register(registry)),
  latencyByOperationExport(
      prometheus::Builder<HistogramDouble>()
      .Name("nfs_latency_ms_by_export")
      .Help("Request latency by export in ms.")
      .Register(registry)) {
}

static std::unique_ptr<DynamicMetrics> dynamic_metrics;

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

static void toLowerCase(std::string &s) {
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
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
  exposer.start(port);
  dynamic_metrics = std::make_unique<DynamicMetrics>(registry);
  initialised = true;
}

void monitoring__dynamic_observe_nfs_request(
                              const char *operation,
                              nsecs_elapsed_t request_time,
                              const char* version,
                              const char* status_label,
                              export_id_t export_id,
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
    dynamic_metrics->clientRequestsTotal
        .Add({{kClient, client},
              {kOperation, operationLowerCase}})
        .Increment();
    dynamic_metrics->lastClientUpdate.Add({{kClient, client}}).Set(epoch);
  }
  dynamic_metrics->errorsByVersionOperationStatus
      .Add({{kVersion, version},
            {kOperation, operationLowerCase},
            {kStatus, status_label}})
      .Increment();

  // Observe metrics.
  dynamic_metrics->requestsTotalByOperation
    .Add({{kOperation, operationLowerCase}})
    .Increment();
  dynamic_metrics->latencyByOperation
    .Add({{kOperation, operationLowerCase}}, latencyBuckets)
    .Observe(latency_ms);

  if (export_id == 0) {
    return;
  }

  // Observe metrics, by export.
  const std::string exportLabel = GetExportLabel(export_id);
  dynamic_metrics->requestsTotalByOperationExport
    .Add({{kOperation, operationLowerCase}, {kExport, exportLabel}})
    .Increment();
  dynamic_metrics->latencyByOperationExport
    .Add({{kOperation, operationLowerCase},
          {kExport, exportLabel}}, latencyBuckets)
    .Observe(latency_ms);
}

void monitoring__dynamic_observe_nfs_io(
                       size_t bytes_requested,
                       size_t bytes_transferred,
                       bool success,
                       bool is_write,
                       export_id_t export_id,
                       const char* client_ip) {
  const std::string operation(is_write ? "write" : "read");
  const size_t bytes_received = (is_write ? 0 : bytes_transferred);
  const size_t bytes_sent = (is_write ? bytes_transferred : 0);
  if (client_ip != NULL) {
    std::string client(client_ip);
    client = trimIPv6Prefix(client);
    dynamic_metrics->clientBytesReceivedTotal
        .Add({{kClient, client},
              {kOperation, operation}})
        .Increment(bytes_received);
    dynamic_metrics->clientBytesSentTotal
        .Add({{kClient, client},
              {kOperation, operation}})
        .Increment(bytes_sent);
  }

  // Observe metrics.
  dynamic_metrics->bytesReceivedTotalByOperation
    .Add({{kOperation, operation}})
    .Increment(bytes_received);
  dynamic_metrics->bytesSentTotalByOperation
    .Add({{kOperation, operation}})
    .Increment(bytes_sent);
  dynamic_metrics->requestSizeByOperation
    .Add({{kOperation, operation}}, requestSizeBuckets)
    .Observe(bytes_requested);
  dynamic_metrics->responseSizeByOperation
    .Add({{kOperation, operation}}, requestSizeBuckets)
    .Observe(bytes_sent);

  // Ignore export id 0. It's never used for actual exports, but can happen
  // during the setup phase, or when the export id is unknown.
  if (export_id == 0) return;

  // Observe by export metrics.
  const std::string exportLabel = GetExportLabel(export_id);
  dynamic_metrics->bytesReceivedTotalByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}})
    .Increment(bytes_received);
  dynamic_metrics->bytesSentTotalByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}})
    .Increment(bytes_sent);
  dynamic_metrics->requestSizeByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}},
         requestSizeBuckets)
    .Observe(bytes_requested);
  dynamic_metrics->responseSizeByOperationExport
    .Add({{kOperation, operation}, {kExport, exportLabel}},
         requestSizeBuckets)
    .Observe(bytes_sent);
}

void monitoring__dynamic_mdcache_cache_hit(const char *operation,
                                           export_id_t export_id) {
  dynamic_metrics->mdcacheCacheHitsTotal.Add({{kOperation, operation}}).Increment();
  if (export_id != 0) {
    const std::string exportLabel = GetExportLabel(export_id);
    dynamic_metrics->mdcacheCacheHitsByExportTotal
            .Add({{kExport, exportLabel},
                  {kOperation, operation}})
        .Increment();
  }
}

void monitoring__dynamic_mdcache_cache_miss(const char *operation,
                                            export_id_t export_id) {
  dynamic_metrics->mdcacheCacheMissesTotal
      .Add({{kOperation, operation}})
      .Increment();
  if (export_id != 0) {
    dynamic_metrics->mdcacheCacheMissesByExportTotal
        .Add({{kExport, GetExportLabel(export_id)},
              {kOperation, operation}})
        .Increment();
  }
}

void monitoring_rpc_received() {
  dynamic_metrics->rpcsReceivedTotal.Add({}).Increment();
}

void monitoring_rpc_completed() {
  dynamic_metrics->rpcsCompletedTotal.Add({}).Increment();
}

void monitoring_rpcs_in_flight(const uint64_t value) {
  dynamic_metrics->rpcsInFlight.Add({}).Set(value);
}

}  // extern "C"

}  // namespace ganesha_monitoring
