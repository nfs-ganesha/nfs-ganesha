#ifndef LIBZFSWRAP_UTIL_H
#define LIBZFSWRAP_UTIL_H

typedef struct status_cbdata {
        int             cb_count;
        boolean_t       cb_allpools;
        boolean_t       cb_verbose;
        boolean_t       cb_explain;
        boolean_t       cb_first;
        boolean_t       cb_dedup_stats;
        libzfs_handle_t *p_zhd;
} status_cbdata_t;

typedef struct spare_cbdata {
        uint64_t        cb_guid;
        zpool_handle_t        *cb_zhp;
} spare_cbdata_t;

typedef struct
{
        boolean_t       cb_first;
        boolean_t       cb_scripted;
        zprop_list_t    *cb_proplist;
} list_cbdata_t;

void lzwu_flags2zfs(int i_flags, int *p_flags, int *p_mode);
nvlist_t *lzwu_make_leaf_vdev(const char *psz_path);
nvlist_t *lzwu_make_root_vdev(const char *psz_type, const char **ppsz_dev, size_t i_dev, const char **ppsz_error);
void lzwu_zpool_print_list_header(zprop_list_t *p_zpl);
void lzwu_zfs_print_list_header(zprop_list_t *p_zpl);
void lzwu_zpool_print_scrub_status(nvlist_t *pnv_root);
int lzwu_zpool_max_width(libzfs_handle_t *p_zhd, zpool_handle_t *p_zpool, nvlist_t *nv, int depth, int max);
boolean_t lzwu_zpool_find_vdev(nvlist_t *pnv_root, uint64_t search);
int lzwu_find_spare(zpool_handle_t *p_zpool, void *data);
void lzwu_zpool_print_status_config(libzfs_handle_t *p_zhd, zpool_handle_t *zhp,
                                    const char *name, nvlist_t *nv, int namewidth,
                                    int depth, boolean_t isspare);
uint_t lzwu_num_logs(nvlist_t *nv);
void lzwu_print_l2cache(libzfs_handle_t *p_zhd, zpool_handle_t *zhp, nvlist_t **l2cache,
                        uint_t nl2cache, int namewidth);
void lzwu_print_spares(libzfs_handle_t *p_zhd, zpool_handle_t *zhp, nvlist_t **spares,
                       uint_t nspares, int namewidth);
void lzwu_print_error_log(zpool_handle_t *zhp);
void lzwu_print_dedup_stats(nvlist_t *config);

#endif //LIBZFSWRAP_UTIL_H
