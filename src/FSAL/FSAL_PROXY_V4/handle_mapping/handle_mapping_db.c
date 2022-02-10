#include "config.h"
#include "handle_mapping.h"
#include "handle_mapping_db.h"
#include "handle_mapping_internal.h"
#include <sqlite3.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <pthread.h>

/* sqlite check macros */

#define CheckTable(_p_conn_, _code_, _msg_str_, _result_) \
do { \
	if ((_code_) != SQLITE_OK) { \
		LogCrit(COMPONENT_FSAL, \
			"SQLite command failed in %s line %i", \
			__func__, __LINE__);		\
		LogCrit(COMPONENT_FSAL, "%s (%d)", \
			(_msg_str_ ? _msg_str_ : sqlite3_errmsg(_p_conn_)), \
			_code_);					\
		if (_msg_str_) {					\
			sqlite3_free(_msg_str_);			\
			_msg_str_ = NULL;				\
		}							\
		if (_result_) {						\
			sqlite3_free_table(_result_);			\
			_result_ = NULL;				\
		}							\
		return HANDLEMAP_DB_ERROR;				\
	}								\
} while (0)

#define CheckCommand(_p_conn_, _code_, _msg_str_) \
do { \
	if ((_code_) != SQLITE_OK) { \
		LogCrit(COMPONENT_FSAL, \
			"SQLite command failed in %s line %i", \
			__func__, __LINE__);			\
		LogCrit(COMPONENT_FSAL, "%s (%d)", \
			(_msg_str_ ? _msg_str_ : sqlite3_errmsg(_p_conn_)), \
			_code_);					\
		if (_msg_str_) { \
			sqlite3_free(_msg_str_); \
			_msg_str_ = NULL; \
		}			  \
		return HANDLEMAP_DB_ERROR;	\
	}					\
} while (0)

#define CheckPrepare(_p_conn_, _code_) \
do { \
	if ((_code_) != SQLITE_OK) {	\
		LogCrit(COMPONENT_FSAL,					\
			"SQLite prepare statement failed in %s line %i", \
			__func__, __LINE__);				\
		LogCrit(COMPONENT_FSAL, "%s (%d)",			\
			sqlite3_errmsg(_p_conn_), _code_);		\
		return HANDLEMAP_DB_ERROR;				\
	}								\
} while (0)

#define CheckBind(_p_conn_, _code_, _stmt_) \
do { \
	if ((_code_) != SQLITE_OK) { \
		LogCrit(COMPONENT_FSAL,				 \
			"SQLite parameter binding failed in %s line %i", \
			__func__, __LINE__);				\
		LogCrit(COMPONENT_FSAL, "%s (%d)",			\
			sqlite3_errmsg(_p_conn_), _code_);		\
		sqlite3_clear_bindings(_stmt_);				\
		return HANDLEMAP_DB_ERROR;				\
	}								\
} while (0)

#define CheckStep(_p_conn_, _code_, _stmt_) \
do { \
	if ((_code_) != SQLITE_OK  && (_code_) != \
	    SQLITE_ROW && (_code_) != SQLITE_DONE) {			\
		LogCrit(COMPONENT_FSAL,					\
			"SQLite command failed in %s line %i",		\
			__func__, __LINE__);			\
		LogCrit(COMPONENT_FSAL, "%s (%d)",			\
			sqlite3_errmsg(_p_conn_), _code_);		\
		sqlite3_reset(_stmt_);					\
		return HANDLEMAP_DB_ERROR;				\
	}								\
} while (0)

/* Type of DB operations */
typedef enum {
	LOAD = 1,
	INSERT,
	DELETE
} db_op_type;

/* DB operation arguments */
typedef struct db_op_item__ {
	db_op_type op_type;

	/* operation info */

	union {
		struct hdlmap_tuple {
			nfs23_map_handle_t nfs23_digest;
			uint8_t fh4_len;
			char fh4_data[NFS4_FHSIZE];
		} fh_info;

		hash_table_t *hash;
	} op_arg;

	/* for chained list */
	struct db_op_item__ *p_next;

} db_op_item_t;

/* the queue for each DB flusher thread */
typedef struct flusher_queue__ {
	/* the queue for high priority operations */
	db_op_item_t *highprio_first;
	db_op_item_t *highprio_last;

	/* the queue for low priority operations */
	db_op_item_t *lowprio_first;
	db_op_item_t *lowprio_last;

	/* number of operations pending */
	unsigned int nb_waiting;

	pthread_mutex_t queues_mutex;

	pthread_cond_t work_avail_condition;
	pthread_cond_t work_done_condition;

	/* status (used for work_done_condition) */
	enum { NOT_READY, IDLE, WORKING, FINISHED } status;

} flusher_queue_t;

#define LOAD_ALL_STATEMENT  0
#define INSERT_STATEMENT    1
#define DELETE_STATEMENT    2

#define STATEMENT_COUNT     3

/* thread info */
typedef struct db_thread_info__ {
	pthread_t thr_id;
	unsigned int thr_index;

	flusher_queue_t work_queue;

	/* SQLite database connection */
	sqlite3 *db_conn;

	/* prepared statement table */
	sqlite3_stmt * prep_stmt[STATEMENT_COUNT];

	/* this pool is accessed by submitter
	 * and by the db thread */
	pthread_mutex_t pool_mutex;
	pool_t *dbop_pool;

} db_thread_info_t;

static char dbmap_dir[MAXPATHLEN];
static char db_tmpdir[MAXPATHLEN];
static unsigned int nb_db_threads;
static int synchronous;

/* used for clean shutdown */
static int do_terminate;

/* all information and context for threads */
static db_thread_info_t db_thread[MAX_DB];

/* test if a letter is hexa */
#define IS_HEXA(c)  \
	((((c) >= '0') && ((c) <= '9')) || (((c) >= 'A') && ((c) <= 'F')) \
	 || (((c) >= 'a') && ((c) <= 'f')))

/* converts an hexa letter */
#define HEXA2BYTE(c)							\
	((unsigned char)						\
	 (((c) >= '0') && ((c) <= '9') ?				\
	  ((c) - '0') : (((c) >= 'A') && ((c) <= 'F') ?			\
			 ((c) - 'A' + 10) : (((c) >= 'a') &&		\
					     ((c) <= 'f') ?		\
					     ((c) - 'a' + 10) : 0))))

/**
 * @brief Read a hexadecimal string into memory
 *
 * @param[out] target     Where memory is to be written
 * @param[in]  tgt_size   Size of the target buffer
 * @param[in]  str_source Hexadecimal string
 *
 * @retval The number of bytes read in the source string.
 * @retval -1 on error.
 */

int
sscanmem(void *target, size_t tgt_size, const char *str_source)
{

	unsigned char *mem;	/* the current byte to be set */

	const char *src;	/* pointer to the current char to be read. */

	int nb_read = 0;

	src = str_source;

	for (mem = (unsigned char *)target;
	     mem < ((unsigned char *)target + tgt_size); mem++) {

		unsigned char tmp_val;

		/* we must read 2 bytes (written in hexa) to have 1
		   target byte value. */
		if ((*src == '\0') || (*(src + 1) == '\0')) {
			/* error, the source string is too small */
			return -1;
		}

		/* they must be hexa values */
		if (!IS_HEXA(*src) || !IS_HEXA(*(src + 1)))
			return -1;

		/* we read hexa values. */
		tmp_val = (HEXA2BYTE(*src) << 4) + HEXA2BYTE(*(src + 1));

		/* we had them to the target buffer */
		(*mem) = tmp_val;

		src += 2;
		nb_read += 2;

	}

	return nb_read;

}

/* Initialize basic structures for a thread */
static int init_db_thread_info(db_thread_info_t *p_thr_info,
			       unsigned int nb_dbop_prealloc)
{
	unsigned int i;

	if (!p_thr_info)
		return HANDLEMAP_INTERNAL_ERROR;

	memset(p_thr_info, 0, sizeof(db_thread_info_t));

	p_thr_info->work_queue.highprio_first = NULL;
	p_thr_info->work_queue.highprio_last = NULL;
	p_thr_info->work_queue.lowprio_first = NULL;
	p_thr_info->work_queue.lowprio_last = NULL;

	p_thr_info->work_queue.nb_waiting = 0;

	if (pthread_mutex_init(&p_thr_info->work_queue.queues_mutex, NULL))
		return HANDLEMAP_SYSTEM_ERROR;

	if (pthread_cond_init
	    (&p_thr_info->work_queue.work_avail_condition, NULL))
		return HANDLEMAP_SYSTEM_ERROR;

	if (pthread_cond_init
	    (&p_thr_info->work_queue.work_done_condition, NULL))
		return HANDLEMAP_SYSTEM_ERROR;

	/* init thread status */
	p_thr_info->work_queue.status = NOT_READY;

	p_thr_info->db_conn = NULL;

	for (i = 0; i < STATEMENT_COUNT; i++)
		p_thr_info->prep_stmt[i] = NULL;

	/* init memory pool */

	if (pthread_mutex_init(&p_thr_info->pool_mutex, NULL))
		return HANDLEMAP_SYSTEM_ERROR;

	p_thr_info->dbop_pool =
	    pool_basic_init("drop_pool", sizeof(db_op_item_t));

	return HANDLEMAP_SUCCESS;
}

/* Called by a thread to initialize its database access.
 * After this call:
 *  - database connection is established
 *  - schema is created
 *  - prepared statements are ready to be used
 */
static int init_database_access(db_thread_info_t *p_thr_info)
{
	char db_file[MAXPATHLEN];
	int rc;
	char **result = NULL;
	int rows, cols;
	char *errmsg = NULL;
	const char *unparsed;

	/* first open the database file */

	rc = snprintf(db_file, sizeof(db_file), "%s/%s.%u", dbmap_dir,
		      DB_FILE_PREFIX, p_thr_info->thr_index);

	if (rc < 0) {
		LogCrit(COMPONENT_FSAL,
			"Unexpected return from snprintf %d error %s (%d)",
			rc, strerror(errno), errno);
		return HANDLEMAP_DB_ERROR;
	} else if (rc >= sizeof(db_file)) {
		LogCrit(COMPONENT_FSAL,
			"PROXY_V4 HANDLE DB path %s/%s.%u too long",
			dbmap_dir, DB_FILE_PREFIX, p_thr_info->thr_index);
		return HANDLEMAP_DB_ERROR;
	}

	rc = sqlite3_open(db_file, &p_thr_info->db_conn);

	if (rc != 0) {
		if (p_thr_info->db_conn) {
			LogCrit(COMPONENT_FSAL,
				"ERROR: could not connect to SQLite3 database (file %s): %s",
				db_file, sqlite3_errmsg(p_thr_info->db_conn));
			sqlite3_close(p_thr_info->db_conn);
		} else {
			LogCrit(COMPONENT_FSAL,
				"ERROR: could not connect to SQLite3 database (file %s): status=%d",
				db_file, rc);
		}
		return HANDLEMAP_DB_ERROR;
	}

	/* Now check, that the map table exists */
	rc = sqlite3_get_table(p_thr_info->db_conn,
			       "SELECT name FROM sqlite_master WHERE type = 'table' AND name = '"
			       MAP_TABLE "'", &result, &rows, &cols, &errmsg);

	CheckTable(p_thr_info->db_conn, rc, errmsg, result);

	/* no need for the result, just the number of rows returned */
	sqlite3_free_table(result);

	if (rows != 1) {
		/* table must be created */
		rc = sqlite3_exec(p_thr_info->db_conn,
				  "CREATE TABLE " MAP_TABLE " ( " OBJID_FIELD
				  "   BIGINT NOT NULL, " HASH_FIELD
				  "    INT NOT NULL, " HANDLE_FIELD
				  "  TEXT, PRIMARY KEY(" OBJID_FIELD ", "
				  HASH_FIELD ") )", NULL, NULL, &errmsg);

		CheckCommand(p_thr_info->db_conn, rc, errmsg);

	}

	/* Now, create prepared statements */

	rc = sqlite3_prepare_v2(p_thr_info->db_conn,
				"SELECT " OBJID_FIELD "," HASH_FIELD ","
				HANDLE_FIELD " FROM " MAP_TABLE, -1,
				&(p_thr_info->prep_stmt[LOAD_ALL_STATEMENT]),
				&unparsed);

	CheckPrepare(p_thr_info->db_conn, rc);

	rc = sqlite3_prepare_v2(p_thr_info->db_conn,
				"INSERT INTO " MAP_TABLE "(" OBJID_FIELD ","
				HASH_FIELD "," HANDLE_FIELD
				") VALUES (?1, ?2, ?3 )", -1,
				&(p_thr_info->prep_stmt[INSERT_STATEMENT]),
				&unparsed);

	CheckPrepare(p_thr_info->db_conn, rc);

	rc = sqlite3_prepare_v2(p_thr_info->db_conn,
				"DELETE FROM " MAP_TABLE " WHERE " OBJID_FIELD
				"=?1 AND " HASH_FIELD "=?2", -1,
				&(p_thr_info->prep_stmt[DELETE_STATEMENT]),
				&unparsed);

	CheckPrepare(p_thr_info->db_conn, rc);

	/* Everything is OK now ! */
	return HANDLEMAP_SUCCESS;

}				/* init_database_access */

static int db_load_operation(db_thread_info_t *p_info, hash_table_t *p_hash)
{
	/* the object id to be inserted to hash table */
	uint64_t object_id;
	unsigned int handle_hash;
	const char *fsal_handle_str;
	char fh4_data[NFS4_FHSIZE];
	unsigned int nb_loaded = 0;
	int rc;
	struct timeval t1;
	struct timeval t2;
	struct timeval tdiff;

	gettimeofday(&t1, NULL);

	rc = sqlite3_step(p_info->prep_stmt[LOAD_ALL_STATEMENT]);
	CheckStep(p_info->db_conn, rc, p_info->prep_stmt[LOAD_ALL_STATEMENT]);

	/* something to read */
	while (rc == SQLITE_ROW) {
		object_id =
		    sqlite3_column_int64(p_info->prep_stmt[LOAD_ALL_STATEMENT],
					 0);
		handle_hash =
		    sqlite3_column_int(p_info->prep_stmt[LOAD_ALL_STATEMENT],
				       1);
		fsal_handle_str =
		    sqlite3_column_text(p_info->prep_stmt[LOAD_ALL_STATEMENT],
					2);

		if (fsal_handle_str) {
			int len = strlen(fsal_handle_str);

			if ((len & 1) || len > NFS4_FHSIZE * 2) {
				LogEvent(COMPONENT_FSAL,
					 "Bogus handle '%s' - wrong number of symbols",
					 fsal_handle_str);
			} else {
				/* convert hexa string representation
				 * to binary data */
				if (sscanmem(fh4_data, len / 2, fsal_handle_str)
				    != len) {
					LogEvent(COMPONENT_FSAL,
						 "Bogus entry '%s' - cannot convert",
						 fsal_handle_str);
				} else {
					/* now insert it to the hash table */
					rc = handle_mapping_hash_add(
								p_hash,
								object_id,
								handle_hash,
								fh4_data,
								len / 2);

					if (rc == 0)
						nb_loaded++;
					else
						LogCrit(COMPONENT_FSAL,
							"ERROR %d adding entry to hash table <object_id=%llu, FH_hash=%u, FSAL_Handle=%s>",
							rc,
							(unsigned long long)
							object_id, handle_hash,
							fsal_handle_str);
				}
			}
		} else {
			LogEvent(COMPONENT_FSAL,
				 "Empty handle in object %lld, hash %d",
				 (unsigned long long)object_id, handle_hash);
		}

		rc = sqlite3_step(p_info->prep_stmt[LOAD_ALL_STATEMENT]);
		CheckStep(p_info->db_conn, rc,
			  p_info->prep_stmt[LOAD_ALL_STATEMENT]);

	}

	/* clear results */
	sqlite3_reset(p_info->prep_stmt[LOAD_ALL_STATEMENT]);

	/* print time and item count */

	gettimeofday(&t2, NULL);
	timersub(&t2, &t1, &tdiff);

	LogEvent(COMPONENT_FSAL, "Reloaded %u items in %d.%06ds", nb_loaded,
		 (int)tdiff.tv_sec, (int)tdiff.tv_usec);

	return HANDLEMAP_SUCCESS;

}				/* db_load_operation */

static int db_insert_operation(db_thread_info_t *p_info,
			       struct hdlmap_tuple *data)
{
	int rc;
	char handle_str[OPAQUE_BYTES_SIZE(NFS4_FHSIZE)];
	struct display_buffer dspbuf = {
				sizeof(handle_str), handle_str, handle_str};

	rc = sqlite3_bind_int64(p_info->prep_stmt[INSERT_STATEMENT], 1,
				data->nfs23_digest.object_id);
	CheckBind(p_info->db_conn, rc, p_info->prep_stmt[INSERT_STATEMENT]);

	rc = sqlite3_bind_int(p_info->prep_stmt[INSERT_STATEMENT], 2,
			      data->nfs23_digest.handle_hash);
	CheckBind(p_info->db_conn, rc, p_info->prep_stmt[INSERT_STATEMENT]);

	if (display_opaque_bytes_flags(&dspbuf, data->fh4_data, data->fh4_len,
				       OPAQUE_BYTES_UPPER) <= 0) {
		LogCrit(COMPONENT_FSAL, "Invalid file handle %s", handle_str);
		return HANDLEMAP_DB_ERROR;
	}

	rc = sqlite3_bind_text(p_info->prep_stmt[INSERT_STATEMENT], 3,
			       handle_str, -1, SQLITE_STATIC);
	CheckBind(p_info->db_conn, rc, p_info->prep_stmt[INSERT_STATEMENT]);

	rc = sqlite3_step(p_info->prep_stmt[INSERT_STATEMENT]);
	CheckStep(p_info->db_conn, rc, p_info->prep_stmt[INSERT_STATEMENT]);

	/* clear results */
	sqlite3_reset(p_info->prep_stmt[INSERT_STATEMENT]);

	return HANDLEMAP_SUCCESS;

}				/* db_insert_operation */

static int db_delete_operation(db_thread_info_t *p_info,
			       nfs23_map_handle_t *p_nfs23_digest)
{
	int rc;

	rc = sqlite3_bind_int64(p_info->prep_stmt[DELETE_STATEMENT], 1,
				p_nfs23_digest->object_id);
	CheckBind(p_info->db_conn, rc, p_info->prep_stmt[DELETE_STATEMENT]);

	rc = sqlite3_bind_int(p_info->prep_stmt[DELETE_STATEMENT], 2,
			      p_nfs23_digest->handle_hash);
	CheckBind(p_info->db_conn, rc, p_info->prep_stmt[DELETE_STATEMENT]);

	rc = sqlite3_step(p_info->prep_stmt[DELETE_STATEMENT]);
	CheckStep(p_info->db_conn, rc, p_info->prep_stmt[DELETE_STATEMENT]);

	/* clear results */
	sqlite3_reset(p_info->prep_stmt[DELETE_STATEMENT]);

	return HANDLEMAP_SUCCESS;

}				/* db_delete_operation */

/* push a task to the queue */
static int dbop_push(flusher_queue_t *p_queue, db_op_item_t *p_op)
{
	PTHREAD_MUTEX_lock(&p_queue->queues_mutex);

	/* add an item at the end of the queue */
	switch (p_op->op_type) {
	case LOAD:
	case INSERT:

		/* high priority operations */

		p_op->p_next = NULL;

		if (p_queue->highprio_last == NULL) {
			/* first operation */
			p_queue->highprio_first = p_op;
			p_queue->highprio_last = p_op;
		} else {
			p_queue->highprio_last->p_next = p_op;
			p_queue->highprio_last = p_op;
		}

		p_queue->nb_waiting++;

		break;

	case DELETE:

		/* low priority operation */

		p_op->p_next = NULL;

		if (p_queue->lowprio_last == NULL) {
			/* first operation */
			p_queue->lowprio_first = p_op;
			p_queue->lowprio_last = p_op;
		} else {
			p_queue->lowprio_last->p_next = p_op;
			p_queue->lowprio_last = p_op;
		}

		p_queue->nb_waiting++;

		break;

	default:
		LogCrit(COMPONENT_FSAL,
			"ERROR in dbop push: Invalid operation type %d",
			p_op->op_type);
	}

	/* there now some work available */
	pthread_cond_signal(&p_queue->work_avail_condition);

	PTHREAD_MUTEX_unlock(&p_queue->queues_mutex);

	return HANDLEMAP_SUCCESS;

}

static void *database_worker_thread(void *arg)
{
	db_thread_info_t *p_info = (db_thread_info_t *) arg;
	int rc;
	db_op_item_t *to_be_done = NULL;
	char thread_name[32];

	/* initialize logging */

	/* We don't care about too long string, truncated is fine and we don't
	 * expect EOVERRUN or EINVAL.
	 */
	(void) snprintf(thread_name, sizeof(thread_name),
			"DB thread #%u", p_info->thr_index);
	SetNameFunction(thread_name);

	/* initialize memory management */

	rc = init_database_access(p_info);

	if (rc != HANDLEMAP_SUCCESS) {
		/* Failed init */
		LogCrit(COMPONENT_FSAL,
			"ERROR: Database initialization error %d", rc);
		exit(rc);
	}

	/* main loop */
	while (1) {

		/* Is "work done" or "work available" condition verified ? */

		PTHREAD_MUTEX_lock(&p_info->work_queue.queues_mutex);

		/* nothing to be done ? */
		while (p_info->work_queue.highprio_first == NULL
		       && p_info->work_queue.lowprio_first == NULL) {
			to_be_done = NULL;
			p_info->work_queue.status = IDLE;
			pthread_cond_signal(
				&p_info->work_queue.work_done_condition);

			/* if termination is requested, exit */
			if (do_terminate) {
				p_info->work_queue.status = FINISHED;
				PTHREAD_MUTEX_unlock(&p_info->work_queue
								.queues_mutex);
				return (void *)p_info;
			}

			/* else, wait for something to do */
			pthread_cond_wait(
				&p_info->work_queue.work_avail_condition,
				&p_info->work_queue.queues_mutex);

		}

		/* there is something to do:
		 * first check the highest priority list,
		 * then the lower priority.
		 */

		if (p_info->work_queue.highprio_first != NULL) {
			/* take the next item in the list */
			to_be_done = p_info->work_queue.highprio_first;
			p_info->work_queue.highprio_first = to_be_done->p_next;

			/* still any entries in the list ? */
			if (p_info->work_queue.highprio_first == NULL)
				p_info->work_queue.highprio_last = NULL;
			/* it is the last entry ? */
			else if (p_info->work_queue.highprio_first->p_next ==
				 NULL)
				p_info->work_queue.highprio_last =
				    p_info->work_queue.highprio_first;

			/* something to do */
			p_info->work_queue.status = WORKING;
		} else if (p_info->work_queue.lowprio_first != NULL) {
			/* take the next item in the list */
			to_be_done = p_info->work_queue.lowprio_first;
			p_info->work_queue.lowprio_first = to_be_done->p_next;

			/* still any entries in the list ? */
			if (p_info->work_queue.lowprio_first == NULL)
				p_info->work_queue.lowprio_last = NULL;
			/* it is the last entry ? */
			else if (p_info->work_queue.lowprio_first->p_next ==
				 NULL)
				p_info->work_queue.lowprio_last =
				    p_info->work_queue.lowprio_first;

			/* something to do */
			p_info->work_queue.status = WORKING;
		}

		p_info->work_queue.nb_waiting--;

		PTHREAD_MUTEX_unlock(&p_info->work_queue.queues_mutex);

		/* PROCESS THE REQUEST */

		switch (to_be_done->op_type) {
		case LOAD:
			db_load_operation(p_info, to_be_done->op_arg.hash);
			break;

		case INSERT:
			db_insert_operation(p_info,
					    &to_be_done->op_arg.fh_info);
			break;

		case DELETE:
			db_delete_operation(
				p_info,
				&to_be_done->op_arg.fh_info.nfs23_digest);
			break;

		default:
			LogCrit(COMPONENT_FSAL,
				"ERROR: Invalid operation type %d",
				to_be_done->op_type);
		}

		/* free the db operation item */
		PTHREAD_MUTEX_lock(&p_info->pool_mutex);
		pool_free(p_info->dbop_pool, to_be_done);
		PTHREAD_MUTEX_unlock(&p_info->pool_mutex);

	}			/* loop forever */

	return (void *)p_info;
}

/**
 * count the number of database instances in a given directory
 * (this is used for checking that the number of db
 * matches the number of threads)
 */
int handlemap_db_count(const char *dir)
{
	DIR *dir_hdl;
	struct dirent *direntry;
	char db_pattern[MAXPATHLEN];
	int rc;
	unsigned int count = 0;
	int end_of_dir = false;

	rc = snprintf(db_pattern, sizeof(db_pattern), "%s.*[0-9]",
		      DB_FILE_PREFIX);

	if (rc < 0) {
		LogCrit(COMPONENT_FSAL,
			"Unexpected return from snprintf %d error %s (%d)",
			rc, strerror(errno), errno);
		return -HANDLEMAP_SYSTEM_ERROR;
	} else if (rc >= sizeof(db_pattern)) {
		LogCrit(COMPONENT_FSAL,
			"ERROR: db_pattern too long %s.*[0-9]", DB_FILE_PREFIX);
		return -HANDLEMAP_SYSTEM_ERROR;
	}

	dir_hdl = opendir(dir);

	if (dir_hdl == NULL) {
		LogCrit(COMPONENT_FSAL,
			"ERROR: could not access directory %s: %s", dir,
			strerror(errno));
		return -HANDLEMAP_SYSTEM_ERROR;
	}

	do {
		errno = 0;
		direntry = readdir(dir_hdl);

		if (direntry != NULL) {
			/* go to the next loop if the entry is . or .. */
			if (!strcmp(".", direntry->d_name)
			    || !strcmp("..", direntry->d_name))
				continue;

			/* does it match the expected db pattern ? */
			if (!fnmatch(db_pattern, direntry->d_name,
				     FNM_PATHNAME))
				count++;

		} else if (errno == 0) {
			/* end of dir */
			end_of_dir = true;
		} else {
			/* error */
			LogCrit(COMPONENT_FSAL,
				"ERROR: error reading directory %s: %s", dir,
				strerror(errno));

			closedir(dir_hdl);
			return -HANDLEMAP_SYSTEM_ERROR;
		}

	} while (!end_of_dir);

	closedir(dir_hdl);

	return count;

}				/* handlemap_db_count */

unsigned int select_db_queue(const nfs23_map_handle_t *p_nfs23_digest)
{
	unsigned int h =
	    ((p_nfs23_digest->object_id * 1049) ^ p_nfs23_digest->handle_hash) %
	    2477;

	h = h % nb_db_threads;

	return h;
}

/**
 * Initialize databases access
 * - init DB queues
 * - start threads
 * - establish DB connections
 * - create db schema if it was empty
 */
int handlemap_db_init(const char *db_dir, const char *tmp_dir,
		      unsigned int db_count, int synchronous_insert)
{
	unsigned int i;
	int rc;

	/* first, save the parameters */

	if (strlcpy(dbmap_dir, db_dir, sizeof(dbmap_dir)) >= sizeof(dbmap_dir))
		return HANDLEMAP_INVALID_PARAM;
	if (strlcpy(db_tmpdir, tmp_dir, sizeof(db_tmpdir))
	    >=  sizeof(db_tmpdir))
		return HANDLEMAP_INVALID_PARAM;

	if (db_count > MAX_DB)
		return HANDLEMAP_INVALID_PARAM;

	nb_db_threads = db_count;
	synchronous = synchronous_insert;
	/* set global database engine info */

	sqlite3_temp_directory = db_tmpdir;

	/* initialize structures for each thread and launch it */

	for (i = 0; i < nb_db_threads; i++) {
		rc = init_db_thread_info(&db_thread[i], 100);
		if (rc)
			return rc;

		db_thread[i].thr_index = i;

		rc = pthread_create(&db_thread[i].thr_id, NULL,
				    database_worker_thread, &db_thread[i]);
		if (rc)
			return HANDLEMAP_SYSTEM_ERROR;
	}

	/* I'm ready to serve, my Lord ! */
	return HANDLEMAP_SUCCESS;
}

/* wait that a thread has done all its jobs */
static void wait_thread_jobs_finished(db_thread_info_t *p_thr_info)
{

	PTHREAD_MUTEX_lock(&p_thr_info->work_queue.queues_mutex);

	/* wait until the thread has no more tasks in its queue
	 * and it is no more working
	 */
	while (p_thr_info->work_queue.highprio_first != NULL
	       || p_thr_info->work_queue.lowprio_first != NULL
	       || p_thr_info->work_queue.status == WORKING)
		pthread_cond_wait(&p_thr_info->work_queue.work_done_condition,
				  &p_thr_info->work_queue.queues_mutex);

	PTHREAD_MUTEX_unlock(&p_thr_info->work_queue.queues_mutex);

}

/**
 * Gives the order to each DB thread to reload
 * the content of its database and insert it
 * to the hash table.
 * The function blocks until all threads have loaded their data.
 */
int handlemap_db_reaload_all(hash_table_t *target_hash)
{
	unsigned int i;
	db_op_item_t *new_task;
	int rc;

	/* give the job to all threads */
	for (i = 0; i < nb_db_threads; i++) {
		/* get a new db operation  */
		PTHREAD_MUTEX_lock(&db_thread[i].pool_mutex);

		new_task = pool_alloc(db_thread[i].dbop_pool);

		PTHREAD_MUTEX_unlock(&db_thread[i].pool_mutex);

		/* can you fill it ? */
		new_task->op_type = LOAD;
		new_task->op_arg.hash = target_hash;

		rc = dbop_push(&db_thread[i].work_queue, new_task);

		if (rc)
			return rc;
	}

	/* wait for all threads to finish their job */

	for (i = 0; i < nb_db_threads; i++)
		wait_thread_jobs_finished(&db_thread[i]);

	return HANDLEMAP_SUCCESS;

}				/* handlemap_db_reaload_all */

/**
 * Submit a db 'insert' request.
 * The request is inserted in the appropriate db queue.
 */
int handlemap_db_insert(nfs23_map_handle_t *p_in_nfs23_digest,
			const void *data, uint32_t len)
{
	unsigned int i;
	db_op_item_t *new_task;
	int rc;

	if (!synchronous) {
		/* which thread is going to handle this inode ? */

		i = select_db_queue(p_in_nfs23_digest);

		/* get a new db operation  */
		PTHREAD_MUTEX_lock(&db_thread[i].pool_mutex);

		new_task = pool_alloc(db_thread[i].dbop_pool);

		PTHREAD_MUTEX_unlock(&db_thread[i].pool_mutex);

		/* fill the task info */
		new_task->op_type = INSERT;
		new_task->op_arg.fh_info.nfs23_digest = *p_in_nfs23_digest;
		memcpy(new_task->op_arg.fh_info.fh4_data, data, len);
		new_task->op_arg.fh_info.fh4_len = len;

		rc = dbop_push(&db_thread[i].work_queue, new_task);

		if (rc)
			return rc;
	}
	/* else: @todo not supported yet */

	return HANDLEMAP_SUCCESS;

}

/**
 * Submit a db 'delete' request.
 * The request is inserted in the appropriate db queue.
 * (always asynchronous)
 */
int handlemap_db_delete(nfs23_map_handle_t *p_in_nfs23_digest)
{
	unsigned int i;
	db_op_item_t *new_task;
	int rc;

	/* which thread is going to handle this inode ? */

	i = select_db_queue(p_in_nfs23_digest);

	/* get a new db operation  */
	PTHREAD_MUTEX_lock(&db_thread[i].pool_mutex);

	new_task = pool_alloc(db_thread[i].dbop_pool);

	PTHREAD_MUTEX_unlock(&db_thread[i].pool_mutex);

	/* fill the task info */
	new_task->op_type = DELETE;
	new_task->op_arg.fh_info.nfs23_digest = *p_in_nfs23_digest;

	rc = dbop_push(&db_thread[i].work_queue, new_task);

	if (rc)
		return rc;

	return HANDLEMAP_SUCCESS;

}

/**
 * Wait for all queues to be empty
 * and all current DB request to be done.
 */
int handlemap_db_flush(void)
{
	unsigned int i;
	struct timeval t1;
	struct timeval t2;
	struct timeval tdiff;
	unsigned int to_sync = 0;

	for (i = 0; i < nb_db_threads; i++)
		to_sync += db_thread[i].work_queue.nb_waiting;

	LogEvent(COMPONENT_FSAL,
		 "Waiting for database synchronization (%u operations pending)",
		 to_sync);

	gettimeofday(&t1, NULL);

	/* wait for all threads to finish their job */

	for (i = 0; i < nb_db_threads; i++)
		wait_thread_jobs_finished(&db_thread[i]);

	gettimeofday(&t2, NULL);

	timersub(&t2, &t1, &tdiff);

	LogEvent(COMPONENT_FSAL, "Database synchronized in %d.%06ds",
		 (int)tdiff.tv_sec, (int)tdiff.tv_usec);

	return HANDLEMAP_SUCCESS;

}
