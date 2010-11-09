#include <umem.h>
#include <libsolkerncompat.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/systm.h>
#include <libzfs.h>
#include <libzfs_impl.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/mode.h>
#include <sys/fcntl.h>

#include <limits.h>

#include "zfs_ioctl.h"
#include <ctype.h>

#include "libzfswrap.h"
#include "libzfswrap_utils.h"

extern int zfs_vfsinit(int fstype, char *name);

static int getattr_helper(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, uint64_t *p_gen, int *p_type);

/**
 * Initialize the libzfswrap library
 * @return a handle to the library, NULL in case of error
 */
libzfswrap_handle_t *libzfswrap_init()
{
        // Create the cache directory if it does not exist
        mkdir(ZPOOL_CACHE_DIR, 0700);

        init_mmap();
        libsolkerncompat_init();
        zfs_vfsinit(zfstype, NULL);
        zfs_ioctl_init();
        libzfs_handle_t *p_zhd = libzfs_init();

        if(!p_zhd)
                libsolkerncompat_exit();

        return (libzfswrap_handle_t*)p_zhd;
}

/**
 * Uninitialize the library
 * @param p_zhd: the libzfswrap handle
 */
void libzfswrap_exit(libzfswrap_handle_t *p_zhd)
{
        libzfs_fini((libzfs_handle_t*)p_zhd);
        libsolkerncompat_exit();
}

/**
 * Create a zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_name: the name of the zpool
 * @param psz_type: type of the zpool (mirror, raidz, raidz([1,255])
 * @param ppsz_error: the error message (if any)
 * @return 0 on success, the error code overwise
 */
int libzfswrap_zpool_create(libzfswrap_handle_t *p_zhd, const char *psz_name, const char *psz_type,
                            const char **ppsz_dev, size_t i_dev, const char **ppsz_error)
{
        int i_error;
        nvlist_t *pnv_root    = NULL;
        nvlist_t *pnv_fsprops = NULL;
        nvlist_t *pnv_props   = NULL;

        // Create the zpool
        if(!(pnv_root = lzwu_make_root_vdev(psz_type, ppsz_dev, i_dev, ppsz_error)))
                return 1;

        i_error = libzfs_zpool_create((libzfs_handle_t*)p_zhd, psz_name, pnv_root,
                                      pnv_props, pnv_fsprops, ppsz_error);

        nvlist_free(pnv_props);
        nvlist_free(pnv_fsprops);
        nvlist_free(pnv_root);
        return i_error;
}

/**
 * Destroy the given zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_name: zpool name
 * @param b_force: force the unmount process or not
 * @param ppsz_error: the error message (if any)
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_destroy(libzfswrap_handle_t *p_zhd, const char *psz_name, int b_force, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        int i_error;

        /** Open the zpool */
        if((p_zpool = libzfs_zpool_open_canfail((libzfs_handle_t*)p_zhd, psz_name, ppsz_error)) == NULL)
        {
                /** If the name contain a '/' redirect the user to zfs_destroy */
                if(strchr(psz_name, '/') != NULL)
                        *ppsz_error = "the pool name cannot contain a '/'";
                return 1;
        }

        i_error = spa_destroy((char*)psz_name);
        libzfs_zpool_close(p_zpool);

        return i_error;
}

/**
 * Add to the given zpool the following device
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_type: type of the device group to add
 * @param ppsz_dev: the list of devices
 * @param i_dev: the number of devices
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_add(libzfswrap_handle_t *p_zhd, const char *psz_zpool, const char *psz_type, const char **ppsz_dev, size_t i_dev, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        nvlist_t *pnv_root;
        int i_error;

        if(!(p_zpool = libzfs_zpool_open((libzfs_handle_t*)p_zhd, psz_zpool, ppsz_error)))
                return 1;

        if(!(pnv_root = lzwu_make_root_vdev(psz_type, ppsz_dev, i_dev, ppsz_error)))
        {
                libzfs_zpool_close(p_zpool);
                return 2;
        }

        i_error = libzfs_zpool_vdev_add(psz_zpool, pnv_root);

        nvlist_free(pnv_root);
        libzfs_zpool_close(p_zpool);

        return i_error;
}

/**
 * Remove the given vdevs from the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param ppsz_vdevs: the vdevs
 * @param i_vdevs: the number of vdevs
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_remove(libzfswrap_handle_t *p_zhd, const char *psz_zpool, const char **ppsz_dev, size_t i_vdevs, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        size_t i;
        int i_error;

        if(!(p_zpool = libzfs_zpool_open((libzfs_handle_t*)p_zhd, psz_zpool, ppsz_error)))
                return 1;

        for(i = 0; i < i_vdevs; i++)
        {
                if((i_error = libzfs_zpool_vdev_remove(p_zpool, ppsz_dev[i], ppsz_error)))
                        break;
        }

        libzfs_zpool_close(p_zpool);

        return i_error;
}

/**
 * Attach the given device to the given vdev in the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_current_dev: the device to use as an attachment point
 * @param psz_new_dev: the device to attach
 * @param i_replacing: replacing the device ?
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_attach(libzfswrap_handle_t *p_zhd, const char *psz_zpool, const char *psz_current_dev, const char *psz_new_dev, int i_replacing, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        nvlist_t *pnv_root;
        int i_error;

        if(!(p_zpool = libzfs_zpool_open((libzfs_handle_t*)p_zhd, psz_zpool, ppsz_error)))
                return 1;

        if(!(pnv_root = lzwu_make_root_vdev("", &psz_new_dev, 1, ppsz_error)))
        {
                libzfs_zpool_close(p_zpool);
                return 2;
        }

        i_error = libzfs_zpool_vdev_attach(p_zpool, psz_current_dev, pnv_root, i_replacing, ppsz_error);

        nvlist_free(pnv_root);
        libzfs_zpool_close(p_zpool);

        return i_error;
}

/**
 * Detach the given vdevs from the zpool
 * @param p_zhd: the libzfswrap handle
 * @param psz_zpool: the zpool name
 * @param psz_dev: the device to detach
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_detach(libzfswrap_handle_t *p_zhd, const char *psz_zpool, const char *psz_dev, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        int i_error;

        if(!(p_zpool = libzfs_zpool_open((libzfs_handle_t*)p_zhd, psz_zpool, ppsz_error)))
                return 1;

        i_error = libzfs_zpool_vdev_detach(p_zpool, psz_dev, ppsz_error);

        libzfs_zpool_close(p_zpool);
        return i_error;
}

/**
 * Callback called for each pool, that print the information
 * @param p_zpool: a pointer to the current zpool
 * @param p_data: a obscure data pointer (the zpool property list)
 * @return 0
 */
static int libzfswrap_zpool_list_callback(zpool_handle_t *p_zpool, void *p_data)
{
        zprop_list_t *p_zpl = (zprop_list_t*)p_data;
        char property[ZPOOL_MAXPROPLEN];
        char *psz_prop;
        boolean_t first = B_TRUE;

        for(; p_zpl; p_zpl = p_zpl->pl_next)
        {
                boolean_t right_justify = B_FALSE;
                if(first)
                        first = B_FALSE;
                else
                        printf("  ");

                if(p_zpl->pl_prop != ZPROP_INVAL)
                {
                        if(zpool_get_prop(p_zpool, p_zpl->pl_prop, property, sizeof(property), NULL))
                                psz_prop = "-";
                        else
                                psz_prop = property;
                        right_justify = zpool_prop_align_right(p_zpl->pl_prop);
                }
                else
                        psz_prop = "-";

                // Print the string
                if(p_zpl->pl_next == NULL && !right_justify)
                        printf("%s", psz_prop);
                else if(right_justify)
                        printf("%*s", (int)p_zpl->pl_width, psz_prop);
                else
                        printf("%-*s", (int)p_zpl->pl_width, psz_prop);
        }
        printf("\n");

        return 0;
}

/**
 * List the available zpools
 * @param p_zhd: the libzfswrap handle
 * @param psz_props: the properties to retrieve
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_list(libzfswrap_handle_t *p_zhd, const char *psz_props, const char **ppsz_error)
{
        zprop_list_t *p_zprop_list = NULL;
        static char psz_default_props[] = "name,size,allocated,free,capacity,dedupratio,health,altroot";
        if(zprop_get_list((libzfs_handle_t*)p_zhd, psz_props ? psz_props : psz_default_props,
                          &p_zprop_list, ZFS_TYPE_POOL))
        {
                *ppsz_error = "unable to get the list of properties";
                return 1;
        }

        lzwu_zpool_print_list_header(p_zprop_list);
        libzfs_zpool_iter((libzfs_handle_t*)p_zhd, libzfswrap_zpool_list_callback, p_zprop_list);
        zprop_free_list(p_zprop_list);

        return 0;
}

static int libzfswrap_zpool_status_callback(zpool_handle_t *zhp, void *data)
{
        status_cbdata_t *cbp = data;
        nvlist_t *config, *nvroot;
        char *msgid;
        int reason;
        const char *health;
        uint_t c;
        vdev_stat_t *vs;

        config = zpool_get_config(zhp, NULL);
        reason = zpool_get_status(zhp, &msgid);
        cbp->cb_count++;

        /*
         * If we were given 'zpool status -x', only report those pools with
         * problems.
         */
        if(reason == ZPOOL_STATUS_OK && cbp->cb_explain)
        {
                if(!cbp->cb_allpools)
                {
                        printf("pool '%s' is healthy\n", zpool_get_name(zhp));
                        if(cbp->cb_first)
                                cbp->cb_first = B_FALSE;
                }
                return 0;
        }

        if (cbp->cb_first)
                cbp->cb_first = B_FALSE;
        else
                printf("\n");

        assert(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvroot) == 0);
        assert(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_STATS,
               (uint64_t **)&vs, &c) == 0);
        health = zpool_state_to_name(vs->vs_state, vs->vs_aux);

        printf("  pool: %s\n", zpool_get_name(zhp));
        printf(" state: %s\n", health);

        switch (reason)
        {
        case ZPOOL_STATUS_MISSING_DEV_R:
                printf("status: One or more devices could not be opened. "
                       "Sufficient replicas exist for\n\tthe pool to "
                       "continue functioning in a degraded state.\n");
                printf("action: Attach the missing device and "
                       "online it using 'zpool online'.\n");
                break;

        case ZPOOL_STATUS_MISSING_DEV_NR:
                printf("status: One or more devices could not "
                       "be opened.  There are insufficient\n\treplicas for the "
                       "pool to continue functioning.\n");
                printf("action: Attach the missing device and "
                       "online it using 'zpool online'.\n");
                break;

        case ZPOOL_STATUS_CORRUPT_LABEL_R:
                printf("status: One or more devices could not "
                       "be used because the label is missing or\n\tinvalid.  "
                       "Sufficient replicas exist for the pool to continue\n\t"
                       "functioning in a degraded state.\n");
                printf("action: Replace the device using 'zpool replace'.\n");
                break;

        case ZPOOL_STATUS_CORRUPT_LABEL_NR:
                printf("status: One or more devices could not "
                       "be used because the label is missing \n\tor invalid.  "
                       "There are insufficient replicas for the pool to "
                       "continue\n\tfunctioning.\n");
                zpool_explain_recover(zpool_get_handle(zhp),
                    zpool_get_name(zhp), reason, config);
                break;

        case ZPOOL_STATUS_FAILING_DEV:
                printf("status: One or more devices has "
                       "experienced an unrecoverable error.  An\n\tattempt was "
                       "made to correct the error.  Applications are "
                       "unaffected.\n");
                printf("action: Determine if the device needs "
                       "to be replaced, and clear the errors\n\tusing "
                       "'zpool clear' or replace the device with 'zpool "
                       "replace'.\n");
                break;

        case ZPOOL_STATUS_OFFLINE_DEV:
                printf("status: One or more devices has "
                       "been taken offline by the administrator.\n\tSufficient "
                       "replicas exist for the pool to continue functioning in "
                       "a\n\tdegraded state.\n");
                printf("action: Online the device using "
                       "'zpool online' or replace the device with\n\t'zpool "
                       "replace'.\n");
                break;

        case ZPOOL_STATUS_REMOVED_DEV:
                printf("status: One or more devices has "
                       "been removed by the administrator.\n\tSufficient "
                       "replicas exist for the pool to continue functioning in "
                       "a\n\tdegraded state.\n");
                printf("action: Online the device using "
                       "'zpool online' or replace the device with\n\t'zpool "
                       "replace'.\n");
                break;


        case ZPOOL_STATUS_RESILVERING:
                printf("status: One or more devices is "
                       "currently being resilvered.  The pool will\n\tcontinue "
                       "to function, possibly in a degraded state.\n");
                printf("action: Wait for the resilver to complete.\n");
                break;

        case ZPOOL_STATUS_CORRUPT_DATA:
                printf("status: One or more devices has "
                       "experienced an error resulting in data\n\tcorruption.  "
                       "Applications may be affected.\n");
                printf("action: Restore the file in question "
                       "if possible.  Otherwise restore the\n\tentire pool from "
                       "backup.\n");
                break;

        case ZPOOL_STATUS_CORRUPT_POOL:
                printf("status: The pool metadata is corrupted "
                       "and the pool cannot be opened.\n");
                zpool_explain_recover(zpool_get_handle(zhp),
                    zpool_get_name(zhp), reason, config);
                break;

        case ZPOOL_STATUS_VERSION_OLDER:
                printf("status: The pool is formatted using an "
                       "older on-disk format.  The pool can\n\tstill be used, but "
                       "some features are unavailable.\n");
                printf("action: Upgrade the pool using 'zpool "
                       "upgrade'.  Once this is done, the\n\tpool will no longer "
                       "be accessible on older software versions.\n");
                break;

        case ZPOOL_STATUS_VERSION_NEWER:
                printf("status: The pool has been upgraded to a "
                       "newer, incompatible on-disk version.\n\tThe pool cannot "
                       "be accessed on this system.\n");
                printf("action: Access the pool from a system "
                       "running more recent software, or\n\trestore the pool from "
                       "backup.\n");
                break;

        case ZPOOL_STATUS_FAULTED_DEV_R:
                printf("status: One or more devices are "
                       "faulted in response to persistent errors.\n\tSufficient "
                       "replicas exist for the pool to continue functioning "
                       "in a\n\tdegraded state.\n");
                printf("action: Replace the faulted device, "
                       "or use 'zpool clear' to mark the device\n\trepaired.\n");
                break;

        case ZPOOL_STATUS_FAULTED_DEV_NR:
                printf("status: One or more devices are "
                       "faulted in response to persistent errors.  There are "
                       "insufficient replicas for the pool to\n\tcontinue "
                       "functioning.\n");
                printf("action: Destroy and re-create the pool "
                       "from a backup source.  Manually marking the device\n"
                       "\trepaired using 'zpool clear' may allow some data "
                       "to be recovered.\n");
                break;

        case ZPOOL_STATUS_IO_FAILURE_WAIT:
        case ZPOOL_STATUS_IO_FAILURE_CONTINUE:
                printf("status: One or more devices are "
                       "faulted in response to IO failures.\n");
                printf("action: Make sure the affected devices "
                       "are connected, then run 'zpool clear'.\n");
                break;

        case ZPOOL_STATUS_BAD_LOG:
                printf("status: An intent log record "
                       "could not be read.\n"
                       "\tWaiting for adminstrator intervention to fix the "
                       "faulted pool.\n");
                printf("action: Either restore the affected "
                       "device(s) and run 'zpool online',\n"
                       "\tor ignore the intent log records by running "
                       "'zpool clear'.\n");
                break;

        default:
                /*
                 * The remaining errors can't actually be generated, yet.
                 */
                assert(reason == ZPOOL_STATUS_OK);
        }

        if(msgid != NULL)
                printf("   see: http://www.sun.com/msg/%s\n", msgid);

        if(config != NULL)
        {
                int namewidth;
                uint64_t nerr;
                nvlist_t **spares, **l2cache;
                uint_t nspares, nl2cache;


                printf(" scrub: ");
                lzwu_zpool_print_scrub_status(nvroot);

                namewidth = lzwu_zpool_max_width(cbp->p_zhd, zhp, nvroot, 0, 0);
                if(namewidth < 10)
                        namewidth = 10;

                printf("config:\n\n");
                printf("\t%-*s  %-8s %5s %5s %5s\n", namewidth,
                       "NAME", "STATE", "READ", "WRITE", "CKSUM");
                lzwu_zpool_print_status_config(cbp->p_zhd, zhp, zpool_get_name(zhp), nvroot, namewidth, 0, B_FALSE);
                if(lzwu_num_logs(nvroot) > 0)
                        lzwu_print_logs(cbp->p_zhd, zhp, nvroot, namewidth, B_TRUE);
                if(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
                   &l2cache, &nl2cache) == 0)
                        lzwu_print_l2cache(cbp->p_zhd, zhp, l2cache, nl2cache, namewidth);

                if(nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
                   &spares, &nspares) == 0)
                        lzwu_print_spares(cbp->p_zhd, zhp, spares, nspares, namewidth);

                if(nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT, &nerr) == 0)
                {
                        nvlist_t *nverrlist = NULL;

                        /*
                         * If the approximate error count is small, get a
                         * precise count by fetching the entire log and
                         * uniquifying the results.
                         */
                        if (nerr > 0 && nerr < 100 && !cbp->cb_verbose &&
                            zpool_get_errlog(zhp, &nverrlist) == 0) {
                                nvpair_t *elem;

                                elem = NULL;
                                nerr = 0;
                                while ((elem = nvlist_next_nvpair(nverrlist,
                                    elem)) != NULL) {
                                        nerr++;
                                }
                        }
                        nvlist_free(nverrlist);

                        printf("\n");

                        if(nerr == 0)
                                printf("errors: No known data errors\n");
                        else if (!cbp->cb_verbose)
                                printf("errors: %llu data errors, use '-v' for a list\n",
                                       (u_longlong_t)nerr);
                        else
                                lzwu_print_error_log(zhp);
                }

                if(cbp->cb_dedup_stats)
                        lzwu_print_dedup_stats(config);
        }
        else
        {
                printf("config: The configuration cannot be determined.\n");
        }
        return (0);
}

/**
 * Print the status of the available zpools
 * @param p_zhd: the libzfswrap handle
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zpool_status(libzfswrap_handle_t *p_zhd, const char **ppsz_error)
{
        status_cbdata_t cb_data;
        cb_data.cb_count = 0;
        cb_data.cb_allpools = B_FALSE;
        cb_data.cb_verbose = B_FALSE;
        cb_data.cb_explain = B_FALSE;
        cb_data.cb_first = B_TRUE;
        cb_data.cb_dedup_stats = B_FALSE;
        cb_data.p_zhd = (libzfs_handle_t*)p_zhd;

        libzfs_zpool_iter((libzfs_handle_t*)p_zhd, libzfswrap_zpool_status_callback, &cb_data);

        return 0;
}

static int libzfswrap_zfs_list_callback(zfs_handle_t *p_zfs, void *data)
{
        zprop_list_t *pl = (zprop_list_t*)data;

        boolean_t first = B_TRUE;
        char property[ZFS_MAXPROPLEN];
        nvlist_t *userprops = zfs_get_user_props(p_zfs);
        nvlist_t *propval;
        char *propstr;
        boolean_t right_justify;
        int width;

        for(; pl != NULL; pl = pl->pl_next)
        {
                if(!first)
                        printf("  ");
                else
                        first = B_FALSE;

                if(pl->pl_prop != ZPROP_INVAL)
                {
                        if(zfs_prop_get(p_zfs, pl->pl_prop, property,
                            sizeof (property), NULL, NULL, 0, B_FALSE) != 0)
                                propstr = "-";
                        else
                                propstr = property;

                        right_justify = zfs_prop_align_right(pl->pl_prop);
                }
                else if(zfs_prop_userquota(pl->pl_user_prop))
                {
                        if(zfs_prop_get_userquota(p_zfs, pl->pl_user_prop,
                            property, sizeof (property), B_FALSE) != 0)
                                propstr = "-";
                        else
                                propstr = property;
                        right_justify = B_TRUE;
                }
                else
                {
                        if(nvlist_lookup_nvlist(userprops,
                            pl->pl_user_prop, &propval) != 0)
                                propstr = "-";
                        else
                                verify(nvlist_lookup_string(propval,
                                    ZPROP_VALUE, &propstr) == 0);
                        right_justify = B_FALSE;
                }

                width = pl->pl_width;

                /*
                 * If this is being called in scripted mode, or if this is the
                 * last column and it is left-justified, don't include a width
                 * format specifier.
                 */
                if((pl->pl_next == NULL && !right_justify))
                        printf("%s", propstr);
                else if(right_justify)
                        printf("%*s", width, propstr);
                else
                        printf("%-*s", width, propstr);
        }

        printf("\n");

        return 0;
}

/**
 * Print the list of ZFS file systems and properties
 * @param p_zhd: the libzfswrap handle
 * @param psz_props: the properties to retrieve
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_list(libzfswrap_handle_t *p_zhd, const char *psz_props, const char **ppsz_error)
{
        zprop_list_t *p_zprop_list = NULL;
        static char psz_default_props[] = "name,used,available,referenced,mountpoint";
        if(zprop_get_list((libzfs_handle_t*)p_zhd, psz_props ? psz_props : psz_default_props,
                          &p_zprop_list, ZFS_TYPE_DATASET))
        {
                *ppsz_error = "Unable to get the list of properties";
                return 1;
        }

        lzwu_zfs_print_list_header(p_zprop_list);
        libzfs_zfs_iter((libzfs_handle_t*)p_zhd, libzfswrap_zfs_list_callback, p_zprop_list, ppsz_error );
        zprop_free_list(p_zprop_list);

        return 0;
}

/**
 * Create a snapshot of the given ZFS file system
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param psz_snapshot: name of the snapshot
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_snapshot(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char *psz_snapshot, const char **ppsz_error)
{
        zfs_handle_t *p_zfs;
        int i_error;

        /**@TODO: check the name of the filesystem and snapshot*/

        if(!(p_zfs = libzfs_zfs_open((libzfs_handle_t*)p_zhd, psz_zfs, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME, ppsz_error)))
                return ENOENT;

        if((i_error = dmu_objset_snapshot(p_zfs->zfs_name, (char*)psz_snapshot, NULL, 0)))
                *ppsz_error = "Unable to create the snapshot";

        libzfs_zfs_close(p_zfs);
        return i_error;
}

/**
 * Destroy a snapshot of the given ZFS file system
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param psz_snapshot: name of the snapshot
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_snapshot_destroy(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char *psz_snapshot, const char **ppsz_error)
{
        zpool_handle_t *p_zpool;
        int i_error;

        /** Open the zpool */
        if((p_zpool = libzfs_zpool_open_canfail((libzfs_handle_t*)p_zhd, psz_zfs, ppsz_error)) == NULL)
        {
                /** If the name contain a '/' redirect the user to zfs_destroy */
                if(strchr(psz_zfs, '/') != NULL)
                        *ppsz_error = "the pool name cannot contain a '/'";
                return 1;
        }

        if((i_error = dmu_snapshots_destroy(psz_zfs, psz_snapshot, B_TRUE)))
                *ppsz_error = "Unable to destro the snapshot";

        libzfs_zpool_close(p_zpool);
        return i_error;
}


/**
 * List the available snapshots for the given zfs
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param ppsz_error: the error message if any
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_zfs_list_snapshot(libzfswrap_handle_t *p_zhd, const char *psz_zfs, const char **ppsz_error)
{
        zprop_list_t *p_zprop_list = NULL;
        static char psz_default_props[] = "name,used,available,referenced,mountpoint";
        if(zprop_get_list((libzfs_handle_t*)p_zhd, psz_default_props, &p_zprop_list, ZFS_TYPE_DATASET))
        {
                *ppsz_error = "Unable to get the list of properties";
                return 1;
        }

        lzwu_zfs_print_list_header(p_zprop_list);

        return libzfs_zfs_snapshot_iter((libzfs_handle_t*)p_zhd, psz_zfs, libzfswrap_zfs_list_callback, p_zprop_list, ppsz_error);
}

typedef struct
{
        char **ppsz_names;
        size_t i_num;
}callback_data_t;

static int libzfswrap_zfs_get_list_snapshots_callback(zfs_handle_t *p_zfs, void *data)
{
        callback_data_t *p_cb = (callback_data_t*)data;
        p_cb->i_num++;
        p_cb->ppsz_names = realloc(p_cb->ppsz_names, p_cb->i_num*sizeof(char*));
        p_cb->ppsz_names[p_cb->i_num-1] = strdup(p_zfs->zfs_name);
        return 0;
}

/**
 * Return the list of snapshots for the given zfs in an array of strings
 * @param p_zhd: the libzfswrap handle
 * @param psz_zfs: name of the file system
 * @param pppsz_snapshots: the array of snapshots names
 * @param ppsz_error: the error message if any
 * @return the number of snapshots in case of success, -1 overwise
 */
int libzfswrap_zfs_get_list_snapshots(libzfswrap_handle_t *p_zhd, const char *psz_zfs, char ***pppsz_snapshots, const char **ppsz_error)
{
        callback_data_t cb = { .ppsz_names = NULL, .i_num = 0 };
        if(libzfs_zfs_snapshot_iter((libzfs_handle_t*)p_zhd, psz_zfs,
                                    libzfswrap_zfs_get_list_snapshots_callback,
                                    &cb, ppsz_error))
                return -1;

        *pppsz_snapshots = cb.ppsz_names;
        return cb.i_num;
}

extern vfsops_t *zfs_vfsops;
/**
 * Mount the given file system
 * @param psz_zpool: the pool to mount
 * @param psz_dir: the directory to mount
 * @param psz_options: options for the mounting point
 * @return the vitual file system
 */
libzfswrap_vfs_t *libzfswrap_mount(const char *psz_zpool, const char *psz_dir, const char *psz_options)
{
        vfs_t *p_vfs = calloc(1, sizeof(vfs_t));
        if(!p_vfs)
                return NULL;

        VFS_INIT(p_vfs, zfs_vfsops, 0);
        VFS_HOLD(p_vfs);

        struct mounta uap = {
        .spec = (char*)psz_zpool,
        .dir = (char*)psz_dir,
        .flags = 0 | MS_SYSSPACE,
        .fstype = "zfs-ganesha",
        .dataptr = "",
        .datalen = 0,
        .optptr = (char*)psz_options,
        .optlen = strlen(psz_options)
        };

        cred_t cred = { .cr_uid = 0, .cr_gid = 0 };
        int i_error = VFS_MOUNT(p_vfs, rootdir, &uap, &cred);
        if(i_error)
        {
                free(p_vfs);
                return NULL;
        }
        return (libzfswrap_vfs_t*)p_vfs;
}

/**
 * Get the root object of a file system
 * @param p_vfs: the virtual filesystem
 * @param p_root: return the root object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getroot(libzfswrap_vfs_t *p_vfs, inogen_t *p_root)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, 3, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode != NULL);
        // Get the generation
        p_root->inode = 3;
        p_root->generation = p_znode->z_phys->zp_gen;

        VN_RELE(ZTOV(p_znode));
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Unmount the given file system
 * @param p_vfs: the virtual file system
 * @param b_force: force the unmount ?
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_umount(libzfswrap_vfs_t *p_vfs, int b_force)
{
        int i_error;
        cred_t cred = { .cr_uid = 0, .cr_gid = 0 };

        VFS_SYNC((vfs_t*)p_vfs, 0, &cred);
        if((i_error = VFS_UNMOUNT((vfs_t*)p_vfs, b_force ? MS_FORCE : 0, &cred)))
        {
                return i_error;
        }

        assert(b_force || ((vfs_t*)p_vfs)->vfs_count == 1);
        return 0;
}



/**
 * Get some more informations about the file system
 * @param p_vfs: the virtual file system
 * @param p_stats: the statistics
 * @return 0 in case of success, -1 overwise
 */
int libzfswrap_statfs(libzfswrap_vfs_t *p_vfs, struct statvfs *p_statvfs)
{
        //FIXME: no ZFS_ENTER ??
        struct statvfs64 zfs_stats = { 0 };
        int i_error;
        if((i_error = VFS_STATVFS((vfs_t*)p_vfs, &zfs_stats)))
                return i_error;

        p_statvfs->f_bsize = zfs_stats.f_frsize;
        p_statvfs->f_frsize = zfs_stats.f_frsize;
        p_statvfs->f_blocks = zfs_stats.f_blocks;
        p_statvfs->f_bfree = zfs_stats.f_bfree;
        p_statvfs->f_bavail = zfs_stats.f_bavail;
        p_statvfs->f_files = zfs_stats.f_files;
        p_statvfs->f_ffree = zfs_stats.f_ffree;
        p_statvfs->f_favail = zfs_stats.f_favail;
        p_statvfs->f_fsid = zfs_stats.f_fsid;
        p_statvfs->f_flag = zfs_stats.f_flag;
        p_statvfs->f_namemax = zfs_stats.f_namemax;

        return 0;
}

/**
 * Lookup for a given file in the given directory
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent file object
 * @param psz_name: filename
 * @param p_object: return the object node and generation
 * @param p_type: return the object type
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_lookup(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, inogen_t *p_object, int *p_type)
{
        if(strlen(psz_name) >= MAXNAMELEN)
                return -1;

        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_parent_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);

        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        ASSERT(p_parent_znode != NULL);
        // Check the parent generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vnode_t *p_vnode = NULL;
        if((i_error = VOP_LOOKUP(p_parent_vnode, (char*)psz_name, &p_vnode, NULL, 0, NULL, (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        p_object->inode = VTOZ(p_vnode)->z_id;
        p_object->generation = VTOZ(p_vnode)->z_phys->zp_gen;
        *p_type = VTTOIF(p_vnode->v_type);

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return 0;
}

/**
 * Test the access right of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param mask: the rights to check
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_access(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int mask)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        int mode = 0;
        if(mask & R_OK)
                mode |= VREAD;
        if(mask & W_OK)
                mode |= VWRITE;
        if(mask & X_OK)
                mode |= VEXEC;

        i_error = VOP_ACCESS(p_vnode, mode, 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Open the given object
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object to open
 * @param i_flags: the opening flags
 * @param pp_vnode: the virtual node
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_open(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, int i_flags, libzfswrap_vnode_t **pp_vnode)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int mode = 0, flags = 0, i_error;
        lzwu_flags2zfs(i_flags, &flags, &mode);

        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        vnode_t *p_old_vnode = p_vnode;

        // Check errors
        if((i_error = VOP_OPEN(&p_vnode, flags, (cred_t*)p_cred, NULL)))
        {
                //FIXME: memleak ?
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_old_vnode == p_vnode);

        ZFS_EXIT(p_zfsvfs);
        *pp_vnode = (libzfswrap_vnode_t*)p_vnode;
        return 0;
}

/**
 * Create the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent object
 * @param psz_filename: the file name
 * @param mode: the file mode
 * @param p_file: return the file
 * @return 0 in case of success the error code overwise
 */
int libzfswrap_create(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, mode_t mode, inogen_t *p_file)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vattr_t vattr = { 0 };
        vattr.va_type = VREG;
        vattr.va_mode = mode;
        vattr.va_mask = AT_TYPE | AT_MODE;

        vnode_t *p_new_vnode;

        if((i_error = VOP_CREATE(p_parent_vnode, (char*)psz_filename, &vattr, NONEXCL, mode, &p_new_vnode, (cred_t*)p_cred, 0, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        p_file->inode = VTOZ(p_new_vnode)->z_id;
        p_file->generation = VTOZ(p_new_vnode)->z_phys->zp_gen;

        VN_RELE(p_new_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Open a directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param directory: the directory to open
 * @param pp_vnode: the vnode to return
 * @return 0 on success, the error code overwise
 */
int libzfswrap_opendir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t directory, libzfswrap_vnode_t **pp_vnode)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, directory.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != directory.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        // Check that we have a directory
        if(p_vnode->v_type != VDIR)
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return ENOTDIR;
        }

        vnode_t *p_old_vnode = p_vnode;
        if((i_error = VOP_OPEN(&p_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_old_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_old_vnode == p_vnode);

        ZFS_EXIT(p_zfsvfs);
        *pp_vnode = (libzfswrap_vnode_t*)p_vnode;
        return 0;
}

/**
 * Read the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_entries: the array of entries to fill
 * @param size: the array size
 * @param cookie: the offset to read in the directory
 * @return 0 on success, the error code overwise
 */
int libzfswrap_readdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, libzfswrap_entry_t *p_entries, size_t size, off_t *cookie)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;

        // Check that the vnode is a directory
        if(((vnode_t*)p_vnode)->v_type != VDIR)
                return ENOTDIR;

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        off_t next_entry = *cookie;
        int eofp = 0;
        union {
                char buf[DIRENT64_RECLEN(MAXNAMELEN)];
                struct dirent64 dirent;
        } entry;

        ZFS_ENTER(p_zfsvfs);
        size_t index = 0;
        while(index < size)
        {
                iovec.iov_base = entry.buf;
                iovec.iov_len = sizeof(entry.buf);
                uio.uio_resid = iovec.iov_len;
                uio.uio_loffset = next_entry;

                /* TODO: do only one call for more than one entry ? */
                if(VOP_READDIR((vnode_t*)p_vnode, &uio, (cred_t*)p_cred, &eofp, NULL, 0))
                        break;

                // End of directory ?
                if(iovec.iov_base == entry.buf)
                        break;

                // Copy the entry name
                strcpy(p_entries[index].psz_filename, entry.dirent.d_name);
                p_entries[index].object.inode = entry.dirent.d_ino;
                getattr_helper(p_vfs, p_cred, p_entries[index].object, &(p_entries[index].stats), &(p_entries[index].object.generation), &(p_entries[index].type));

                // Go to the next entry
                next_entry = entry.dirent.d_off;
                index++;
        }
        ZFS_EXIT(p_zfsvfs);

        // Set the last element to NULL if we end before size elements
        if(index < size)
        {
                p_entries[index].psz_filename[0] = '\0';
                *cookie = 0;
        }
        else
                *cookie = next_entry;

        return 0;
}

/**
 * Close the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @return 0 on success, the error code overwise
 */
int libzfswrap_closedir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode)
{
        return libzfswrap_close(p_vfs, p_cred, p_vnode, O_RDONLY);
}

/**
 * Get the stat about a file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_stat: the stat struct to fill in
 * @return 0 on success, the error code overwise
 */
int libzfswrap_stat(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, struct stat *p_stat)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        vattr_t vattr;
        vattr.va_mask = AT_ALL;
        memset(p_stat, 0, sizeof(*p_stat));

        ZFS_ENTER(p_zfsvfs);
        int i_error = VOP_GETATTR((vnode_t*)p_vnode, &vattr, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);
        if(i_error)
                return i_error;

        p_stat->st_dev = vattr.va_fsid;                      
        p_stat->st_ino = vattr.va_nodeid;
        p_stat->st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
        p_stat->st_nlink = vattr.va_nlink;
        p_stat->st_uid = vattr.va_uid;
        p_stat->st_gid = vattr.va_gid;
        p_stat->st_rdev = vattr.va_rdev;
        p_stat->st_size = vattr.va_size;
        p_stat->st_blksize = vattr.va_blksize;         
        p_stat->st_blocks = vattr.va_nblocks;
        TIMESTRUC_TO_TIME(vattr.va_atime, &p_stat->st_atime);
        TIMESTRUC_TO_TIME(vattr.va_mtime, &p_stat->st_mtime);
        TIMESTRUC_TO_TIME(vattr.va_ctime, &p_stat->st_ctime);

        return 0;
}

static int getattr_helper(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, uint64_t *p_gen, int *p_type)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_FALSE)))
                return i_error;
        ASSERT(p_znode);
        // Check the generation
        if(p_gen)
                *p_gen = p_znode->z_phys->zp_gen;
        else if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        vattr_t vattr;
        vattr.va_mask = AT_ALL;
        memset(p_stat, 0, sizeof(*p_stat));

        if(p_type)
                *p_type = VTTOIF(p_vnode->v_type);

        if((i_error = VOP_GETATTR(p_vnode, &vattr, 0, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_vnode);
                return i_error;
        }
        VN_RELE(p_vnode);

        p_stat->st_dev = vattr.va_fsid;
        p_stat->st_ino = vattr.va_nodeid;
        p_stat->st_mode = VTTOIF(vattr.va_type) | vattr.va_mode;
        p_stat->st_nlink = vattr.va_nlink;
        p_stat->st_uid = vattr.va_uid;
        p_stat->st_gid = vattr.va_gid;
        p_stat->st_rdev = vattr.va_rdev;
        p_stat->st_size = vattr.va_size;
        p_stat->st_blksize = vattr.va_blksize;
        p_stat->st_blocks = vattr.va_nblocks;
        TIMESTRUC_TO_TIME(vattr.va_atime, &p_stat->st_atime);
        TIMESTRUC_TO_TIME(vattr.va_mtime, &p_stat->st_mtime);
        TIMESTRUC_TO_TIME(vattr.va_ctime, &p_stat->st_ctime);

        return 0;
}

/**
 * Get the attributes of an object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param p_stat: the attributes to fill
 * @param p_type: return the type of the object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_getattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int *p_type)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        i_error = getattr_helper(p_vfs, p_cred, object, p_stat, NULL, p_type);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Set the attributes of an object
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param p_stat: new attributes to set
 * @param flags: bit field of attributes to set
 * @param p_new_stat: new attributes of the object
 * @return 0 on success, the error code overwise
 */
int libzfswrap_setattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, struct stat *p_stat, int flags, struct stat *p_new_stat)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;
        int update_time = 0;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t* p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        vattr_t vattr = { 0 };
        if(flags & LZFSW_ATTR_MODE)
        {
                vattr.va_mask |= AT_MODE;
                vattr.va_mode = p_stat->st_mode;
        }
        if(flags & LZFSW_ATTR_UID)
        {
                vattr.va_mask |= AT_UID;
                vattr.va_uid = p_stat->st_uid;
        }
        if(flags & LZFSW_ATTR_GID)
        {
                vattr.va_mask |= AT_GID;
                vattr.va_gid = p_stat->st_gid;
        }
        if(flags & LZFSW_ATTR_ATIME)
        {
                vattr.va_mask |= AT_ATIME;
                TIME_TO_TIMESTRUC(p_stat->st_atime, &vattr.va_atime);
                update_time = ATTR_UTIME;
        }
        if(flags & LZFSW_ATTR_MTIME)
        {
                vattr.va_mask |= AT_MTIME;
                TIME_TO_TIMESTRUC(p_stat->st_mtime, &vattr.va_mtime);
                update_time = ATTR_UTIME;
        }

        i_error = VOP_SETATTR(p_vnode, &vattr, update_time, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Helper function for every function that manipulate xattrs
 * @param p_zfsvfs: the virtual file system root object
 * @param p_cred: the user credentials
 * @param object: the object
 * @param
 */
int xattr_helper(zfsvfs_t *p_zfsvfs, creden_t *p_cred, inogen_t object, vnode_t **pp_vnode)
{
        znode_t *p_znode;
        int i_error;

        if((i_error = zfs_zget(p_zfsvfs, object.inode, &p_znode, B_TRUE)))
                return i_error;
        ASSERT(p_znode);

        // Check the generation
        if(p_znode->z_phys->zp_gen != object.generation)
        {
                VN_RELE(ZTOV(p_znode));
                return ENOENT;
        }
        vnode_t* p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        // Lookup for the xattr directory
        vnode_t *p_xattr_vnode;
        i_error = VOP_LOOKUP(p_vnode, "", &p_xattr_vnode, NULL,
                             LOOKUP_XATTR | CREATE_XATTR_DIR, NULL,
                             (cred_t*)p_cred, NULL, NULL, NULL);
        VN_RELE(p_vnode);

        if(i_error || !p_xattr_vnode)
        {
                if(p_xattr_vnode)
                        VN_RELE(p_xattr_vnode);
                return i_error ? i_error : ENOSYS;
        }

        *pp_vnode = p_xattr_vnode;
        return 0;
}

/**
 * List the extended attributes
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param ppsz_buffer: the buffer to fill with the list of attributes
 * @param p_size: will contain the size of the buffer
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_listxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, char **ppsz_buffer, size_t *p_size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Open the speudo directory
        if((i_error = VOP_OPEN(&p_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        char *psz_buffer = NULL;
        size_t i_size = 0;
        union {
                char buf[DIRENT64_RECLEN(MAXNAMELEN)];
                struct dirent64 dirent;
        } entry;

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        int eofp = 0;
        off_t next = 0;

        while(1)
        {
                iovec.iov_base = entry.buf;
                iovec.iov_len = sizeof(entry.buf);
                uio.uio_resid = iovec.iov_len;
                uio.uio_loffset = next;

                if((i_error = VOP_READDIR(p_vnode, &uio, (cred_t*)p_cred, &eofp, NULL, 0)))
                {
                        VOP_CLOSE(p_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);
                        VN_RELE(p_vnode);
                        ZFS_EXIT(p_zfsvfs);
                        return i_error;
                }

                if(iovec.iov_base == entry.buf)
                        break;

                next = entry.dirent.d_off;
                // Skip '.' and '..'
                char *s = entry.dirent.d_name;
                if(*s == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0)))
                        continue;

                size_t length = strlen(s);
                psz_buffer = realloc(psz_buffer, i_size + length + 1);
                strcpy(&psz_buffer[i_size], s);
                i_size += length + 1;
        }

        VOP_CLOSE(p_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        // Return the values
        *ppsz_buffer = psz_buffer;
        *p_size = i_size;

        return 0;
}

/**
 * Add the given (key,value) to the extended attributes.
 * This function will change the value if the key already exist.
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param object: the object
 * @param psz_key: the key
 * @param psz_value: the value
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_setxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, const char *psz_value)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Create a new speudo-file
        vattr_t vattr = { 0 };
        vattr.va_type = VREG;
        vattr.va_mode = 0660;
        vattr.va_mask = AT_TYPE | AT_MODE | AT_SIZE;
        vattr.va_size = 0;

        vnode_t *p_pseudo_vnode;
        if((i_error = VOP_CREATE(p_vnode, (char*)psz_key, &vattr, NONEXCL, VWRITE,
                                 &p_pseudo_vnode, (cred_t*)p_cred, 0, NULL, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
        }
        VN_RELE(p_vnode);

        // Open the key-file
        vnode_t *p_key_vnode = p_pseudo_vnode;
        if((i_error = VOP_OPEN(&p_key_vnode, FWRITE, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_pseudo_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = (void *) psz_value;
        iovec.iov_len = strlen(psz_value);
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_WRITE(p_key_vnode, &uio, FWRITE, (cred_t*)p_cred, NULL);
        VOP_CLOSE(p_key_vnode, FWRITE, 1, (offset_t) 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_key_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Get the value for the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @param ppsz_value: the value
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_getxattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key, char **ppsz_value)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;
        char *psz_value;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Lookup for the right file
        vnode_t *p_key_vnode;
        if((i_error = VOP_LOOKUP(p_vnode, (char*)psz_key, &p_key_vnode, NULL, 0, NULL,
                                 (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        VN_RELE(p_vnode);

        // Get the size of the value
        vattr_t vattr = { 0 };
        vattr.va_mask = AT_STAT | AT_NBLOCKS | AT_BLKSIZE | AT_SIZE;
        if((i_error = VOP_GETATTR(p_key_vnode, &vattr, 0, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_key_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        if((i_error = VOP_OPEN(&p_key_vnode, FREAD, (cred_t*)p_cred, NULL)))
        {
                VN_RELE(p_key_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        // Read the value
        psz_value = malloc(vattr.va_size + 1);
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = psz_value;
        iovec.iov_len = vattr.va_size + 1;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_READ(p_key_vnode, &uio, FREAD, (cred_t*)p_cred, NULL);
        VOP_CLOSE(p_key_vnode, FREAD, 1, (offset_t)0, (cred_t*)p_cred, NULL);

        VN_RELE(p_key_vnode);
        ZFS_EXIT(p_zfsvfs);

        if(!i_error)
        {
                psz_value[vattr.va_size] = '\0';
                *ppsz_value = psz_value;
        }
        return i_error;
}

/**
 * Remove the given extended attribute
 * @param p_vfs: the virtual file system
 * @param p_cred: the user credentials
 * @param object: the object
 * @param psz_key: the key
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_removexattr(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t object, const char *psz_key)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        vnode_t *p_vnode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = xattr_helper(p_zfsvfs, p_cred, object, &p_vnode)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        i_error = VOP_REMOVE(p_vnode, (char*)psz_key, (cred_t*)p_cred, NULL, 0);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}


/**
 * Read some data from the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_buffer: the buffer to write into
 * @param size: the size of the buffer
 * @param behind: do we have to read behind the file ?
 * @param offset: the offset to read
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_read(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;                      // TODO: Do we have to give the same flags ?
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = p_buffer;
        iovec.iov_len = size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = offset;
        if(behind)
                uio.uio_loffset += VTOZ((vnode_t*)p_vnode)->z_phys->zp_size;

        ZFS_ENTER(p_zfsvfs);
        int error = VOP_READ((vnode_t*)p_vnode, &uio, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);

        if(offset == uio.uio_loffset)
                return 0;
        else
                return size;
        return error;
}


/**
 * Write some data to the given file
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode
 * @param p_buffer: the buffer to write
 * @param size: the size of the buffer
 * @param behind: do we have to write behind the end of the file ?
 * @param offset: the offset to write
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_write(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, void *p_buffer, size_t size, int behind, off_t offset)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;                      // TODO: Do we have to give the same flags ?
        uio.uio_llimit = RLIM64_INFINITY;

        iovec.iov_base = p_buffer;
        iovec.iov_len = size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = offset;
        if(behind)
                uio.uio_loffset += VTOZ((vnode_t*)p_vnode)->z_phys->zp_size;

        ZFS_ENTER(p_zfsvfs);
        int error = VOP_WRITE((vnode_t*)p_vnode, &uio, 0, (cred_t*)p_cred, NULL);
        ZFS_EXIT(p_zfsvfs);

        return error;
}

/**
 * Close the given vnode
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param p_vnode: the vnode to close
 * @param i_flags: the flags given when opening
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_close(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, libzfswrap_vnode_t *p_vnode, int i_flags)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;

        int mode, flags, i_error;
        lzwu_flags2zfs(i_flags, &flags, &mode);

        ZFS_ENTER(p_zfsvfs);
        i_error = VOP_CLOSE((vnode_t*)p_vnode, flags, 1, (offset_t)0, (cred_t*)p_cred, NULL);
        VN_RELE((vnode_t*)p_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Create the given directory
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_name: the name of the directory
 * @param mode: the mode for the directory
 * @param p_directory: return the new directory
 * @return 0 on success, the error code overwise
 */
int libzfswrap_mkdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, mode_t mode, inogen_t *p_directory)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode != NULL);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);
        vnode_t *p_vnode = NULL;

        vattr_t vattr = { 0 };
        vattr.va_type = VDIR;
        vattr.va_mode = mode & PERMMASK;
        vattr.va_mask = AT_TYPE | AT_MODE;

        if((i_error = VOP_MKDIR(p_parent_vnode, (char*)psz_name, &vattr, &p_vnode, (cred_t*)p_cred, NULL, 0, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        p_directory->inode = VTOZ(p_vnode)->z_id;
        p_directory->generation = VTOZ(p_vnode)->z_phys->zp_gen;

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return 0;
}

/**
 * Remove the given directory
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_rmdir(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode);

        i_error = VOP_RMDIR(p_parent_vnode, (char*)psz_filename, NULL, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error == EEXIST ? ENOTEMPTY : i_error;
}

/**
 * Create a symbolic link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_name: the symbolic name
 * @param psz_link: the link content
 * @param p_symlink: the new symlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_symlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_name, const char *psz_link, inogen_t *p_symlink)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode != NULL);
        // Check generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode != NULL);

        vattr_t vattr = { 0 };
        vattr.va_type = VLNK;
        vattr.va_mode = 0777;
        vattr.va_mask = AT_TYPE | AT_MODE;

        if((i_error = VOP_SYMLINK(p_parent_vnode, (char*)psz_name, &vattr, (char*) psz_link, (cred_t*)p_cred, NULL, 0)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        vnode_t *p_vnode;
        if((i_error = VOP_LOOKUP(p_parent_vnode, (char*) psz_name, &p_vnode, NULL, 0, NULL, (cred_t*)p_cred, NULL, NULL, NULL)))
        {
                VN_RELE(p_parent_vnode);
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }

        ASSERT(p_vnode != NULL);
        p_symlink->inode = VTOZ(p_vnode)->z_id;
        p_symlink->generation = VTOZ(p_vnode)->z_phys->zp_gen;

        VN_RELE(p_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return 0;
}

/**
 * Read the content of a symbolic link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param symlink: the symlink to read
 * @param psz_content: return the content of the symlink
 * @param content_size: size of the buffer
 * @return 0 on success, the error code overwise
 */
int libzfswrap_readlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t symlink, char *psz_content, size_t content_size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_znode;

        ZFS_ENTER(p_zfsvfs);

        if((i_error = zfs_zget(p_zfsvfs, symlink.inode, &p_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode != NULL);
        // Check generation
        if(p_znode->z_phys->zp_gen != symlink.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode != NULL);

        iovec_t iovec;
        uio_t uio;
        uio.uio_iov = &iovec;
        uio.uio_iovcnt = 1;
        uio.uio_segflg = UIO_SYSSPACE;
        uio.uio_fmode = 0;
        uio.uio_llimit = RLIM64_INFINITY;
        iovec.iov_base = psz_content;
        iovec.iov_len = content_size;
        uio.uio_resid = iovec.iov_len;
        uio.uio_loffset = 0;

        i_error = VOP_READLINK(p_vnode, &uio, (cred_t*)p_cred, NULL);
        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);

        if(!i_error)
                psz_content[uio.uio_loffset] = '\0';
        else
                psz_content[0] = '\0';

        return i_error;
}

/**
 * Create a hard link
 * @param p_vfs: the virtual file system
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param target: the target object
 * @param psz_name: name of the link
 * @return 0 on success, the error code overwise
 */
int libzfswrap_link(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, inogen_t target, const char *psz_name)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode, *p_target_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        if((i_error = zfs_zget(p_zfsvfs, target.inode, &p_target_znode, B_FALSE)))
        {
                VN_RELE((ZTOV(p_parent_znode)));
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_target_znode);
        // Check the generation
        if(p_target_znode->z_phys->zp_gen != target.generation)
        {
                VN_RELE(ZTOV(p_target_znode));
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        vnode_t *p_target_vnode = ZTOV(p_target_znode);

        i_error = VOP_LINK(p_parent_vnode, p_target_vnode, (char*)psz_name, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_target_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

/**
 * Unlink the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the parent directory
 * @param psz_filename: name of the file to unlink
 * @return 0 on success, the error code overwise
 */
int libzfswrap_unlink(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        int i_error;
        znode_t *p_parent_znode;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        ASSERT(p_parent_vnode);

        i_error = VOP_REMOVE(p_parent_vnode, (char*)psz_filename, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Rename the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param parent: the current parent directory
 * @param psz_filename: current name of the file
 * @param new_parent: the new parents directory
 * @param psz_new_filename: new file name
 * @return 0 on success, the error code overwise
 */
int libzfswrap_rename(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t parent, const char *psz_filename, inogen_t new_parent, const char *psz_new_filename)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_parent_znode, *p_new_parent_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, parent.inode, &p_parent_znode, B_FALSE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_parent_znode);
        // Check the generation
        if(p_parent_znode->z_phys->zp_gen != parent.generation)
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        if((i_error = zfs_zget(p_zfsvfs, new_parent.inode, &p_new_parent_znode, B_FALSE)))
        {
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_new_parent_znode);
        // Check the generation
        if(p_new_parent_znode->z_phys->zp_gen != new_parent.generation)
        {
                VN_RELE(ZTOV(p_new_parent_znode));
                VN_RELE(ZTOV(p_parent_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }


        vnode_t *p_parent_vnode = ZTOV(p_parent_znode);
        vnode_t *p_new_parent_vnode = ZTOV(p_new_parent_znode);
        ASSERT(p_parent_vnode);
        ASSERT(p_new_parent_vnode);

        i_error = VOP_RENAME(p_parent_vnode, (char*)psz_filename, p_new_parent_vnode,
                             (char*)psz_new_filename, (cred_t*)p_cred, NULL, 0);

        VN_RELE(p_new_parent_vnode);
        VN_RELE(p_parent_vnode);
        ZFS_EXIT(p_zfsvfs);

        return i_error;
}

/**
 * Set the size of the given file
 * @param p_vfs: the virtual filesystem
 * @param p_cred: the credentials of the user
 * @param file: the file to truncate
 * @param size: the new size
 * @return 0 in case of success, the error code overwise
 */
int libzfswrap_truncate(libzfswrap_vfs_t *p_vfs, creden_t *p_cred, inogen_t file, size_t size)
{
        zfsvfs_t *p_zfsvfs = ((vfs_t*)p_vfs)->vfs_data;
        znode_t *p_znode;
        int i_error;

        ZFS_ENTER(p_zfsvfs);
        if((i_error = zfs_zget(p_zfsvfs, file.inode, &p_znode, B_TRUE)))
        {
                ZFS_EXIT(p_zfsvfs);
                return i_error;
        }
        ASSERT(p_znode);
        // Check the generation
        if(p_znode->z_phys->zp_gen != file.generation)
        {
                VN_RELE(ZTOV(p_znode));
                ZFS_EXIT(p_zfsvfs);
                return ENOENT;
        }

        vnode_t *p_vnode = ZTOV(p_znode);
        ASSERT(p_vnode);

        flock64_t fl;
        fl.l_whence = 0;        // beginning of the file
        fl.l_start = size;
        fl.l_type = F_WRLCK;
        fl.l_len = (off_t)0;

        i_error = VOP_SPACE(p_vnode, F_FREESP, &fl, FWRITE, 0, (cred_t*)p_cred, NULL);

        VN_RELE(p_vnode);
        ZFS_EXIT(p_zfsvfs);
        return i_error;
}

