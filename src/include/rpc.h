/* This is a central clearing house for RPC definitions. Nothing
   should included anything related to RPC except this file */

#ifndef GANESHA_RPC_H
#define GANESHA_RPC_H

#ifdef _USE_GSSRPC
#include <gssrpc/rpc.h>
#include <gssrpc/types.h>
#include <gssrpc/svc.h>
#include <gssrpc/pmap_clnt.h>
#else
#ifdef _USE_TIRPC
#include <tirpc/rpc/rpc.h>
#include <tirpc/rpc/svc.h>
#include <tirpc/rpc/types.h>
#include <tirpc/rpc/pmap_clnt.h>
#else
#include <rpc/rpc.h>
#include </rpc/types.h>
#include <rpc/svc.h>
#include <rpc/pmap_clnt.h>
#endif
#endif
#endif
