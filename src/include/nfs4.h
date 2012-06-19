/*
 * Local RPC definitions, especially the GSS switch and
 * compensating definitions if we don't have GSS.
 */
#include "ganesha_rpc.h"

/* Now the NFS stuff we're looking for */

#ifdef _USE_NFS4_0
#include "nfsv40.h"
#endif

#ifdef _USE_NFS4_1
#include "nfsv41.h"
#endif

#ifndef NFS4_MAX_DOMAIN_LEN
#define NFS4_MAX_DOMAIN_LEN 512
#endif
