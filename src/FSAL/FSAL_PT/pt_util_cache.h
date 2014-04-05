/*
 * ----------------------------------------------------------------------------
 * Copyright IBM Corp. 2010, 2011
 * All Rights Reserved
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * Filename:    pt_util_cache.h
 * Description: Define common macros and data types for caching purpose
 * Author:      FSI IPC team
 * ----------------------------------------------------------------------------
 */
#ifndef PT_UTIL_CACHE_H_
#define PT_UTIL_CACHE_H_

#define CACHE_MAX_NUM_CACHE_ENTRY(_CACHE_TABLE) \
	(sizeof(_CACHE_TABLE)/sizeof(CACHE_DATA_TYPE_T))

/*
 * An enum representing what the purpose of this cache table is
 * This is mostly for facilitating debugging in the log
 */
typedef enum {
	CACHE_ID_192_FRONT_END_HANDLE_TO_NAME_CACHE = 1,
	CACHE_ID_2500_BACK_END_HANDLE_TO_NAME_CACHE = 2
} CACHE_ID_ENUMS;

typedef struct {
	int keyLengthInBytes;	/* Length (in bytes) of the key */
	int dataSizeInBytes;	/* Data size (in bytes) */
	/* How big the cache table should be at max */
	int maxNumOfCacheEntries;
	/* This is used to identify which cache this is.
	 *  Defined in CACHE_ID_* enum */
	CACHE_ID_ENUMS cacheTableID;

	/*
	 * -------------------------------------------------------------------
	 * Compare two key entry and indicates the order of those two entries
	 * This is intended for the use of binary search and binary insertion
	 * routine used in this cache utilities
	 *
	 * Return:  1 if string1 >  string2
	 *          0 if string1 == string2
	 *         -1 if string1 <  string2
	 */
	int (*cacheKeyComprefn) (const void *cacheEntry1,
				 const void *cacheEntry2);
} CACHE_TABLE_INIT_PARAM;

typedef struct {
	/*
	 * NOTE: cache entries are not pre-declared array.  They only contains
	 *       pointers to memory location where the real information is
	 *       stored for the cached entries.
	 */
	void *key;		/* Pointer to cached Key */
	void *data;		/* Pointer Cached data */
} CACHE_TABLE_ENTRY_T;

typedef struct {
	/* How many elements currently in the cache table */
	int numElementsOccupied;
	/* How big this cache table can hold at max */
	int maxNumOfCacheEntries;
	/* Length of the key in the cache entry */
	int keyLengthInBytes;
	/* Length of the data in the cache entry */
	int dataSizeInBytes;
	/* This is used to identify which cache this is. */
	CACHE_ID_ENUMS cacheTableID;
	/*
	 * Defined in CACHE_ID_* enum
	 * Function pointer to the comparison function
	 */
	int (*cacheKeyComprefn) (const void *key1, const void *key2);
} CACHE_TABLE_META_DATA_T;

typedef struct {
	CACHE_TABLE_META_DATA_T cacheMetaData;
	CACHE_TABLE_ENTRY_T *cacheEntries;
} CACHE_TABLE_T;

typedef struct {
	/* This is for fsi_get_name_from_handle() lookup
	 * This should have the name length of PATH_MAX
	 */
	char m_name[PATH_MAX];
	/* We record handle index if there is one for this name */
	int handle_index;
} CACHE_ENTRY_DATA_HANDLE_TO_NAME_T;
int fsi_cache_handle2name_keyCompare(const void *cacheEntry1,
				     const void *cacheEntry2);

int fsi_cache_table_init(CACHE_TABLE_T *cacheTableToInit,
			 CACHE_TABLE_INIT_PARAM *cacheTableInitParam);

int fsi_cache_getInsertionPoint(CACHE_TABLE_T *cacheTable,
				CACHE_TABLE_ENTRY_T *whatToInsert,
				int *whereToInsert);
int fsi_cache_insertEntry(CACHE_TABLE_T *cacheTable,
			  CACHE_TABLE_ENTRY_T *whatToInsert);
int fsi_cache_deleteEntry(CACHE_TABLE_T *cacheTable,
			  CACHE_TABLE_ENTRY_T *whatToDelete);
int fsi_cache_getEntry(CACHE_TABLE_T *cacheTable,
		       CACHE_TABLE_ENTRY_T *buffer);
void fsi_cache_handle2name_dumpTableKeys(fsi_ipc_trace_level logLevel,
					 CACHE_TABLE_T *cacheTable,
					 char *titleString);
void fsi_cache_32Bytes_rawDump(fsi_ipc_trace_level loglevel, void *data,
			       int index);
extern CACHE_TABLE_T g_fsi_name_handle_cache_opened_files;
#endif				/* PT_UTIL_CACHE_H_ */
