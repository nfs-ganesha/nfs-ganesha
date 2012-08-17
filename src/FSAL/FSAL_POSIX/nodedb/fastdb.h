
#define ERROR_MSG_SIZE		160

#define user_data_t             void

struct fastdb_item;

struct fastdb;

void fastdb_lock (struct fastdb *m);
void fastdb_unlock (struct fastdb *m);
void fastdb_insert (struct fastdb *m, const user_data_t * data);
void fastdb_insert_or_replace (struct fastdb *m, int idx_number, const user_data_t * data);
int fastdb_update (struct fastdb *m, int idx_number, user_data_t * data);
int fastdb_delete (struct fastdb *m, int idx_number, user_data_t * data);
int fastdb_lookup (struct fastdb *m, int idx_number, user_data_t * data);
int fastdb_lookup_lock (struct fastdb *m, int idx_number, user_data_t * data, enum cmp_op op, enum cmp_lean lean);
int fastdb_next (struct fastdb *m, int idx_num, user_data_t * data);
void fastdb_free (struct fastdb *m);
long long fastdb_count (struct fastdb *m);
long long fastdb_write_pending_count (struct fastdb *m);
struct fastdb *fastdb_setup (char *err_msg, int userdata_size);
int fastdb_add_index (struct fastdb *m, int allow_duplicates, redblack_cmp_cb_t cmp, void *hook);
int fastdb_load (struct fastdb *m, const char *filename, char *err_msg);
void fastdb_traverse (struct fastdb *m, int idx_num, void (*cb) (user_data_t *, void *, void *), void *user_data1, void *user_data2);
long long fastdb_eof_offset (struct fastdb *m);
int fastdb_flush (struct fastdb *fastdb, int flush_records, long long *count, long long *done_truncate, char *err_msg);
