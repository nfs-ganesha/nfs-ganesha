/**
 *  POSIXDB
 */

#ifndef _FSAL_POSIX_DBEXT_H
#define _FSAL_POSIX_DBEXT_H

/*
 * DB relative includes & defines
 */
#ifdef _USE_PGSQL
/*
 * POSTGRESQL
 */
#include <libpq-fe.h>
#define fsal_posixdb_conn PGconn

#elif defined(_USE_MYSQL)
/*
 * MYSQL
 */
#include <mysql.h>
#include <mysqld_error.h>
#include <errmsg.h>

/* index for prepared requests */

#define BUILDONEPATH          0

#define NB_PREPARED_REQ       1

typedef struct fsal_posixdb_conn__
{
  MYSQL db_conn;

  /* array of prepared requests */
  MYSQL_STMT *stmt_tab[NB_PREPARED_REQ];

} fsal_posixdb_conn;

#elif defined(_USE_SQLITE3)
/*
 * SQLite v3
 */
#include <sqlite3.h>

/* index for prepared requests */

#define BUILDONEPATH          0
#define LOOKUPPATHS           1
#define LOOKUPPATHSEXT        2
#define LOOKUPHANDLEBYNAME    3
#define LOOKUPHANDLEBYNAMEFU  4
#define LOOKUPROOTHANDLE      5
#define LOOKUPHANDLEBYINODEFU 6
#define LOOKUPHANDLEFU        7
#define LOOKUPHANDLE          8
#define UPDATEHANDLE          9
#define UPDATEHANDLENLINK     10
#define LOOKUPPARENT          11
#define LOOKUPCHILDRENFU      12
#define LOOKUPCHILDREN        13
#define COUNTCHILDREN         14
#define INSERTHANDLE          15
#define UPDATEPARENT          16
#define INSERTPARENT          17
#define DELETEPARENT          18
#define DELETEHANDLE          19

#ifndef  HOST_NAME_MAX
#define HOST_NAME_MAX          64
#endif

#define NB_PREPARED_REQ       20

typedef struct fsal_posixdb_conn__
{
  sqlite3 *db_conn;

  /* array of prepared requests */
  sqlite3_stmt *stmt_tab[NB_PREPARED_REQ];

} fsal_posixdb_conn;

#else

#error "No DB compilation flag set for POSIXDB."

#endif

/*
 * DB independant definitions 
 */

#ifdef _APPLE
#define FSAL_MAX_DBHOST_NAME_LEN 64
#else
#define FSAL_MAX_DBHOST_NAME_LEN  HOST_NAME_MAX
#endif

#define FSAL_MAX_DBPORT_STR_LEN   8
#define FSAL_MAX_DB_NAME_LEN      64

#ifdef _APPLE
#define FSAL_MAX_DB_LOGIN_LEN    256
#else
#define FSAL_MAX_DB_LOGIN_LEN     LOGIN_NAME_MAX
#endif

#define FSAL_POSIXDB_MAXREADDIRBLOCKSIZE 64

#ifndef _USE_SQLITE3

typedef struct
{
  char host[FSAL_MAX_DBHOST_NAME_LEN];
  char port[FSAL_MAX_DBPORT_STR_LEN];
  char dbname[FSAL_MAX_DB_NAME_LEN];
  char login[FSAL_MAX_DB_LOGIN_LEN];
  char passwdfile[PATH_MAX];
} fsal_posixdb_conn_params_t;

#else

typedef struct
{
  char dbfile[FSAL_MAX_PATH_LEN];
  char tempdir[FSAL_MAX_PATH_LEN];
} fsal_posixdb_conn_params_t;

#endif

typedef struct
{
  posixfsal_handle_t handle;
  fsal_name_t name;
} fsal_posixdb_child;

/*
 * POSIXDB structures (for status and results)
 */

typedef enum fsal_posixdb_errorcode
{
  ERR_FSAL_POSIXDB_NOERR = 0,   /* no error */
  ERR_FSAL_POSIXDB_BADCONN,     /* not connected to the database */
  ERR_FSAL_POSIXDB_NOENT,       /* no such object in the database */
  ERR_FSAL_POSIXDB_CMDFAILED,   /* a command failed */
  ERR_FSAL_POSIXDB_FAULT,       /* sanity check failed, ... */
  ERR_FSAL_POSIXDB_NOPATH,
  ERR_FSAL_POSIXDB_TOOMANYPATHS,
  ERR_FSAL_POSIXDB_PATHTOOLONG,
  ERR_FSAL_POSIXDB_CONSISTENCY, /* entry is not consistent */
  ERR_FSAL_POSIXDB_NO_MEM       /* allocation error */
} fsal_posixdb_errorcode;

typedef struct fsal_posixdb_status_t
{
  int major;
  int minor;
} fsal_posixdb_status_t;

#define FSAL_POSIXDB_IS_ERROR( _status_ ) \
          ( ! ( ( _status_ ).major == ERR_FSAL_POSIXDB_NOERR ) )

#define FSAL_POSIXDB_IS_NOENT( _status_ ) \
          ( ( _status_ ).major == ERR_FSAL_POSIXDB_NOENT )

/* for initializing DB cache */
int fsal_posixdb_cache_init();

/**
 * fsal_posixdb_connect:
 * Connect to the Database and initialize *dbconn.
 *
 * \param params (input)
 *    Connection parameters
 * \param conn (output)
 *    Connection to the database
 * \return - FSAL_POSIXDB_NOERR, if no error.
             Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_connect(fsal_posixdb_conn_params_t * p_params,       /* IN */
                                           fsal_posixdb_conn ** p_conn /* OUT */ );

/**
 * fsal_posixdb_disconnect:
 * Disconnect from the Database.
 *
 * \param conn (input)
 *    Connection to the database
 * \return - FSAL_POSIXDB_NOERR, if no error.
             Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_disconnect(fsal_posixdb_conn * p_conn);

/**
 * fsal_posixdb_getInfoFromName:
 * Return informations known about an object, knowing the FSAL handle of its parent and its name.
 * 
 * \param conn (input)
 *        Database connection
 * \param p_parent_directory_handle (input)
 *        Handle of the parent directory to search the object in.
 * \param p_objectname (input)
 *        The name of the object to find.
 * \param p_path (optional output)
 *        path to the objects
 * \param p_handle (output)
 *        Handle of the object found in the database
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - FSAL_POSIXDB_NOENT, if directory unknown, or no object named 'object_name' in that directory.
 */
fsal_posixdb_status_t fsal_posixdb_getInfoFromName(fsal_posixdb_conn * p_conn,  /* IN */
                                                   posixfsal_handle_t * p_parent_directory_handle,      /* IN */
                                                   fsal_name_t * p_objectname,  /* IN */
                                                   fsal_path_t * p_path,        /* OUT */
                                                   posixfsal_handle_t *
                                                   p_handle /* OUT */ );

/**
 * fsal_posixdb_getInfoFromHandle:
 * Fills informations known about an object (inside its posixfsal_handle_t object), its 'paths', knowing its FSAL handle.
 * 
 * \param conn (input)
 *        Database connection
 * \param object_handle (input)
 *        Handle of the object.
 * \param p_paths (output)
 *        Array of path to the objects
 * \param paths_size (input)
 *        Number of elements of p_paths allocated
 * \param p_count (output)
 *        Number of paths set in p_paths
 * \param p_infos (output)
 *        POSIX information stored in the database.
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - FSAL_POSIXDB_UNKNOWHANDLE, if object_handle is unknown.
 */
fsal_posixdb_status_t fsal_posixdb_getInfoFromHandle(fsal_posixdb_conn * p_conn,        /* IN */
                                                     posixfsal_handle_t * p_object_handle,      /* IN */
                                                     fsal_path_t * p_paths,     /* OUT */
                                                     int paths_size,    /* IN */
                                                     int *p_count /* OUT */ );

/**
 * fsal_posixdb_add:
 * Add an object in the database (identified by its name and its parent directory). 
 * Create an entry in the Handle table if needed, and add (or replace existing) an entry in the Path table.
 *
 * \param conn (input)
 *        Database connection
 * \param p_object_info (input):
 *        POSIX information of the object to add (device ID, inode, ...)
 * \param p_parent_directory_handle (input):
 *        Handle of the parent directory of the object to add.
 * \param p_filename (input):
 *        The name of the object to add.
 * \param p_object_handle (output):
 *        FSAL handle of the added entry.
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_add(fsal_posixdb_conn * p_conn,      /* IN */
                                       fsal_posixdb_fileinfo_t * p_object_info, /* IN */
                                       posixfsal_handle_t * p_parent_directory_handle,  /* IN */
                                       fsal_name_t * p_filename,        /* IN */
                                       posixfsal_handle_t * p_object_handle /* OUT */ );

/**
 * fsal_posixdb_replace:
 * Move an object in the Path table (identified by its name and its parent directory). 
 * Create an entry in the Handle table if needed, and update (or create) an entry in the Path table.
 *
 * \param conn (input)
 *        Database connection
 * \param p_object_info (input):
 *        POSIX information of the object to add (device ID, inode, ...)
 * \param p_parent_directory_handle_old (input):
 *        Handle of the source directory of the object.
 * \param p_filename_old (input):
 *        The old name of the object.
 * \param p_parent_directory_handle_new (input):
 *        Handle of the new directory of the object.
 * \param p_filename_new (input):
 *        The new name of the object.
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_replace(fsal_posixdb_conn * p_conn,  /* IN */
                                           fsal_posixdb_fileinfo_t * p_object_info,     /* IN */
                                           posixfsal_handle_t * p_parent_directory_handle_old,  /* IN */
                                           fsal_name_t * p_filename_old,        /* IN */
                                           posixfsal_handle_t * p_parent_directory_handle_new,  /* IN */
                                           fsal_name_t * p_filename_new /* IN */ );

/**
 * fsal_posixdb_delete:
 * Delete a path entry for an object. If the file has only one path (hardlink = 1), its handle is deleted too.
 *
 * \param p_conn (input)
 *        Database connection
 * \param p_parent_directory_handle (input):
 *        Handle of the directory where the object is.
 * \param p_filename (input):
 *        The name of the object to delete.
 * \param p_object_info (input):
 *        POSIX information of the object (device ID, inode, ...)
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - FSAL_POSIXDB_NOENT, if parent_directory_handle does not exist.
 */
fsal_posixdb_status_t fsal_posixdb_delete(fsal_posixdb_conn * p_conn,   /* IN */
                                          posixfsal_handle_t * p_parent_directory_handle,       /* IN */
                                          fsal_name_t * p_filename,     /* IN */
                                          fsal_posixdb_fileinfo_t *
                                          p_object_info /* IN */ );

/**
 * fsal_posixdb_deleteHandle:
 * Delete a Handle and all paths related to him.
 *
 * \param p_conn (input)
 *        Database connection
 * \param p_handle (input):
 *        Handle of the directory where the object is.
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - FSAL_POSIXDB_NOENT, if p_handle does not exist.
 */
fsal_posixdb_status_t fsal_posixdb_deleteHandle(fsal_posixdb_conn * p_conn,     /* IN */
                                                posixfsal_handle_t * p_handle /* IN */ );

/**
 * fsal_posixdb_getChildren:
 * retrieve all the children of a directory handle.
 *
 * \param p_conn (input)
 *        Database connection
 * \param p_parent_directory_handle (input):
 *        Handle of the directory where the objects to be retrieved are.
 * \param max_count
 *        max fsal_posixdb_child to be returned
 * \param p_children:
 *        Children of p_parent_directory_handle. It is dynamically allocated inside the function. It have to be freed outside the function !!!
 * \param p_count:
 *        Number of children returned in p_children
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_getChildren(fsal_posixdb_conn * p_conn,      /* IN */
                                               posixfsal_handle_t * p_parent_directory_handle,  /* IN */
                                               unsigned int max_count, fsal_posixdb_child ** p_children,        /* OUT */
                                               unsigned int *p_count /* OUT */ );

/**
 * fsal_posixdb_export:
 * Export the content of the database to a file.
 *
 * \param p_conn (input)
 *        Database connection
 * \param out (input):
 *        POSIX file descriptor
 */
fsal_posixdb_status_t fsal_posixdb_export(fsal_posixdb_conn * p_conn,   /* IN */
                                          FILE * out /* IN */ );

/**
 * fsal_posixdb_import:
 * Import the content of the database from a file.
 *
 * \param p_conn (input)
 *        Database connection
 * \param in (input):
 *        POSIX file descriptor
 */
fsal_posixdb_status_t fsal_posixdb_import(fsal_posixdb_conn * p_conn,   /* IN */
                                          FILE * in /* IN */ );

/**
 * fsal_posixdb_flush:
 * Empty the database.
 *
 * \param p_conn (input)
 *        Database connection
 */
fsal_posixdb_status_t fsal_posixdb_flush(fsal_posixdb_conn * p_conn /* IN */ );

/**
 * fsal_posixdb_getParentDirHandle:
 * get the parent directory of a directory (p_object_handle must have only one Parent entry).
 *
 * \param p_conn (input)
 *        Database connection
 * \param p_object_handle (input):
 *        Directory Handle we want the parent
 * \param p_parent_directory_handle (output):
 *        Parent directory handle ( corresponding to <path to p_object_handle>/.. )
 */
fsal_posixdb_status_t fsal_posixdb_getParentDirHandle(fsal_posixdb_conn * p_conn,       /* IN */
                                                      posixfsal_handle_t * p_object_handle,     /* IN */
                                                      posixfsal_handle_t * p_parent_directory_handle    /* OUT */
    );

/** 
 * @brief Lock the line of the Handle table with inode & devid defined in p_info
 * 
 * @param p_conn
 *        Database connection
 * @param p_info 
 *        Information about the file
 * 
 * @return ERR_FSAL_POSIXDB_NOERR if no error,
 *         another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_lockHandleForUpdate(fsal_posixdb_conn * p_conn,      /* IN */
                                                       fsal_posixdb_fileinfo_t *
                                                       p_info /* IN */ );

/** 
 * @brief Unlock the Handle line previously locked by fsal_posixdb_lockHandleForUpdate
 * 
 * @param p_conn
 *        Database connection
 * 
 * @return ERR_FSAL_POSIXDB_NOERR if no error,
 *         another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_cancelHandleLock(fsal_posixdb_conn * p_conn /* IN */ );

#endif
