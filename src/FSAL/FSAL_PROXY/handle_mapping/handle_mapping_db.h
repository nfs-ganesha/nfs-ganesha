#ifndef _HANDLE_MAPPING_DB_H
#define _HANDLE_MAPPING_DB_H

#include "handle_mapping.h"
#include "HashTable.h"

#define DB_FILE_PREFIX "handlemap.sqlite"

/* Database definition */
#define MAP_TABLE      "HandleMap"
#define OBJID_FIELD    "ObjectId"
#define HASH_FIELD     "HandleHash"
#define HANDLE_FIELD   "FSALHandle"

#define MAX_DB  32

/**
 * count the number of database instances in a given directory
 * (this is used for checking that the number of db
 * matches the number of threads)
 */
int handlemap_db_count(const char *dir);

/**
 * Initialize databases access
 * (init DB queues, start threads, establish DB connections,
 * and create db schema if it was empty).
 */
int handlemap_db_init(const char *db_dir,
                      const char *tmp_dir,
                      unsigned int db_count,
                      unsigned int nb_dbop_prealloc, int synchronous_insert);

/**
 * Gives the order to each DB thread to reload
 * the content of its database and insert it
 * to the hash table.
 * The function blocks until all threads have loaded their data.
 */
int handlemap_db_reaload_all(hash_table_t * target_hash);

/**
 * Submit a db 'insert' request.
 * The request is inserted in the appropriate db queue.
 */
int handlemap_db_insert(nfs23_map_handle_t * p_in_nfs23_digest,
                        fsal_handle_t * p_in_handle);

/**
 * Submit a db 'delete' request.
 * The request is inserted in the appropriate db queue.
 * (always asynchronous)
 */
int handlemap_db_delete(nfs23_map_handle_t * p_in_nfs23_digest);

/**
 * Wait for all queues to be empty
 * and all current DB request to be done.
 */
int handlemap_db_flush();

#endif
