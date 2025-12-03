/* SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * @brief gRPC library for NFS Ganesha.
 */

#ifndef GANESHA_GRPC_H
#define GANESHA_GRPC_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "ip_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Inits grpc module. */
void grpc__init(uint16_t port, char *server_cert, char *server_key,
		char *ca_cert, sockaddr_t *addr);

void grpc_shutdown(void);
#ifdef __cplusplus
}
#endif
#endif /* GANESHA_GRPC_H */
