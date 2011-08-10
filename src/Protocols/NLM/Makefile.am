AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES            = libnlm.la

libnlm_la_SOURCES = nlm_Cancel.c                        \
                    nlm_Granted_Res.c                   \
                    nlm_Lock.c                          \
                    nlm_Null.c                          \
                    nlm_Sm_Notify.c                     \
                    nlm_Test.c                          \
                    nlm_Unlock.c                        \
                    nlm_async.c                         \
                    nlm_util.c                          \
                    nsm.c                               \
                    ../../include/nlm4.h                \
                    ../../include/nlm_async.h           \
                    ../../include/nlm_list.h            \
                    ../../include/nlm_util.h            \
                    ../../include/stuff_alloc.h         \
                    ../../include/cache_inode.h         \
                    ../../include/nfs_proto_functions.h

#include "cache_content.h"
#include "cache_inode.h"
#include "config.h"
#include "HashData.h"
#include "HashTable.h"
#include "log_macros.h"
#include "mount.h"
#include "nfs23.h"
#include "nfs4.h"
#include "nfs_core.h"
#include "nfs_creds.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_tools.h"
#include "nlm4.h"
#include "nlm_async.h"
#include "nlm_util.h"
#include "nsm.h"
#include "rpc.h"
#include "solaris_port.h"
#include "stuff_alloc.h"

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
