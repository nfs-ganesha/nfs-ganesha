AM_CFLAGS                     = $(FSAL_CFLAGS) $(SEC_CFLAGS)

noinst_LTLIBRARIES            = libnfsproto.la

check_PROGRAMS                = test_mnt_proto

libnfsproto_la_SOURCES = mnt_Export.c	      		     \
                         mnt_Null.c                          \
                         mnt_Mnt.c                           \
                         mnt_Dump.c                          \
                         mnt_Umnt.c                          \
                         mnt_UmntAll.c                       \
                         nfs_Null.c                          \
                         nfs_proto_tools.c                   \
                         nfs4_pseudo.c                       \
                         nfs4_referral.c                     \
                         nfs4_xattr.c                        \
                         nfs_xattr.c                         \
                         nfs4_Compound.c                     \
                         nfs4_op_access.c                    \
                         nfs4_op_close.c                     \
                         nfs4_op_commit.c                    \
                         nfs4_op_create.c                    \
                         nfs4_op_delegpurge.c                \
                         nfs4_op_delegreturn.c               \
                         nfs4_op_getattr.c                   \
                         nfs4_op_getfh.c                     \
                         nfs4_op_link.c                      \
                         nfs4_op_lock.c                      \
                         nfs4_op_lockt.c                     \
                         nfs4_op_locku.c                     \
                         nfs4_op_lookup.c                    \
                         nfs4_op_lookupp.c                   \
                         nfs4_op_nverify.c                   \
                         nfs4_op_open.c                      \
                         nfs4_op_openattr.c                  \
                         nfs4_op_open_downgrade.c            \
                         nfs4_op_open_confirm.c              \
                         nfs4_op_putfh.c                     \
                         nfs4_op_putpubfh.c                  \
                         nfs4_op_putrootfh.c                 \
                         nfs4_op_read.c                      \
                         nfs4_op_readdir.c                   \
                         nfs4_op_readlink.c                  \
                         nfs4_op_rename.c                    \
                         nfs4_op_renew.c                     \
                         nfs4_op_remove.c                    \
                         nfs4_op_restorefh.c                 \
                         nfs4_op_savefh.c                    \
                         nfs4_op_secinfo.c                   \
                         nfs4_op_setattr.c                   \
                         nfs4_op_setclientid.c               \
                         nfs4_op_setclientid_confirm.c       \
                         nfs4_op_verify.c                    \
                         nfs4_op_write.c                     \
                         nfs4_op_release_lockowner.c         \
                         nfs4_op_illegal.c                   \
                         nfs2_Root.c                         \
                         nfs2_Writecache.c                   \
                         nfs3_Access.c                       \
                         nfs3_Commit.c                       \
                         nfs3_Mknod.c                        \
                         nfs3_Fsinfo.c                       \
                         nfs3_Pathconf.c                     \
                         nfs_Lookup.c                        \
                         nfs_Fsstat.c                        \
                         nfs_Getattr.c                       \
                         nfs_Link.c                          \
                         nfs_Mkdir.c                         \
                         nfs_Create.c                        \
                         nfs_Readlink.c                      \
                         nfs_Remove.c                        \
                         nfs_Rmdir.c                         \
                         nfs_Setattr.c                       \
                         nfs_Symlink.c                       \
                         nfs_Rename.c                        \
                         nfs_Readdir.c                       \
                         nfs3_Readdirplus.c                  \
                         nfs_Write.c                         \
                         nfs_Read.c                          \
                         nfs4_cb_Null.c                      \
                         nfs4_cb_Compound.c                  \
                         nfs4_cb_illegal.c                   \
                         nfs4_cb_getattr.c                   \
                         nfs4_cb_recall.c                    \
                         ../../include/nfs_proto_functions.h \
                         ../../include/nfs_core.h            \
                         ../../include/stuff_alloc.h         \
                         ../../include/nfs23.h               \
                         ../../include/nfs4.h                \
                         ../../include/mount.h               \
                         ../../include/cache_inode.h         \
                         ../../include/cache_content.h       \
                         ../../include/nfs_exports.h         \
                         ../../include/nfs_creds.h           \
                         ../../include/nfs_tools.h           \
                         ../../include/err_inject.h          \
                         ../../include/nfs_file_handle.h

if USE_QUOTA
libnfsproto_la_SOURCES += rquota_Null.c            \
                          rquota_getquota.c        \
                          rquota_getactivequota.c  \
                          rquota_setquota.c        \
                          rquota_setactivequota.c

endif


if USE_NFS4_1
libnfsproto_la_SOURCES += nfs41_op_exchange_id.c      \
                          nfs41_op_close.c            \
                          nfs41_op_getdevicelist.c    \
                          nfs41_op_getdeviceinfo.c    \
                          nfs41_op_create_session.c   \
                          nfs41_op_destroy_session.c  \
                          nfs41_op_free_stateid.c     \
                          nfs41_op_open.c             \
                          nfs41_op_layoutget.c        \
                          nfs41_op_layoutcommit.c     \
                          nfs41_op_layoutreturn.c     \
                          nfs41_op_lock.c             \
                          nfs41_op_lockt.c            \
                          nfs41_op_locku.c            \
                          nfs41_op_read.c             \
                          nfs41_op_reclaim_complete.c \
                          nfs41_op_sequence.c         \
                          nfs41_op_set_ssv.c          \
                          nfs41_op_test_stateid.c     \
                          nfs41_op_write.c
endif


test_mnt_proto_SOURCES       = test_mnt_proto.c

test_mnt_proto_LDADD = libnfsproto.la ../../BuddyMalloc/libBuddyMalloc.la ../../Log/liblog.la

new: clean all

doc:
	doxygen ./doxygen.conf
	rep=`grep OUTPUT_DIRECTORY doxygen.conf | grep share  | awk -F '=' '{print $$2;}'` ; cd $$rep/latex ; make
