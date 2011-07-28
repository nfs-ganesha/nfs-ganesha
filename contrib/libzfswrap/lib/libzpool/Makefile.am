noinst_LTLIBRARIES = libzpool-user.la libzpool-kernel.la

libzpool_user_la_SOURCES = arc.c bplist.c dbuf.c dnode_sync.c dmu.c dmu_object.c dmu_objset.c dmu_send.c dmu_traverse.c dmu_tx.c dmu_zfetch.c dnode.c dsl_dataset.c dsl_deleg.c dsl_dir.c dsl_pool.c dsl_prop.c dsl_scrub.c dsl_synctask.c fletcher.c flushwc.c gzip.c lzjb.c metaslab.c refcount.c rprwlock.c rrwlock.c sha256.c spa.c spa_config.c spa_errlog.c spa_history.c spa_misc.c space_map.c txg.c uberblock.c unique.c util.c vdev.c vdev_cache.c vdev_file.c vdev_label.c vdev_mirror.c vdev_missing.c vdev_queue.c vdev_raidz.c vdev_root.c zap.c zap_leaf.c zap_micro.c zfs_byteswap.c zfs_fm.c zfs_fuid.c zfs_znode.c zil.c zio.c zio_checksum.c zio_compress.c zio_inject.c kmem_asprintf.c ddt.c ddt_zap.c zle.c kernel.c taskq.c
libzpool_user_la_CFLAGS = -I${top_srcdir}/lib/libavl/include \
                          -I${top_srcdir}/lib/libnvpair/include \
                          -I${top_srcdir}/lib/libzfscommon/include \
                          -I${top_srcdir}/lib/libumem/include \
                          -I${top_srcdir}/lib/libzpool/include \
                          -I${top_srcdir}/lib/libsolcompat/include \
                          @DEBUG_CFLAGS@

libzpool_kernel_la_SOURCES = arc.c bplist.c dbuf.c dnode_sync.c dmu.c dmu_object.c dmu_objset.c dmu_send.c dmu_traverse.c dmu_tx.c dmu_zfetch.c dnode.c dsl_dataset.c dsl_deleg.c dsl_dir.c dsl_pool.c dsl_prop.c dsl_scrub.c dsl_synctask.c fletcher.c flushwc.c gzip.c lzjb.c metaslab.c refcount.c rprwlock.c rrwlock.c sha256.c spa.c spa_config.c spa_errlog.c spa_history.c spa_misc.c space_map.c txg.c uberblock.c unique.c util.c vdev.c vdev_cache.c vdev_file.c vdev_label.c vdev_mirror.c vdev_missing.c vdev_queue.c vdev_raidz.c vdev_root.c zap.c zap_leaf.c zap_micro.c zfs_byteswap.c zfs_fm.c zfs_fuid.c zfs_znode.c zil.c zio.c zio_checksum.c zio_compress.c zio_inject.c kmem_asprintf.c ddt.c ddt_zap.c zle.c
libzpool_kernel_la_CFLAGS = -I${top_srcdir}/lib/libavl/include \
                            -I${top_srcdir}/lib/libnvpair/include \
                            -I${top_srcdir}/lib/libzfscommon/include \
                            -I${top_srcdir}/lib/libumem/include \
                            -I${top_srcdir}/lib/libsolkerncompat/include \
                            -D_KERNEL @DEBUG_CFLAGS@

noinst_HEADERS = flushwc.h \
                 kmem_asprintf.h \
                 include/sys/zfs_context.h
