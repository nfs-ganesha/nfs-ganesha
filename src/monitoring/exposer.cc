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
 * @file exposer.cc
 * @author Yoni Couriel <yonic@google.com>
 * @brief Prometheus client that exposes HTTP interface for metrics scraping.
 */
#include <arpa/inet.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <streambuf>

#include "prometheus/text_serializer.h"

#include "exposer.h"

#define PERROR(MESSAGE)                                                        \
	fprintf(stderr, "[%s:%d] %s: %s\n", __FILE__, __LINE__, (MESSAGE),     \
		strerror(errno))
#define PFATAL(MESSAGE) (PERROR(MESSAGE), abort())

namespace ganesha_monitoring {

/* streambuf wrapper for sending into a socket */
template <std::size_t size = 4096>
class SocketStreambuf : public std::streambuf {
public:
	explicit SocketStreambuf(int socket_fd) : socket_fd_(socket_fd) {
		setp(buffer_.data(), buffer_.data() + buffer_.size());
	}

protected:
	/* Flushes buffer to socket */
	int overflow(int ch) override {
		if (pptr() == epptr()) {
			/* Buffer is full, flush it */
			if (sync())
				return traits_type::eof();
		}
		if (ch != traits_type::eof()) {
			/* Store incoming character */
			*pptr() = static_cast<char>(ch);
			pbump(1);
		}
		return ch;
	}

	/* Sends buffer to socket (blocking) and clears it */
	int sync() override {
		if (aborted_)
			return -1;
		const std::size_t bytes_count = pptr() - pbase();
		if (bytes_count > 0) {
			/* Try to send buffer */
			std::size_t bytes_sent = 0;
			while (bytes_sent < bytes_count) {
				const ssize_t result = TEMP_FAILURE_RETRY(send(
					socket_fd_,
					pbase() + bytes_sent,
					bytes_count - bytes_sent,
					0));
				if (result <= 0) {
					PERROR("Could not send metrics, aborting");
					aborted_ = true;
					return -1;
				}
				bytes_sent += result;
			}
		}
		/* Clear buffer */
		pbump(-bytes_count);
		return 0;
	}

private:
	const int socket_fd_;
	bool aborted_ = false;
	std::array<char, size> buffer_{};

	// Delete copy/move constructor/assignment
	SocketStreambuf(const SocketStreambuf&) = delete;
	SocketStreambuf& operator=(const SocketStreambuf&) = delete;
	SocketStreambuf(SocketStreambuf&&) = delete;
	SocketStreambuf& operator=(SocketStreambuf&&) = delete;
};

static bool is_metric_empty(prometheus::Metric::Type type,
			    prometheus::ClientMetric &metric) {
	switch (type) {
	case prometheus::Metric::Type::Counter:
		return metric.counter.value == 0.0;
	case prometheus::Metric::Type::Summary:
		return metric.summary.sample_count == 0;
	case prometheus::Metric::Type::Histogram:
		return metric.histogram.sample_count == 0;
	default:
		return false;
	}
}

// Removes empty metrics from family
// Most metrics are empty or rarly used (for example consider
// nfsv4__op_latency_bucket{op="REMOVEXATTR",status="NFS4ERR_REPLAY"})
// Significantly reduces the amount of data transferred to the Prometheus
// server from MBs to KBs
static void compact_family(prometheus::MetricFamily &family) {
	auto first_element_to_remove = std::remove_if(
		family.metric.begin(), family.metric.end(),
		[&family](auto metric) {
			return is_metric_empty(family.type, metric); });
	// Keep at least one metric even if it's empty so it's easier to query
	if (first_element_to_remove == family.metric.begin())
		first_element_to_remove++;
	family.metric.erase(first_element_to_remove, family.metric.end());
}

Exposer::~Exposer() {
	stop();
}

void Exposer::start(uint16_t port) {
	const std::lock_guard<std::mutex> lock(mutex_);
	if (running_)
		PFATAL("Already running");

	server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd_ == -1)
		PFATAL("Failed to create socket");

	const int opt = 1;
	if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
		PFATAL("Failed to set socket options");

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)))
		PFATAL("Failed to bind socket");
	if (listen(server_fd_, 3))
		PFATAL("Failed to listen on socket");

	running_ = true;
	thread_id_ = std::thread{ server_thread, this };
}

void Exposer::stop() {
	const std::lock_guard<std::mutex> lock(mutex_);
	if (running_) {
		running_ = false;
		shutdown(server_fd_, SHUT_RDWR); // Wakes up the thread
		thread_id_.join();
		close(server_fd_);
		server_fd_ = INVALID_FD;
	}
}

void *Exposer::server_thread(void *arg) {
	Exposer *const exposer = (Exposer *)arg;
	char buffer[1024];

	while (exposer->running_) {
		const int client_fd = TEMP_FAILURE_RETRY(
			accept4(exposer->server_fd_, NULL, NULL, SOCK_CLOEXEC));
		if (client_fd < 0) {
			if (exposer->running_)
				PERROR("Failed to accept connection");
			continue;
		}
		recv(client_fd, buffer, sizeof(buffer), 0);

		auto families = exposer->registry_.Collect();
		for (auto &family : families) {
			compact_family(family);
		}

		SocketStreambuf socket_streambuf(client_fd);
		std::ostream socket_ostream(&socket_streambuf);
		socket_ostream << "HTTP/1.1 200 OK\r\n\r\n";
		prometheus::TextSerializer::Serialize(socket_ostream, families);
		socket_ostream.flush();

		close(client_fd);
	}
	return NULL;
}

} // namespace ganesha_monitoring
