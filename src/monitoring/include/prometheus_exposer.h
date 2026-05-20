/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2025 Google LLC
 * Contributor : Roy Babayov  roybabayov@google.com
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
 * 02110-1301 USA
 *
 * ---------------------------------------
 */

/**
 * @file prometheus_exposer.h
 * @author Roy Babayov <roybabayov@google.com>
 * @brief Prometheus client that exposes HTTP interface for metrics scraping.
 */

#ifndef PROMETHEUS_EXPOSER_H
#define PROMETHEUS_EXPOSER_H

#include "monitoring.h"

typedef struct sockaddr_storage sockaddr_t;

#ifdef USE_MONITORING

#ifndef __cplusplus
void prometheus_exposer__start(const sockaddr_t *addr, uint16_t port,
			       prometheus_registry_handle_t registry_handle);
void prometheus_exposer__stop(prometheus_registry_handle_t registry_handle);
#else /* __cplusplus */

#include <thread>
#include "prometheus/histogram.h"
#include "prometheus/text_serializer.h"
#include "prometheus/registry.h"

extern "C" {
void prometheus_exposer__start(const sockaddr_t *addr, uint16_t port,
			       prometheus_registry_handle_t registry_handle);
void prometheus_exposer__stop(prometheus_registry_handle_t registry_handle);

void update_mem_info(void);
void update_export_mem(void);
void register_all_metrics(void);
} /* extern "C" */

namespace ganesha_monitoring
{
using HistogramInt = prometheus::Histogram<int64_t>;

class PrometheusExposer {
    public:
	explicit PrometheusExposer(prometheus::Registry &registry);
	~PrometheusExposer();

	void start(const sockaddr_t *addr, uint16_t port);
	void stop(void);

    private:
	prometheus::Registry &registry_;
	HistogramInt::Family &scrapingLatencies_;
	prometheus::Histogram<int64_t> &successLatencies_;
	prometheus::Histogram<int64_t> &failureLatencies_;
	static constexpr int INVALID_FD = -1;
	int server_fd_ = INVALID_FD;
	bool running_ = false;
	std::thread thread_id_;
	std::mutex mutex_;

	// Delete copy/move constructor/assignment
	PrometheusExposer(const PrometheusExposer &) = delete;
	PrometheusExposer &operator=(const PrometheusExposer &) = delete;
	PrometheusExposer(PrometheusExposer &&) = delete;
	PrometheusExposer &operator=(PrometheusExposer &&) = delete;

	static void *server_thread(void *arg);
};

} // namespace ganesha_monitoring

#endif /* __cplusplus */

#else /* USE_MONITORING */

#ifndef UNUSED
#define UNUSED_ATTR __attribute__((unused))
#define UNUSED(...) UNUSED_(__VA_ARGS__)
#define UNUSED_(arg) NOT_USED_##arg UNUSED_ATTR
#endif

static inline void
prometheus_exposer__start(const sockaddr_t *UNUSED(addr), uint16_t UNUSED(port),
			  prometheus_registry_handle_t UNUSED(registry_handle))
{
}

static inline void
prometheus_exposer__stop(prometheus_registry_handle_t UNUSED(registry_handle))
{
}
#endif /* USE_MONITORING */

#endif /* PROMETHEUS_EXPOSER_H */
