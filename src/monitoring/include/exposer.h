/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright 2024 Google LLC
 * Contributor : Yoni Couriel  yonic@google.com
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
 * @file exposer.h
 * @author Yoni Couriel <yonic@google.com>
 * @brief Prometheus client that exposes HTTP interface for metrics scraping.
 */
#include <thread>

#include "prometheus/registry.h"

namespace ganesha_monitoring {

class Exposer {
public:
	Exposer(prometheus::Registry &registry) : registry_(registry) {}
	~Exposer();

	void start(uint16_t port);
	void stop(void);

private:
	prometheus::Registry &registry_;
	static constexpr int INVALID_FD = -1;
	int server_fd_ = INVALID_FD;
	bool running_ = false;
	std::thread thread_id_;
	std::mutex mutex_;

	// Delete copy/move constructor/assignment
	Exposer(const Exposer&) = delete;
	Exposer& operator=(const Exposer&) = delete;
	Exposer(Exposer&&) = delete;
	Exposer& operator=(Exposer&&) = delete;

	static void *server_thread(void *arg);
};

} // namespace ganesha_monitoring
