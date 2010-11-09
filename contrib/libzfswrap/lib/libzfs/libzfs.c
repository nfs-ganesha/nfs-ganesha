#include "zfs_namecheck.h"
#include "libzfs_impl.h"
#include <sys/spa.h>
#include <sys/dmu_objset.h>

/**
 * Check if the zpool name is valid
 * @param psz_zpool: the zpool name
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, 1 in case of error
 */
static int libzfs_zpool_name_valid(const char *psz_zpool, const char **ppsz_error)
{
        namecheck_err_t why;
        char c_what;
        int i_error;

        if(pool_namecheck(psz_zpool, &why, &c_what))
        {
                switch(why)
                {
                case NAME_ERR_TOOLONG:
                        *ppsz_error = "name is too long";
                        break;
                case NAME_ERR_INVALCHAR:
                        *ppsz_error = "invalid character in pool name";
                        break;
                case NAME_ERR_NOLETTER:
                        *ppsz_error = "name must begin with a letter";
                        break;
                case NAME_ERR_RESERVED:
                        *ppsz_error = "name is reserved";
                        break;
                case NAME_ERR_DISKLIKE:
                        *ppsz_error = "pool name is reserved";
                        break;
                case NAME_ERR_LEADING_SLASH:
                        *ppsz_error = "leading slash in name";
                        break;
                case NAME_ERR_EMPTY_COMPONENT:
                        *ppsz_error = "empty component in name";
                        break;
                case NAME_ERR_TRAILING_SLASH:
                        *ppsz_error = "trailing slash in name";
                        break;
                case NAME_ERR_MULTIPLE_AT:
                        *ppsz_error = "multiple '@' delimiters in name";
                        break;
                default:
                        *ppsz_error = "zpool name invalid";
                }
                return 1;
        }
        return 0;
}

/**
 * Create the zpool
 * @param p_libzfshd: libzfs handle
 * @param psz_zpool: zpool name
 * @param pnv_root: the root tree of vdev
 * @param pnv_props: the tree of properties (can be NULL)
 * @param pnv_fsprops: the tree of the file system properties (can be NULL)
 * @param ppsz_error: the error message if any
 * @return 0 in case of error, the error code overwise
 */
int libzfs_zpool_create(libzfs_handle_t *p_libzfshd, const char* psz_zpool,
                        nvlist_t *pnv_root, nvlist_t *pnv_props,
                        nvlist_t *pnv_fsprops, const char **ppsz_error)
{
        int i_error;
        char *psz_altroot;

        /* Check the zpool name */
        if(libzfs_zpool_name_valid(psz_zpool, ppsz_error))
                return EINVAL;

        /** Check the properties
            TODO: zpool_valid_proplist and zfs_valid_proplist */

        if((i_error = spa_create(psz_zpool, pnv_root, pnv_props, "libzfswrap_zpool_create", pnv_fsprops)))
        {
                switch(i_error)
                {
                case EBUSY:
                        *ppsz_error = "one or more vdevs refer to the same device";
                        break;
                case EOVERFLOW:
                        *ppsz_error = "one or more devices is less than the minimum size (64Mo)";
                        break;
                case ENOSPC:
                        *ppsz_error = "one or more devices is out of space";
                        break;
                case ENOTBLK:
                        *ppsz_error = "cache device must be a disk or disk slice";
                        break;
                case EEXIST:
                        *ppsz_error = "the pool already exist";
                        break;
                default:
                        *ppsz_error = "unable to create the spa";
                }
                return i_error;
        }

        /* If this is an alternate root pool, then automatically set the
           mountpoint to be '/' */
        if(nvlist_lookup_string(pnv_props, zpool_prop_to_name(ZPOOL_PROP_ALTROOT), &psz_altroot) == 0)
        {
                zfs_handle_t *p_zhd;
                assert((p_zhd = zfs_open(p_libzfshd, psz_zpool, ZFS_TYPE_DATASET)) != NULL);
                assert(zfs_prop_set(p_zhd, zfs_prop_to_name(ZFS_PROP_MOUNTPOINT), "/") == 0);
                zfs_close(p_zhd);
        }

        return 0;
}

/**
 * Close the given zpool
 * @param p_zpool: the zpool handle
 */
void libzfs_zpool_close(zpool_handle_t *p_zpool)
{
        if(p_zpool->zpool_config)
                nvlist_free(p_zpool->zpool_config);
        if(p_zpool->zpool_old_config)
                nvlist_free(p_zpool->zpool_old_config);
        if(p_zpool->zpool_props)
                nvlist_free(p_zpool->zpool_props);
        free(p_zpool);
}

/**
 * Open the given zpool
 * @param p_libzfshd: the libzfs handle
 * @param psz_zpool: the zpool name
 * @param ppsz_error: the error message if any
 * @return the zpool handle or NULL in case of error
 */
zpool_handle_t *libzfs_zpool_open_canfail(libzfs_handle_t *p_libzfshd, const char* psz_zpool, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        nvlist_t *pnv_config;
        int i_error;

        /* Check the zpool name */
        if(libzfs_zpool_name_valid(psz_zpool, ppsz_error))
                return NULL;

        if((p_zpool = calloc(1, sizeof(zpool_handle_t))) == NULL)
        {
                *ppsz_error = "no memory";
                return NULL;
        }
        p_zpool->zpool_hdl = p_libzfshd;
        strlcpy(p_zpool->zpool_name, psz_zpool, sizeof(p_zpool->zpool_name));

        i_error = spa_get_stats(psz_zpool, &pnv_config, NULL, 0);
        if(!pnv_config)
        {
                free(p_zpool);
                *ppsz_error = "unable to get the statistics of the zpool";
                return NULL;
        }

        assert(nvlist_size(pnv_config, &p_zpool->zpool_config_size, NV_ENCODE_NATIVE) == 0);

        if(p_zpool->zpool_config)
        {
                uint64_t oldtxg, newtxg;

                assert(nvlist_lookup_uint64(p_zpool->zpool_config, ZPOOL_CONFIG_POOL_TXG, &oldtxg) == 0);
                assert(nvlist_lookup_uint64(pnv_config, ZPOOL_CONFIG_POOL_TXG, &newtxg) == 0);

                if(p_zpool->zpool_old_config)
                        nvlist_free(p_zpool->zpool_old_config);

                if(oldtxg != newtxg)
                {
                        nvlist_free(p_zpool->zpool_config);
                        p_zpool->zpool_old_config = NULL;
                }
                else
                        p_zpool->zpool_old_config = p_zpool->zpool_config;
        }

        p_zpool->zpool_config = pnv_config;
        if(i_error)
                p_zpool->zpool_state = POOL_STATE_UNAVAIL;
        else
                p_zpool->zpool_state = POOL_STATE_ACTIVE;

        return p_zpool;
}

/**
 * Open the given zpool (if the zpool is unavailable, return an error)
 * @param p_libzfshd: the libzfs handle
 * @param psz_zpool: the zpool name
 * @param ppsz_error: the error message if any
 * @return the zpool handle or NULL in case of error
 */
zpool_handle_t *libzfs_zpool_open(libzfs_handle_t *p_libzfshd, const char *psz_pool, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        if(!(p_zpool = libzfs_zpool_open_canfail(p_libzfshd, psz_pool, ppsz_error)))
                return NULL;

        if(p_zpool->zpool_state == POOL_STATE_UNAVAIL)
        {
                *ppsz_error = "cannot open the zpool";
                libzfs_zpool_close(p_zpool);
                return NULL;
        }

        return p_zpool;
}

/**
 * Add a vdev to a given zpool
 * @param psz_name: zpool name
 * @param pnv_root: the root tree
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zpool_vdev_add(const char *psz_name, nvlist_t *pnv_root)
{
        spa_t *p_spa;
        nvlist_t **pnv_l2cache, **pnv_spares;
        uint_t i_l2cache, i_spares;
        int i_error;

        if((i_error = spa_open(psz_name, &p_spa, FTAG)))
                return i_error;

        nvlist_lookup_nvlist_array(pnv_root, ZPOOL_CONFIG_L2CACHE, &pnv_l2cache, &i_l2cache);
        nvlist_lookup_nvlist_array(pnv_root, ZPOOL_CONFIG_SPARES,  &pnv_spares, &i_spares);

         /*
         * A root pool with concatenated devices is not supported.
         * Thus, can not add a device to a root pool.
         *
         * Intent log device can not be added to a rootpool because
         * during mountroot, zil is replayed, a seperated log device
         * can not be accessed during the mountroot time.
         *
         * l2cache and spare devices are ok to be added to a rootpool.
         */
        if(spa_bootfs(p_spa) != 0 && i_l2cache == 0 && i_spares == 0)
        {
                spa_close(p_spa, FTAG);
                return EDOM;
        }

        //TODO: get the error message
        spa_vdev_add(p_spa, pnv_root);
        spa_close(p_spa, FTAG);

        return 0;
}

/**
 * Remove the given vdev from the pool
 * @param p_zpool: the zpool handler
 * @param psz_name: the name of the device to remove
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zpool_vdev_remove(zpool_handle_t *p_zpool, const char *psz_name, const char **ppsz_error)
{
        nvlist_t *pnv_tgt;
        spa_t *p_spa;
        boolean_t avail_spare, l2cache, islog;
        uint64_t guid;
        int i_error;

        if((pnv_tgt = zpool_find_vdev(p_zpool, psz_name,
                                      &avail_spare, &l2cache, &islog)) == 0)
        {
                *ppsz_error = "no vdev corresponding to the one given";
                return ENOENT;
        }

        assert(nvlist_lookup_uint64(pnv_tgt, ZPOOL_CONFIG_GUID, &guid) == 0);

        if((i_error = spa_open(p_zpool->zpool_name, &p_spa, FTAG)))
        {
                *ppsz_error = "unable to open the spa";
                return i_error;
        }
        i_error = spa_vdev_remove(p_spa, guid, B_FALSE);
        spa_close(p_spa, FTAG);

        switch(i_error)
        {
        case 0:
                return 0;
        case ENOTSUP:
                *ppsz_error = "only spares, slogs, and level 2 ARC devices can be removed";
                break;
        case ENOENT:
                *ppsz_error = "no vdev corresponding to the one given";
                break;
        }

        return i_error;
}

/**
 * Attach a vdev to a given zpool
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zpool_vdev_attach(zpool_handle_t *p_zpool, const char *psz_current_dev, nvlist_t *pnv_root, int i_replacing, const char **ppsz_error)
{
        spa_t *p_spa;
        nvlist_t *pnv_tgt;
        boolean_t avail_spare, l2cache;
        uint64_t guid;
        int i_error;

        if((pnv_tgt = zpool_find_vdev(p_zpool, psz_current_dev,
                                      &avail_spare, &l2cache, NULL)) == 0)
        {
                *ppsz_error = "no vdev corresponding to the one given";
                return ENOENT;
        }
        assert(nvlist_lookup_uint64(pnv_tgt, ZPOOL_CONFIG_GUID, &guid) == 0);

        // Do not attach hot spares or L2 cache
        if(avail_spare)
        {
                *ppsz_error = "could not attach hot spares";
                return EINVAL;
        }
        if(l2cache)
        {
                *ppsz_error = "could not attach to a device actually used as a cache";
                return EINVAL;
        }


        if((i_error = spa_open(p_zpool->zpool_name, &p_spa, FTAG)))
                return i_error;

        i_error = spa_vdev_attach(p_spa, guid, pnv_root, i_replacing);
        spa_close(p_spa, FTAG);

        switch(i_error)
        {
        case ENOTSUP:
                *ppsz_error = "can only attach to mirror and top-level disks";
                break;
        case EINVAL:
                *ppsz_error = "new device must be a single disk";
                break;
        case EBUSY:
                *ppsz_error = "the device is busy";
                break;
        case EOVERFLOW:
                *ppsz_error = "devices is too small";
                break;
        case EDOM:
                *ppsz_error = "devices have different sector alignment";
                break;
        default:
                *ppsz_error ="unable to attach the new device";
        }

        return i_error;
}


/**
 * Detach the given vdev from the given pool
 * @param p_zpool: the zpool handler
 * @param psz_device: the device name
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zpool_vdev_detach(zpool_handle_t *p_zpool, const char *psz_device, const char **ppsz_error)
{
        spa_t *p_spa;
        nvlist_t *pnv_tgt;
        boolean_t avail_spare, l2cache;
        uint64_t guid;
        int i_error;

        if((pnv_tgt = zpool_find_vdev(p_zpool, psz_device,
                                      &avail_spare, &l2cache, NULL)) == 0)
        {
                *ppsz_error = "no vdev corresponding to the one given";
                return ENOENT;
        }

        // Do not detach hot spares or L2 cache
        if(avail_spare)
        {
                *ppsz_error = "could not detach hot spares";
                return EINVAL;
        }
        if(l2cache)
        {
                *ppsz_error = "could not detach device actually used as a cache";
                return EINVAL;
        }

        assert(nvlist_lookup_uint64(pnv_tgt, ZPOOL_CONFIG_GUID, &guid) == 0);

        if((i_error = spa_open(p_zpool->zpool_name, &p_spa, FTAG)))
        {
                *ppsz_error = "unable to open the given zpool";
                return i_error;
        }

        if((i_error = spa_vdev_detach(p_spa, guid, 0, 0)))
        {
                switch(i_error)
                {
                case ENOTSUP:
                        *ppsz_error = "'detach' is only applicable to mirror and to replace vdevs";
                        break;
                case EBUSY:
                        *ppsz_error = "the device is actually in use";
                        break;
                default:
                        *ppsz_error = "unable to detach the given vdev";
                }
        }
        spa_close(p_spa, FTAG);

        return i_error;
}

typedef struct config_node {
        char            *cn_name;
        nvlist_t        *cn_config;
        uu_avl_node_t   cn_avl;
} config_node_t;

/**
 * Compare two config_node_t
 * @param a: the first config node
 * @param b: the second config node
 * @return -1, 0 or 1
 */
static int config_node_compare(const void *a, const void *b, void *unused)
{
        int ret;

        const config_node_t *ca = (config_node_t *)a;
        const config_node_t *cb = (config_node_t *)b;

        ret = strcmp(ca->cn_name, cb->cn_name);

        if (ret < 0)
                return -1;
        else if (ret > 0)
                return 1;
        else
                return 0;
}
static int namespace_reload(libzfs_handle_t *p_hdl)
{
        nvlist_t *pnv_config;
        nvpair_t *pnv_elem;
        config_node_t *p_cn;
        void *cookie;

        if(p_hdl->libzfs_ns_gen == 0)
        {
                /*
                 * This is the first time we've accessed the configuration
                 * cache.  Initialize the AVL tree and then fall through to the
                 * common code.
                */
                if(!(p_hdl->libzfs_ns_avlpool =
                        uu_avl_pool_create("config_pool", sizeof (config_node_t),
                                           offsetof(config_node_t, cn_avl),
                                           config_node_compare, UU_DEFAULT)))
			return -1;

                if((p_hdl->libzfs_ns_avl =
                        uu_avl_create(p_hdl->libzfs_ns_avlpool, NULL, UU_DEFAULT)) == NULL)
                        return 1;
	}

        pnv_config = spa_all_configs(&p_hdl->libzfs_ns_gen);
        if(!pnv_config)
                return -1;

        /*
         * Clear out any existing configuration information.
         */
        cookie = NULL;
        while((p_cn = uu_avl_teardown(p_hdl->libzfs_ns_avl, &cookie)) != NULL)
        {
                nvlist_free(p_cn->cn_config);
                free(p_cn->cn_name);
                free(p_cn);
        }

        pnv_elem = NULL;
        while((pnv_elem = nvlist_next_nvpair(pnv_config, pnv_elem)) != NULL)
        {
                nvlist_t *child;
                uu_avl_index_t where;

                if((p_cn = zfs_alloc(p_hdl, sizeof (config_node_t))) == NULL)
                {
                        nvlist_free(pnv_config);
                        return -1;
                }

                if((p_cn->cn_name = zfs_strdup(p_hdl,
		    nvpair_name(pnv_elem))) == NULL) {
                        free(p_cn);
                        nvlist_free(pnv_config);
                        return -1;
                }

		verify(nvpair_value_nvlist(pnv_elem, &child) == 0);
		if (nvlist_dup(child, &p_cn->cn_config, 0) != 0) {
			free(p_cn->cn_name);
			free(p_cn);
			nvlist_free(pnv_config);
			return -1;
		}
		verify(uu_avl_find(p_hdl->libzfs_ns_avl, p_cn, NULL, &where)
		    == NULL);

		uu_avl_insert(p_hdl->libzfs_ns_avl, p_cn, where);
	}

	nvlist_free(pnv_config);
	return 0;
}

/**
 * Iterate over the zpools
 * @param p_libzfshd: the libzfs handle
 * @param func: the function to call for each zpool
 * @param data: anonymous data to pass along to the callback function
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zpool_iter(libzfs_handle_t *p_libzfshd, zpool_iter_f func, void *data, const char **ppsz_error)
{
        config_node_t *p_config_node;

        /*
         * If someone makes a recursive call to zpool_iter(), we want to avoid
         * refreshing the namespace because that will invalidate the parent
         * context.  We allow recursive calls, but simply re-use the same
         * namespace AVL tree.
         */
        if(!p_libzfshd->libzfs_pool_iter && namespace_reload(p_libzfshd))
        {
                *ppsz_error = "unable to reload the namespace";
                return -1;
        }

        p_libzfshd->libzfs_pool_iter++;
        for(p_config_node = uu_avl_first(p_libzfshd->libzfs_ns_avl);
            p_config_node;
            p_config_node = uu_avl_next(p_libzfshd->libzfs_ns_avl, p_config_node))
        {
                zpool_handle_t *p_zpool = libzfs_zpool_open_canfail(p_libzfshd,
                                                p_config_node->cn_name, ppsz_error);

                if(!p_zpool)
                        continue;

                /* Call the callback function: it might return 0 */
                int i_ret = func(p_zpool, data);
                libzfs_zpool_close(p_zpool);
                if(i_ret)
                {
                        *ppsz_error = "error when calling the callback function";
                        p_libzfshd->libzfs_pool_iter--;
                        return i_ret;
                }
        }
        p_libzfshd->libzfs_pool_iter--;

        return 0;
}

/*
 * This function takes the raw DSL properties, and filters out the user-defined
 * properties into a separate nvlist.
 */
static nvlist_t *process_user_props(zfs_handle_t *zhp, nvlist_t *props)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	nvpair_t *elem;
	nvlist_t *propval;
	nvlist_t *nvl;

	if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0) {
		(void) no_memory(hdl);
		return (NULL);
	}

	elem = NULL;
	while ((elem = nvlist_next_nvpair(props, elem)) != NULL) {
		if (!zfs_prop_user(nvpair_name(elem)))
			continue;

		verify(nvpair_value_nvlist(elem, &propval) == 0);
		if (nvlist_add_nvlist(nvl, nvpair_name(elem), propval) != 0) {
			nvlist_free(nvl);
			return (NULL);
		}
	}

	return (nvl);
}


static int libzfs_update_stats(zfs_handle_t *p_zfs)
{
        objset_t *p_os;
        nvlist_t *pnv_allprops, *pnv_userprops;
        int i_error;

        if((i_error = dmu_objset_hold(p_zfs->zfs_name, FTAG, &p_os)))
                return i_error;

        dmu_objset_fast_stat(p_os, &p_zfs->zfs_dmustats);

        if((i_error = dsl_prop_get_all(p_os, &pnv_allprops)) == 0)
        {
                dmu_objset_stats(p_os, pnv_allprops);
                if(!p_zfs->zfs_dmustats.dds_inconsistent)
                {
                        if(dmu_objset_type(p_os) == DMU_OST_ZVOL)
                                assert(zvol_get_stats(p_os, pnv_allprops) == 0);
                }
        }

        dmu_objset_rele(p_os, FTAG);

        // Continue processing the stats
        if((pnv_userprops = process_user_props(p_zfs, pnv_allprops)) == NULL)
        {
                nvlist_free(pnv_allprops);
                return 1;
        }

        nvlist_free(p_zfs->zfs_props);
        nvlist_free(p_zfs->zfs_user_props);

        p_zfs->zfs_props = pnv_allprops;
        p_zfs->zfs_user_props = pnv_userprops;

        return 0;
}

static zpool_handle_t *zpool_add_handle(zfs_handle_t *zhp, const char *pool_name)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph;
        const char *psz_error;

	if ((zph = libzfs_zpool_open_canfail(hdl, pool_name, &psz_error)) != NULL) {
		if (hdl->libzfs_pool_handles != NULL)
			zph->zpool_next = hdl->libzfs_pool_handles;
		hdl->libzfs_pool_handles = zph;
	}
	return (zph);
}

static zpool_handle_t *zpool_find_handle(zfs_handle_t *zhp, const char *pool_name, int len)
{
	libzfs_handle_t *hdl = zhp->zfs_hdl;
	zpool_handle_t *zph = hdl->libzfs_pool_handles;

	while ((zph != NULL) &&
	    (strncmp(pool_name, zpool_get_name(zph), len) != 0))
		zph = zph->zpool_next;
	return (zph);
}

/*
 * Returns a handle to the pool that contains the provided dataset.
 * If a handle to that pool already exists then that handle is returned.
 * Otherwise, a new handle is created and added to the list of handles.
 */
static zpool_handle_t *
zpool_handle(zfs_handle_t *zhp)
{
	char *pool_name;
	int len;
	zpool_handle_t *zph;

	len = strcspn(zhp->zfs_name, "/@") + 1;
	pool_name = zfs_alloc(zhp->zfs_hdl, len);
	(void) strlcpy(pool_name, zhp->zfs_name, len);

	zph = zpool_find_handle(zhp, pool_name, len);
	if (zph == NULL)
		zph = zpool_add_handle(zhp, pool_name);

	free(pool_name);
	return (zph);
}

zfs_handle_t *libzfs_make_dataset_handle(libzfs_handle_t *p_libzfshd, const char *psz_path)
{
        zfs_handle_t *p_zfs = calloc(1, sizeof(zfs_handle_t));
        if(!p_zfs)
                return NULL;

        p_zfs->zfs_hdl = p_libzfshd;
        strlcpy(p_zfs->zfs_name, psz_path, sizeof(p_zfs->zfs_name));

        if(libzfs_update_stats(p_zfs))
        {
                free(p_zfs);
                return NULL;
        }

        // Common part
        switch(p_zfs->zfs_dmustats.dds_type)
        {
        case DMU_OST_ZVOL:
                p_zfs->zfs_head_type = ZFS_TYPE_VOLUME;
                break;
        case DMU_OST_ZFS:
                p_zfs->zfs_head_type = ZFS_TYPE_FILESYSTEM;
                break;
        default:
                assert(0);
        }

        if(p_zfs->zfs_dmustats.dds_is_snapshot)
                p_zfs->zfs_type = ZFS_TYPE_SNAPSHOT;
        else
        {
                switch(p_zfs->zfs_dmustats.dds_type)
                {
                case DMU_OST_ZVOL:
                        p_zfs->zfs_type = ZFS_TYPE_VOLUME;
                        break;
                case DMU_OST_ZFS:
                        p_zfs->zfs_type = ZFS_TYPE_FILESYSTEM;
                        break;
                default:
                        assert(0);
                }
        }

        p_zfs->zpool_hdl = zpool_handle(p_zfs);

        return p_zfs;
}

void libzfs_zfs_close(zfs_handle_t *p_zfs)
{
        if(p_zfs->zfs_mntopts)
                free(p_zfs->zfs_mntopts);
        nvlist_free(p_zfs->zfs_props);
        nvlist_free(p_zfs->zfs_user_props);
        nvlist_free(p_zfs->zfs_recvd_props);
        free(p_zfs);
}

/**
 * Iterate over root datasets
 * @param p_libzfshd: the libzfs handle
 * @param func: the function to call for each zfs
 * @param data: anonymous data to pass along to the callback function
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfs_zfs_iter(libzfs_handle_t *p_libzfshd, zfs_iter_f func, void *data, const char **ppsz_error)
{
        config_node_t *p_cn;
        zfs_handle_t *p_zfs;
        int i_error;

        if(namespace_reload(p_libzfshd))
        {
                *ppsz_error = "Unable to reload the namespace";
                return 1;
        }

        for(p_cn = uu_avl_first(p_libzfshd->libzfs_ns_avl); p_cn;
            p_cn = uu_avl_next(p_libzfshd->libzfs_ns_avl, p_cn))
        {
                if(!(p_zfs = libzfs_make_dataset_handle(p_libzfshd, p_cn->cn_name)))
                {
                        *ppsz_error = "Unable to create the zfs_handle for the zfs object";
                        return 1;
                }
                if((i_error = func(p_zfs, data)))
                {
                        *ppsz_error = "Error in the callback function";
                        libzfs_zfs_close(p_zfs);
                        return i_error;
                }
                libzfs_zfs_close(p_zfs);
        }

        return 0;
}

int libzfs_zfs_validate_name(libzfs_handle_t *hdl, const char *path, int type, boolean_t modifying, const char **ppsz_error)
{
        namecheck_err_t why;
        char what;

        if(dataset_namecheck(path, &why, &what) != 0)
        {
                switch (why)
                {
                case NAME_ERR_TOOLONG:
                        *ppsz_error = "name is too long";
                        break;

                case NAME_ERR_LEADING_SLASH:
                        *ppsz_error = "leading slash in name";
                        break;

                case NAME_ERR_EMPTY_COMPONENT:
                        *ppsz_error = "empty component in name";
                        break;

                case NAME_ERR_TRAILING_SLASH:
                        *ppsz_error = "trailing slash in name";
                        break;

                case NAME_ERR_INVALCHAR:
                        *ppsz_error = "Invalid character in name";
                        break;

                case NAME_ERR_MULTIPLE_AT:
                        *ppsz_error = "multiple '@' delimiters in name";
                        break;

                case NAME_ERR_NOLETTER:
                        *ppsz_error = "pool doesn't begin with a letter";
                        break;

                case NAME_ERR_RESERVED:
                        *ppsz_error = "name is reserved";
                        break;

                case NAME_ERR_DISKLIKE:
                        *ppsz_error = "reserved disk name";
                        break;
                }

                return 0;
        }

        if(!(type & ZFS_TYPE_SNAPSHOT) && strchr(path, '@') != NULL)
        {
                *ppsz_error = "snapshot delimiter '@' in filesystem name";
                return 0;
        }

        if(type == ZFS_TYPE_SNAPSHOT && strchr(path, '@') == NULL)
        {
                *ppsz_error = "missing '@' delimiter in snapshot name";
                return 0;
        }

        if(modifying && strchr(path, '%') != NULL)
        {
                *ppsz_error = "invalid character '%%' in name";
                return 0;
        }

        return -1;
}

/**
 * Open the given snapshot, filesystem or volume.
 * @param p_libzfshd: the libzfs handle
 * @param ppsz_error: the error message if any
 * @return the zfs handle in case of success, NULL overwise
 */
zfs_handle_t *libzfs_zfs_open(libzfs_handle_t *p_libzfshd, const char *psz_path, int type, const char **ppsz_error)
{
        zfs_handle_t *p_zfs;

        /* Validate the name before tryin to open it */
        if(!libzfs_zfs_validate_name(p_libzfshd, psz_path, ZFS_TYPE_DATASET, B_FALSE, ppsz_error))
                return NULL;

        if((p_zfs = libzfs_make_dataset_handle(p_libzfshd, psz_path)) == NULL)
        {
                *ppsz_error = "Unable to create the zfs handle for the zfs object";
                return NULL;
        }

        /* Check the type of the dataset */
        if(!(type & p_zfs->zfs_type))
        {
                *ppsz_error = "The given dataset is not of the right type";
                libzfs_zfs_close(p_zfs);
                return NULL;
        }

        return p_zfs;
}

static zfs_handle_t *libzfs_zfs_snapshot_next(libzfs_handle_t *p_libzfshd, const char *psz_zfs, char *psz_buffer, size_t i_buffer, uint64_t *p_cookie, const char **ppsz_error)
{
        objset_t *p_os;
        size_t i_zfs_len = strlen(psz_zfs);
        int i_error;

        /* Check the size of the zfs name */
        if(i_zfs_len >= i_buffer)
        {
                *ppsz_error = "ZFS name too long to handle snapshots";
                return NULL;
        }

        if(*p_cookie == 0)
                dmu_objset_find(psz_zfs, dmu_objset_prefetch, NULL, DS_FIND_SNAPSHOTS);

top:
        if((i_error = dmu_objset_hold(psz_zfs, FTAG, &p_os)))
        {
                *ppsz_error = "Unable to hold the zfs filesystem";
                return NULL;
        }

        snprintf(psz_buffer, i_buffer, "%s@", psz_zfs);

        if((i_error = dmu_snapshot_list_next(p_os, i_buffer - i_zfs_len - 1,
                                             psz_buffer + i_zfs_len + 1, NULL, p_cookie, NULL)))
                *ppsz_error = "Unable to get the next snapshot";

        dmu_objset_rele(p_os, FTAG);

        zfs_handle_t *p_zfs_snap = NULL;
        if(i_error == 0)
        {
                i_error = dmu_objset_hold(psz_buffer, FTAG, &p_os);
                if(i_error == ENOENT)
                        goto top;

                if(!(p_zfs_snap = libzfs_make_dataset_handle(p_libzfshd, psz_buffer)))
                        *ppsz_error = "Unable to create a zfs handle for the snapshot";
                dmu_objset_rele(p_os, FTAG);
        }
        return p_zfs_snap;
}

/*
 * Iterate over every snapshot of the given zfs
 * @param p_libzfshd: the libzfs handle
 * @param func: the function to call for each zfs
 * @param data: anonymous data to pass along to the callback function
 * @param ppsz_error: the error message if any
 * @return the zfs handle in case of success, NULL overwise
 */
int libzfs_zfs_snapshot_iter(libzfs_handle_t *p_libzfshd, const char *psz_zfs, zfs_iter_f func, void *data, const char **ppsz_error)
{
        zfs_handle_t *p_zfs = libzfs_make_dataset_handle(p_libzfshd, psz_zfs);
        if(!p_zfs)
        {
                *ppsz_error = "Unable to open the zfs file system";
                return ENOENT;
        }

        char psz_buffer[MAXNAMELEN];
        uint64_t i_cookie = 0;
        zfs_handle_t *p_zfs_snap;
        while((p_zfs_snap = libzfs_zfs_snapshot_next(p_libzfshd, psz_zfs, psz_buffer, MAXNAMELEN, &i_cookie, ppsz_error)))
        {
                /* Call the callback function */
                func(p_zfs_snap, data);
                libzfs_zfs_close(p_zfs_snap);
        }

        libzfs_zfs_close(p_zfs);
        return 0;
}
