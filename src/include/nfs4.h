/*
 * Local RPC definitions, especially the GSS switch and
 * compensating definitions if we don't have GSS.
 */
#include "ganesha_rpc.h"

/* Now the NFS stuff we're looking for */

#include "nfsv41.h"

#ifndef NFS4_MAX_DOMAIN_LEN
#define NFS4_MAX_DOMAIN_LEN 512
#endif
