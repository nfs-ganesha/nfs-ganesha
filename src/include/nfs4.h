/*
 * Local RPC definitions, especially the GSS switch and
 * compensating definitions if we don't have GSS.
 */
#include "rpc.h"

/* Now the NFS stuff we're looking for */

#ifdef _USE_NFS4_0
#include "nfsv40.h"
#endif

#ifdef _USE_NFS4_1
#include "nfsv41.h"
#endif

