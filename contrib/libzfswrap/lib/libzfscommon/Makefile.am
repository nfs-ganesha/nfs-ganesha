noinst_LTLIBRARIES = libzfscommon-user.la libzfscommon-kernel.la

libzfscommon_user_la_SOURCES = compress.c list.c zfs_comutil.c zfs_deleg.c zfs_namecheck.c zfs_prop.c zpool_prop.c zprop_common.c
libzfscommon_user_la_CFLAGS = -I${top_srcdir}/lib/libzfscommon/include \
                              -I${top_srcdir}/lib/libavl/include \
                              -I${top_srcdir}/lib/libnvpair/include \
                              -I${top_srcdir}/lib/libumem/include \
                              -I${top_srcdir}/lib/libzpool/include \
                              -I${top_srcdir}/lib/libsolcompat/include \
                              @DEBUG_CFLAGS@

libzfscommon_kernel_la_SOURCES = compress.c list.c zfs_comutil.c zfs_deleg.c zfs_namecheck.c zfs_prop.c zpool_prop.c zprop_common.c
libzfscommon_kernel_la_CFLAGS = -I${top_srcdir}/lib/libzfscommon/include \
                                -I${top_srcdir}/lib/libavl/include \
                                -I${top_srcdir}/lib/libnvpair/include \
                                -I${top_srcdir}/lib/libumem/include \
                                -I${top_srcdir}/lib/libsolkerncompat/include \
                                -D_KERNEL @DEBUG_CFLAGS@

noinst_HEADERS = include/format.h \
                 include/zfs_comutil.h \
                 include/zfs_deleg.h \
                 include/zfs_fletcher.h \
                 include/zfs_namecheck.h \
                 include/zfs_prop.h \
                 include/sys/arc.h \
                 include/sys/bplist.h \
                 include/sys/compress.h \
                 include/sys/dbuf.h \
                 include/sys/ddt.h \
                 include/sys/dmu.h \
                 include/sys/dmu_impl.h \
                 include/sys/dmu_objset.h \
                 include/sys/dmu_traverse.h \
                 include/sys/dmu_tx.h \
                 include/sys/dmu_zfetch.h \
                 include/sys/dnode.h \
                 include/sys/dsl_dataset.h \
                 include/sys/dsl_deleg.h \
                 include/sys/dsl_dir.h \
                 include/sys/dsl_pool.h \
                 include/sys/dsl_prop.h \
                 include/sys/dsl_synctask.h \
                 include/sys/list.h \
                 include/sys/list_impl.h \
                 include/sys/metaslab.h \
                 include/sys/metaslab_impl.h \
                 include/sys/refcount.h \
                 include/sys/rprwlock.h \
                 include/sys/rrwlock.h \
                 include/sys/spa_boot.h \
                 include/sys/space_map.h \
                 include/sys/spa.h \
                 include/sys/spa_impl.h \
                 include/sys/txg.h \
                 include/sys/txg_impl.h \
                 include/sys/uberblock.h \
                 include/sys/uberblock_impl.h \
                 include/sys/unique.h \
                 include/sys/vdev_disk.h \
                 include/sys/vdev_file.h \
                 include/sys/vdev.h \
                 include/sys/vdev_impl.h \
                 include/sys/zap.h \
                 include/sys/zap_impl.h \
                 include/sys/zap_leaf.h \
                 include/sys/zfs_acl.h \
                 include/sys/zfs_ctldir.h \
                 include/sys/zfs_debug.h \
                 include/sys/zfs_dir.h \
                 include/sys/zfs_fuid.h \
                 include/sys/zfs_ioctl.h \
                 include/sys/zfs_rlock.h \
                 include/sys/zfs_vfsops.h \
                 include/sys/zfs_znode.h \
                 include/sys/zil.h \
                 include/sys/zil_impl.h \
                 include/sys/zio_checksum.h \
                 include/sys/zio_compress.h \
                 include/sys/zio.h \
                 include/sys/zio_impl.h \
                 include/sys/zvol.h \
                 include/sys/fm/fs/zfs.h \
                 include/sys/fs/zfs.h

