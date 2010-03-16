
#define ECL_MAX_ERRMSG_LEN  1024

/**
 * Loads a file into datacache.
 * \param filepath (in): the path of the object to be datacached
 * \param errmsg (out): an explanation string to be returned to administrator
 * \return 0 if no error occured,
 *         an error code else.
 */
int ECL_datacache_load_file(const char *filepath, char *errmsg);

/**
 * Synchronize a datacached file to filesystem (flush it but keep it in cache).
 * \param filepath (in): the path of the object to be synchronized
 * \param errmsg (out): an explanation string to be returned to administrator
 * \return 0 if no error occured,
 *         an error code else.
 */
int ECL_datacache_sync_file(const char *filepath, char *errmsg);

/**
 * Flush a datacached file to filesystem (flush it and remove it from cache).
 * \param filepath (in): the path of the object to be flushed
 * \param errmsg (out): an explanation string to be returned to administrator
 * \return 0 if no error occured,
 *         an error code else.
 */
int ECL_datacache_flush_file(const char *filepath, char *errmsg);

/**
 * Reload a file from filesystem to datacache (WARNING: this overwrites the datacached version !!!).
 * \param filepath (in): the path of the object to be reloaded
 * \param errmsg (out): an explanation string to be returned to administrator
 * \return 0 if no error occured,
 *         an error code else.
 */
int ECL_datacache_reload_file(const char *filepath, char *errmsg);

/**
 * Properly stop ganesha NFS server.
 * \param errmsg (out): an explanation string to be returned to administrator
 * \return 0 if no error occured,
 *         an error code else.
 */
int ECL_halt_server(char *errmsg);
