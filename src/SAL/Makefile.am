AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)


noinst_LTLIBRARIES            = libsal.la

#check_PROGRAMS                = test_cache_inode test_cache_inode_readlink \
#                                test_cache_inode_readdir test_cache_inode_lookup 

libsal_la_SOURCES = state_async.c                    \
                    state_lock.c                     \
                    state_share.c                    \
                    state_misc.c                     \
                    state_layout.c                   \
                    nfs4_clientid.c                  \
                    nfs4_state.c                     \
                    nfs4_state_id.c                  \
                    nfs4_owner.c                     \
                    nfs4_lease.c                     \
                    nfs4_recovery.c                  \
                    nfs41_session_id.c

libsal_la_SOURCES += nlm_owner.c

if USE_9P
libsal_la_SOURCES += 9p_owner.c
endif



new: clean all
