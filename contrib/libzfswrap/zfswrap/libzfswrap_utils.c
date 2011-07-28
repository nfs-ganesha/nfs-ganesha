#include <umem.h>
#include <libsolkerncompat.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <sys/systm.h>
#include <libzfs.h>
#include <libzfs_impl.h>
#include <sys/zfs_znode.h>
#include <sys/mode.h>
#include <sys/fcntl.h>

#include <limits.h>

#include "libzfswrap_utils.h"

/** 
 * Convert the POSIX flags to ZFS ones
 * @param i_flags: the POSIX flag
 * @param p_flags: return the ZFS flag
 * @param p_mode: return the ZFS mode
 */
void lzwu_flags2zfs(int i_flags, int *p_flags, int *p_mode)
{
        if(i_flags & O_WRONLY)
        {
                *p_mode = VWRITE;
                *p_flags = FWRITE;
        }
        else if(i_flags & O_RDWR)
        {
               *p_mode = VREAD | VWRITE;
               *p_flags = FREAD | FWRITE;
        }
        else
        {
                *p_mode = VREAD;
                *p_flags = FREAD;
        }

        if(i_flags & O_CREAT)
                *p_flags |= FCREAT;
        if(i_flags & O_SYNC)
                *p_flags |= FSYNC;
        if(i_flags & O_DSYNC)
                *p_flags |= FDSYNC;
        if(i_flags & O_RSYNC)
                *p_flags |= FRSYNC;
        if(i_flags & O_APPEND)
                *p_flags |= FAPPEND;
        if(i_flags & O_LARGEFILE)
                *p_flags |= FOFFMAX;
        if(i_flags & O_NOFOLLOW)
                *p_flags |= FNOFOLLOW;
        if(i_flags & O_TRUNC)
                *p_flags |= FTRUNC;
        if(i_flags & O_EXCL)
                *p_flags |= FEXCL;
}

/**
 * Create the vdev leaf for the given path.
 * The function assume that the path is a block device or a file.
 * Log devices and hot spares are not supported
 * @param psz_path: path to the device to use
 * @return the new vdev or NULL in case of error.
 */
nvlist_t *lzwu_make_leaf_vdev(const char *psz_path)
{
        struct stat64 statbuf;
        nvlist_t *p_vdev;
        const char *psz_type;

        if(stat64(psz_path, &statbuf) != 0)
                return NULL;

        if(S_ISBLK(statbuf.st_mode))
                psz_type = VDEV_TYPE_DISK;
        else if(S_ISREG(statbuf.st_mode))
                psz_type = VDEV_TYPE_FILE;
        else
                return NULL;

        nvlist_alloc(&p_vdev, NV_UNIQUE_NAME, 0);
        nvlist_add_string(p_vdev, ZPOOL_CONFIG_PATH, psz_path);
        nvlist_add_string(p_vdev, ZPOOL_CONFIG_TYPE, psz_type);
        nvlist_add_string(p_vdev, ZPOOL_CONFIG_IS_LOG, 0);
        if(!strcmp(psz_type, VDEV_TYPE_DISK))
                nvlist_add_uint64(p_vdev, ZPOOL_CONFIG_WHOLE_DISK, 0);

        return p_vdev;
}

/**
 * Create the root of the vdev tree according to the parameters (type and vdev)
 * @param psz_type: type of zpool (""=raid0, "mirror or "raidz")
 * @param ppsz_dev: the list of devices
 * @param i_dev: the number of devices
 * @param ppsz_error: return the error message if any
 * @return the root vded or NULL in case of error
 */
nvlist_t *lzwu_make_root_vdev(const char *psz_type, const char **ppsz_dev, size_t i_dev, const char **ppsz_error)
{
        nvlist_t *pnv_root, **ppnv_top;
        int i_mindev, i_maxdev, i_top = 0;
        size_t i;

        /* Check the type and the required number of devices */
        if(!strcmp(psz_type, "raidz", 5))
        {
                int i_parity;
                const char *psz_parity = psz_type + 5;
                if(*psz_parity == '\0')
                        i_parity = 1;
                else if(*psz_parity == '0')
                {
                        *ppsz_error = "raidz0 does not exist";
                        return NULL;
                }
                else
                {
                        const char *psz_end;
                        i_parity = strtol(psz_parity, &psz_end, 10);
                        if(i_parity < 1 || i_parity > 255 || *psz_end != '\0')
                        {
                                *ppsz_error = "raidz only accept values in [1, 255]";
                                return NULL;
                        }
                }
                i_mindev = i_parity + 1;
                i_maxdev = 255;
                psz_type = "raidz";
        }
        else if(!strcmp(psz_type, "mirror"))
        {
                i_mindev = 2;
                i_maxdev = INT_MAX;
        }
        else if(psz_type[0] == '\0')
        {
                i_mindev = 1;
                i_maxdev = INT_MAX;
        }
        else
        {
                *ppsz_error = "unknown zpool type: only '', 'mirror' and 'raidz' are handled";
                return NULL;
        }

        /* Check the number of devices */
        if(i_dev < i_mindev || i_dev > i_maxdev)
        {
                *ppsz_error = i_dev < i_mindev ? "too few devices" :
                                                 "too much devices";
                return NULL;
        }

        if(psz_type[0] == '\0')
        {
                ppnv_top = malloc(i_dev * sizeof(nvlist_t*));
                i_top = i_dev;
                for(i = 0; i < i_dev; i++)
                {
                        ppnv_top[i] = lzwu_make_leaf_vdev(ppsz_dev[i]);
                        if(!ppnv_top[i])
                        {
                                size_t j;
                                for(j = 0; j < i; j++)
                                        nvlist_free(ppnv_top[j]);
                                free(ppnv_top);

                                *ppsz_error = "unable to create the vdev array";
                                return NULL;
                        }
                }
        }
        else
        {
                /* List all the devices */
                nvlist_t **pp_children = malloc(i_dev * sizeof(nvlist_t *));
                for(i = 0; i < i_dev; i++)
                {
                        pp_children[i] = lzwu_make_leaf_vdev(ppsz_dev[i]);
                        if(!pp_children[i])
                        {
                                size_t j;
                                for(j = 0; j < i; j++)
                                        nvlist_free(pp_children[j]);
                                *ppsz_error = "unable to create the vdev array";
                                return NULL;
                        }
                }

                // Build the list of devices
                nvlist_t *pnv_fs;
                assert(nvlist_alloc(&pnv_fs, NV_UNIQUE_NAME, 0) == 0);
                assert(nvlist_add_string(pnv_fs, ZPOOL_CONFIG_TYPE, psz_type) == 0);
                assert(nvlist_add_nvlist_array(pnv_fs, ZPOOL_CONFIG_CHILDREN, pp_children, i_dev) == 0);
                if(!strncmp(psz_type, "raidz", 5))
                        nvlist_add_uint64(pnv_fs, ZPOOL_CONFIG_NPARITY, i_mindev - 1);

                i_top = 1;
                ppnv_top = malloc(sizeof(nvlist_t*));
                ppnv_top[0] = pnv_fs;

                for(i = 0; i < i_dev; i++)
                        nvlist_free(pp_children[i]);
                free(pp_children);
        }

        /* Create the root tree */
        assert(nvlist_alloc(&pnv_root, NV_UNIQUE_NAME, 0) == 0);
        assert(nvlist_add_string(pnv_root, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) == 0);
        assert(nvlist_add_nvlist_array(pnv_root, ZPOOL_CONFIG_CHILDREN, ppnv_top, i_top) == 0);

        for(i = 0; i < i_top; i++)
                nvlist_free(ppnv_top[i]);
        free(ppnv_top);

        return pnv_root;
}

/**
 * Print the header of the 'list' function
 * @param p_zpl: the list of properties
 */
void lzwu_zpool_print_list_header(zprop_list_t *p_zpl)
{
        const char *psz_header;
        boolean_t first = B_TRUE;
        boolean_t right_justify;

        for(; p_zpl != NULL; p_zpl = p_zpl->pl_next)
        {
                if(p_zpl->pl_prop == ZPROP_INVAL)
                        continue;

                if(!first)
                        printf("  ");
                else
                        first = B_FALSE;

                psz_header = zpool_prop_column_name(p_zpl->pl_prop);
                right_justify = zpool_prop_align_right(p_zpl->pl_prop);

                if(p_zpl->pl_next == NULL && !right_justify)
                        printf("%s", psz_header);
                else if(right_justify)
                        printf("%*s", (int)p_zpl->pl_width, psz_header);
                else
                        printf("%-*s", (int)p_zpl->pl_width, psz_header);
        }

        printf("\n");
}

/**
 * Print the header of the 'list' function
 * @param p_zpl: the list of properties
 */
void lzwu_zfs_print_list_header(zprop_list_t *p_zpl)
{
        char *headerbuf[ZFS_MAXPROPLEN];
        const char *psz_header;
        boolean_t first = B_TRUE;
        boolean_t right_justify;

        for(; p_zpl != NULL; p_zpl = p_zpl->pl_next)
        {
                if(!first)
                        printf("  ");
                else
                        first = B_FALSE;

                right_justify = B_FALSE;
                if(p_zpl->pl_prop != ZPROP_INVAL)
                {
                        psz_header = zfs_prop_column_name(p_zpl->pl_prop);
                        right_justify = zfs_prop_align_right(p_zpl->pl_prop);
                }
                else
                {
                        int i;
                        for(i = 0; p_zpl->pl_user_prop[i] != '\0'; i++)
                                headerbuf[i] = toupper(p_zpl->pl_user_prop[i]);
                        headerbuf[i] = '\0';
                        psz_header = headerbuf;
                }

                if(p_zpl->pl_next == NULL && !right_justify)
                        printf("%s", psz_header);
                else if(right_justify)
                        printf("%*s", (int)p_zpl->pl_width, psz_header);
                else
                        printf("%-*s", (int)p_zpl->pl_width, psz_header);
        }

        printf("\n");
}

/*
 * Print out detailed scrub status.
 * @param pnv_root: the root tree
 */
void lzwu_zpool_print_scrub_status(nvlist_t *pnv_root)
{
        vdev_stat_t *p_vs;
        unsigned vs_count;
        time_t start, end, now;
        double fraction_done;
        uint64_t examined, total, minutes_left, minutes_taken;
        const char *psz_scrub_type;

        assert(nvlist_lookup_uint64_array(pnv_root, ZPOOL_CONFIG_STATS,
            (uint64_t **)&p_vs, &vs_count) == 0);

        /*
         * If there's never been a scrub, there's not much to say.
         */
        if(p_vs->vs_scrub_end == 0 && p_vs->vs_scrub_type == POOL_SCRUB_NONE)
        {
                printf("none requested\n");
                return;
        }

        psz_scrub_type = (p_vs->vs_scrub_type == POOL_SCRUB_RESILVER) ? "resilver" : "scrub";

        start = p_vs->vs_scrub_start;
        end = p_vs->vs_scrub_end;
        now = time(NULL);
        examined = p_vs->vs_scrub_examined;
        total = p_vs->vs_alloc;

        if(end != 0)
        {
                minutes_taken = (uint64_t)((end - start) / 60);

                printf("%s %s after %lluh%um with %llu errors on %s",
                       psz_scrub_type, p_vs->vs_scrub_complete ? "completed" : "stopped",
                       (u_longlong_t)(minutes_taken / 60),
                       (uint_t)(minutes_taken % 60),
                       (u_longlong_t)p_vs->vs_scrub_errors, ctime(&end));
                return;
        }

        if(examined == 0)
                examined = 1;
        if(examined > total)
                total = examined;

        fraction_done = (double)examined / total;
        minutes_left = (uint64_t)((now - start) *
            (1 - fraction_done) / fraction_done / 60);
        minutes_taken = (uint64_t)((now - start) / 60);

        printf("%s in progress for %lluh%um, %.2f%% done, %lluh%um to go\n",
               psz_scrub_type, (u_longlong_t)(minutes_taken / 60),
               (uint_t)(minutes_taken % 60), 100 * fraction_done,
               (u_longlong_t)(minutes_left / 60), (uint_t)(minutes_left % 60));
}

/*
 * Given a vdev configuration, determine the maximum width needed for the device
 * name column.
 */
int lzwu_zpool_max_width(libzfs_handle_t *p_zhd, zpool_handle_t *p_zpool, nvlist_t *nv, int depth, int max)
{
        char *psz_zpool = zpool_vdev_name(p_zhd, p_zpool, nv, B_TRUE);
        nvlist_t **ppnv_child;
        uint_t i_children;
        int ret;

        if(strlen(psz_zpool) + depth > max)
                max = strlen(psz_zpool) + depth;

        free(psz_zpool);

        if(nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
            &ppnv_child, &i_children) == 0)
        {
                for(int c = 0; c < i_children; c++)
                        if((ret = lzwu_zpool_max_width(p_zhd, p_zpool, ppnv_child[c],
                                                       depth + 2, max)) > max)
                                max = ret;
        }

        if(nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE, &ppnv_child, &i_children) == 0)
        {
                for(int c = 0; c < i_children; c++)
                        if((ret = lzwu_zpool_max_width(p_zhd, p_zpool, ppnv_child[c],
                                                       depth + 2, max)) > max)
                                max = ret;
        }

        if(nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &ppnv_child, &i_children) == 0)
        {
                for(int c = 0; c < i_children; c++)
                        if((ret = lzwu_zpool_max_width(p_zhd, p_zpool, ppnv_child[c],
                                                       depth + 2, max)) > max)
                                max = ret;
        }

        return max;
}

boolean_t lzwu_zpool_find_vdev(nvlist_t *pnv_root, uint64_t search)
{
        uint64_t guid;
        nvlist_t **ppnv_child;
        uint_t i_children;

        if(nvlist_lookup_uint64(pnv_root, ZPOOL_CONFIG_GUID, &guid) == 0 &&
           search == guid)
                return B_TRUE;

        if(nvlist_lookup_nvlist_array(pnv_root, ZPOOL_CONFIG_CHILDREN,
            &ppnv_child, &i_children) == 0)
        {
                for(unsigned c = 0; c < i_children; c++)
                        if(lzwu_zpool_find_vdev(ppnv_child[c], search))
                                return B_TRUE;
        }

        return B_FALSE;
}

int lzwu_find_spare(zpool_handle_t *p_zpool, void *data)
{
        spare_cbdata_t *cbp = (spare_cbdata_t*)data;
        nvlist_t *config, *pnv_root;

        config = zpool_get_config(p_zpool, NULL);
        verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
            &pnv_root) == 0);

        if(lzwu_zpool_find_vdev(pnv_root, cbp->cb_guid))
        {
                cbp->cb_zhp = p_zpool;
                return 1;
        }

        zpool_close(p_zpool);
        return 0;
}

/*
 * Print out configuration state as requested by status_callback.
 */
void lzwu_zpool_print_status_config(libzfs_handle_t *p_zhd, zpool_handle_t *zhp,
                                    const char *name, nvlist_t *nv, int namewidth,
                                    int depth, boolean_t isspare)
{
        nvlist_t **child;
        uint_t children;
        unsigned c;
        vdev_stat_t *vs;
        char rbuf[6], wbuf[6], cbuf[6], repaired[7];
        char *vname;
        uint64_t notpresent;
        spare_cbdata_t cb;
        char *state;

        verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_STATS,
            (uint64_t **)&vs, &c) == 0);

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
            &child, &children) != 0)
                children = 0;

        state = zpool_state_to_name(vs->vs_state, vs->vs_aux);
        if(isspare)
        {
                /*
                 * For hot spares, we use the terms 'INUSE' and 'AVAILABLE' for
                 * online drives.
                 */
                if(vs->vs_aux == VDEV_AUX_SPARED)
                        state = "INUSE";
                else if(vs->vs_state == VDEV_STATE_HEALTHY)
                        state = "AVAIL";
        }

        printf("\t%*s%-*s  %-8s", depth, "", namewidth - depth,
            name, state);

        if(!isspare)
        {
                zfs_nicenum(vs->vs_read_errors, rbuf, sizeof (rbuf));
                zfs_nicenum(vs->vs_write_errors, wbuf, sizeof (wbuf));
                zfs_nicenum(vs->vs_checksum_errors, cbuf, sizeof (cbuf));
                printf(" %5s %5s %5s", rbuf, wbuf, cbuf);
        }

        if(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT, &notpresent) == 0)
        {
                char *path;
                verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_PATH, &path) == 0);
                printf("  was %s", path);
        }
        else if(vs->vs_aux != 0)
        {
                printf("  ");

                switch (vs->vs_aux)
                {
                case VDEV_AUX_OPEN_FAILED:
                        printf("cannot open");
                        break;

                case VDEV_AUX_BAD_GUID_SUM:
                        printf("missing device");
                        break;

                case VDEV_AUX_NO_REPLICAS:
                        printf("insufficient replicas");
                        break;

                case VDEV_AUX_VERSION_NEWER:
                        printf("newer version");
                        break;

                case VDEV_AUX_SPARED:
                        verify(nvlist_lookup_uint64(nv, ZPOOL_CONFIG_GUID,
                            &cb.cb_guid) == 0);
                        if(zpool_iter(p_zhd, lzwu_find_spare, &cb) == 1)
                        {
                                if(strcmp(zpool_get_name(cb.cb_zhp),
                                    zpool_get_name(zhp)) == 0)
                                        printf("currently in use");
                                else
                                        printf("in use by pool '%s'", zpool_get_name(cb.cb_zhp));
                                zpool_close(cb.cb_zhp);
                        }
                        else
                                printf("currently in use");
                        break;

                case VDEV_AUX_ERR_EXCEEDED:
                        printf("too many errors");
                        break;

                case VDEV_AUX_IO_FAILURE:
                        printf("experienced I/O failures");
                        break;

                case VDEV_AUX_BAD_LOG:
                        printf("bad intent log");
                        break;

                case VDEV_AUX_EXTERNAL:
                        printf("external device fault");
                        break;

                case VDEV_AUX_SPLIT_POOL:
                        printf("split into new pool");
                        break;

                default:
                        printf("corrupted data");
                        break;
                }
        }
        else if(vs->vs_scrub_repaired != 0 && children == 0)
        {
                /*
                 * Report bytes resilvered/repaired on leaf devices.
                 */
                zfs_nicenum(vs->vs_scrub_repaired, repaired, sizeof (repaired));
                printf("  %s %s", repaired,
                       (vs->vs_scrub_type == POOL_SCRUB_RESILVER) ?
                       "resilvered" : "repaired");
        }

        printf("\n");

        for(unsigned c = 0; c < children; c++)
        {
                uint64_t islog = B_FALSE, ishole = B_FALSE;

                /* Don't print logs or holes here */
                nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG, &islog);
                nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE, &ishole);
                if(islog || ishole)
                        continue;
                vname = zpool_vdev_name(p_zhd, zhp, child[c], B_TRUE);
                lzwu_zpool_print_status_config(p_zhd, zhp, vname, child[c],
                                               namewidth, depth + 2, isspare);
                free(vname);
        }
}

/*
 * Return the number of logs in supplied nvlist
 */
uint_t lzwu_num_logs(nvlist_t *nv)
{
        uint_t nlogs = 0;
        uint_t c, children;
        nvlist_t **child;

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
            &child, &children) != 0)
                return (0);

        for (c = 0; c < children; c++) {
                uint64_t is_log = B_FALSE;

                (void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
                    &is_log);
                if (is_log)
                        nlogs++;
        }
        return (nlogs);
}

/*
 * Print the configuration of an exported pool.  Iterate over all vdevs in the
 * pool, printing out the name and status for each one.
 */
static void lzwu_print_import_config(libzfs_handle_t *p_zhd, const char *name, nvlist_t *nv, int namewidth, int depth)
{
        nvlist_t **child;
        uint_t c, children;
        vdev_stat_t *vs;
        char *type, *vname;

        verify(nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &type) == 0);
        if (strcmp(type, VDEV_TYPE_MISSING) == 0 ||
            strcmp(type, VDEV_TYPE_HOLE) == 0)
                return;

        verify(nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_STATS,
            (uint64_t **)&vs, &c) == 0);

        (void) printf("\t%*s%-*s", depth, "", namewidth - depth, name);
        (void) printf("  %s", zpool_state_to_name(vs->vs_state, vs->vs_aux));

        if (vs->vs_aux != 0) {
                (void) printf("  ");

                switch (vs->vs_aux) {
                case VDEV_AUX_OPEN_FAILED:
                        printf("cannot open");
                        break;

                case VDEV_AUX_BAD_GUID_SUM:
                        printf("missing device");
                        break;

                case VDEV_AUX_NO_REPLICAS:
                        printf("insufficient replicas");
                        break;

                case VDEV_AUX_VERSION_NEWER:
                        printf("newer version");
                        break;

                case VDEV_AUX_ERR_EXCEEDED:
                        printf("too many errors");
                        break;

                default:
                        printf("corrupted data");
                        break;
                }
        }
        (void) printf("\n");

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
            &child, &children) != 0)
                return;

        for (c = 0; c < children; c++) {
                uint64_t is_log = B_FALSE;

                (void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
                    &is_log);
                if (is_log)
                        continue;

                vname = zpool_vdev_name(p_zhd, NULL, child[c], B_TRUE);
                lzwu_print_import_config(p_zhd, vname, child[c], namewidth, depth + 2);
                free(vname);
        }

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_L2CACHE,
            &child, &children) == 0) {
                printf("\tcache\n");
                for (c = 0; c < children; c++) {
                        vname = zpool_vdev_name(p_zhd, NULL, child[c], B_FALSE);
                        (void) printf("\t  %s\n", vname);
                        free(vname);
                }
        }

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_SPARES,
            &child, &children) == 0) {
                printf("\tspares\n");
                for (c = 0; c < children; c++) {
                        vname = zpool_vdev_name(p_zhd, NULL, child[c], B_FALSE);
                        (void) printf("\t  %s\n", vname);
                        free(vname);
                }
        }
}
/*
 * Print log vdevs.
 * Logs are recorded as top level vdevs in the main pool child array
 * but with "is_log" set to 1. We use either print_status_config() or
 * lzwu_print_import_config() to print the top level logs then any log
 * children (eg mirrored slogs) are printed recursively - which
 * works because only the top level vdev is marked "is_log"
 */
void lzwu_print_logs(libzfs_handle_t *p_zhd, zpool_handle_t *zhp, nvlist_t *nv, int namewidth, boolean_t verbose)
{
        uint_t children;
        nvlist_t **child;

        if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN, &child,
            &children) != 0)
                return;

        printf("\tlogs\n");

        for(unsigned c = 0; c < children; c++)
        {
                uint64_t is_log = B_FALSE;
                char *name;

                (void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
                    &is_log);
                if(!is_log)
                        continue;
                name = zpool_vdev_name(p_zhd, zhp, child[c], B_TRUE);
                if(verbose)
                        lzwu_zpool_print_status_config(p_zhd, zhp, name, child[c], namewidth,
                                                       2, B_FALSE);
                else
                        lzwu_print_import_config(p_zhd, name, child[c], namewidth, 2);
                free(name);
        }
}

void lzwu_print_l2cache(libzfs_handle_t *p_zhd, zpool_handle_t *zhp, nvlist_t **l2cache,
                          uint_t nl2cache, int namewidth)
{
        if (nl2cache == 0)
                return;

        printf("\tcache\n");

        for(unsigned i = 0; i < nl2cache; i++)
        {
                char *name = zpool_vdev_name(p_zhd, zhp, l2cache[i], B_FALSE);
                lzwu_zpool_print_status_config(p_zhd, zhp, name, l2cache[i], namewidth, 2, B_FALSE);
                free(name);
        }
}

void lzwu_print_spares(libzfs_handle_t *p_zhd, zpool_handle_t *zhp, nvlist_t **spares,
                         uint_t nspares, int namewidth)
{
        if (nspares == 0)
                return;

        printf("\tspares\n");

        for(unsigned i = 0; i < nspares; i++)
        {
                char *name = zpool_vdev_name(p_zhd, zhp, spares[i], B_FALSE);
                lzwu_zpool_print_status_config(p_zhd, zhp, name, spares[i], namewidth, 2, B_TRUE);
                free(name);
        }
}

void lzwu_print_error_log(zpool_handle_t *zhp)
{
        nvlist_t *nverrlist = NULL;
        nvpair_t *elem;
        char *pathname;
        size_t len = MAXPATHLEN * 2;

        if (zpool_get_errlog(zhp, &nverrlist) != 0) {
                printf("errors: List of errors unavailable "
                    "(insufficient privileges)\n");
                return;
        }

        printf("errors: Permanent errors have been "
               "detected in the following files:\n\n");

        pathname = malloc(len);
        elem = NULL;
        while ((elem = nvlist_next_nvpair(nverrlist, elem)) != NULL) {
                nvlist_t *nv;
                uint64_t dsobj, obj;

                verify(nvpair_value_nvlist(elem, &nv) == 0);
                verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_DATASET,
                    &dsobj) == 0);
                verify(nvlist_lookup_uint64(nv, ZPOOL_ERR_OBJECT,
                    &obj) == 0);
                zpool_obj_to_path(zhp, dsobj, obj, pathname, len);
                (void) printf("%7s %s\n", "", pathname);
        }
        free(pathname);
        nvlist_free(nverrlist);
}

void lzwu_print_dedup_stats(nvlist_t *config)
{
        ddt_histogram_t *ddh;
        ddt_stat_t *dds;
        ddt_object_t *ddo;
        uint_t c;

        /*
         * If the pool was faulted then we may not have been able to
         * obtain the config. Otherwise, if have anything in the dedup
         * table continue processing the stats.
         */
        if (nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_OBJ_STATS,
            (uint64_t **)&ddo, &c) != 0 || ddo->ddo_count == 0)
                return;

        printf("\n");
        printf("DDT entries %llu, size %llu on disk, %llu in core\n",
               (u_longlong_t)ddo->ddo_count,
               (u_longlong_t)ddo->ddo_dspace,
               (u_longlong_t)ddo->ddo_mspace);

        verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_STATS,
            (uint64_t **)&dds, &c) == 0);
        verify(nvlist_lookup_uint64_array(config, ZPOOL_CONFIG_DDT_HISTOGRAM,
            (uint64_t **)&ddh, &c) == 0);
        zpool_dump_ddt(dds, ddh);
}


