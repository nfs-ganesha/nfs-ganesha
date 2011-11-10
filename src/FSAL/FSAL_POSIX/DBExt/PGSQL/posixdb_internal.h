/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */

#include "fsal_types.h"

#ifndef _POSIXDB_INTERNAL_H
#define _POSIXDB_INTERNAL_H

#define MAX_HANDLEIDSTR_SIZE 21 // (size for "18446744073709551616" + 1 char for '\0')
#define MAX_DEVICEIDSTR_SIZE 21 // (size for "18446744073709551616" + 1 char for '\0')
#define MAX_INODESTR_SIZE 21    // (size for "18446744073709551616" + 1 char for '\0')
#define MAX_HANDLETSSTR_SIZE 11 // (size for "4294967296" + 1 char for '\0')
#define MAX_CTIMESTR_SIZE 11
#define MAX_NLINKSTR_SIZE 11
#define MAX_FTYPESTR_SIZE 11

#define ReturnCodeDB( _code_, _minor_ ) do {                 \
               fsal_posixdb_status_t _struct_status_;        \
               if(isFullDebug(COMPONENT_FSAL))               \
                 {                                           \
                   LogCrit(COMPONENT_FSAL, "Exiting %s ( %s:%i ) with status code = %i/%i\n", __FUNCTION__, __FILE__, __LINE__ - 2, _code_, _minor_ ); \
                 }                                           \
               (_struct_status_).major = (_code_) ;          \
               (_struct_status_).minor = (_minor_) ;         \
               return (_struct_status_);                     \
              } while(0)

#define CheckCommand( _res_ ) do { \
                              if (PQresultStatus( _res_ ) != PGRES_COMMAND_OK)                        \
                                {                                                                       \
                                    LogCrit(COMPONENT_FSAL, "PGSQL Command Failed in %s ( %s:%i ) with %s", __FUNCTION__, __FILE__, __LINE__, (PQresultErrorMessage(_res_))); \
                                    PQclear(_res_);                                                     \
                                    RollbackTransaction( p_conn, _res_ );  \
                                    ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, (PQresultStatus( _res_ )));        \
                                } \
                              } while (0)

#define CheckResult( _res_ ) do { \
                              if (PQresultStatus( _res_ ) != PGRES_TUPLES_OK)                        \
                                {                                                                       \
                                    LogCrit(COMPONENT_FSAL, "PGSQL Select Failed in %s ( %s:%i ) with %s", __FUNCTION__, __FILE__, __LINE__, (PQresultErrorMessage(_res_))); \
                                    PQclear(_res_);                     \
                                    RollbackTransaction( p_conn, _res_ );  \
                                    ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, (PQresultStatus( _res_ )));        \
                                } \
                              } while (0)

#define CheckConn( _p_conn_ ) do { \
                                if (PQstatus( _p_conn_ ) != CONNECTION_OK) { \
                                  LogCrit(COMPONENT_FSAL, "Reconnecting to database..."); \
                                  PQreset( _p_conn_ );\
                                  if (PQstatus( _p_conn_ ) != CONNECTION_OK) ReturnCodeDB(ERR_FSAL_POSIXDB_BADCONN, PQstatus( _p_conn_ ));/* Connexion still bad */ \
                                  fsal_posixdb_initPreparedQueries( _p_conn_ ); \
                                } \
                              } while (0)

#define BeginTransaction( _conn_, _res_ )   do { \
                                              PGTransactionStatusType transStatus = PQtransactionStatus(_conn_);\
                                              if (transStatus != PQTRANS_ACTIVE && transStatus != PQTRANS_INTRANS) { \
                                                _res_ = PQexec(_conn_, "BEGIN"); \
                                                CheckCommand(_res_); \
                                                PQclear(_res_); \
                                              } \
                                            } while (0)

#define EndTransaction( _conn_, _res_ )     do { \
                                              _res_ = PQexec(_conn_, "END"); \
                                              CheckCommand(_res_); \
                                              PQclear(_res_); \
                                            } while (0)

#define RollbackTransaction( _conn_, _res_ )     do { \
                                              _res_ = PQexec(_conn_, "ROLLBACK"); \
                                              PQclear(_res_); \
                                            } while (0)

/**
 * fsal_posixdb_buildOnePath:
 * Build the path of an object with only one Path in the parent table (usually a directory).
 *
 * \param p_conn (input)
 *    Connection to the database
 * \param p_handle (input)
 *    Handle of the object we want the path
 * \param path (output)
 *    Path of the object
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *           Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_buildOnePath(fsal_posixdb_conn * p_conn,     /* IN */
                                                posixfsal_handle_t * p_handle,  /* IN */
                                                fsal_path_t * p_path /* OUT */ );

/**
 * fsal_posixdb_recursiveDelete:
 * Delete a handle and all its entries in the Parent table. If the object is a directory, then all its entries will be recursively deleted.
 *
 * \param p_conn (input)
 *    Connection to the database
 * \param handleid_str (input)
 *    ID part of the handle
 * \param handlets_str (input)
 *    Timestamp part of the handle
 * \param ftype (input)
 *    Type of the object (regular file, directory, ...)
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *           Another error code else.
 */

fsal_posixdb_status_t fsal_posixdb_recursiveDelete(fsal_posixdb_conn * p_conn,  /* IN */
                                                   char *handleid_str,  /* IN */
                                                   char *handlets_str,  /* IN */
                                                   fsal_nodetype_t ftype);      /* IN */

/**
 * fsal_posixdb_deleteParent:
 * Delete a parent entry. If the handle has no more links, then it is also deleted.
 * Notice : do not use with a directory
 *
 * \param p_conn (input)
 *    Connection to the database
 * \param handleid_str (input)
 *    ID part of the handle of the object 
 * \param handlets_str (input)
 *    Timestamp part of the handle of the parent directory
 * \param handleidparent_str (input)
 *    ID part of the handle of the object
 * \param handletsparent_str (input)
 *    Timestamp part of the handle of the parent directory
 * \param filename (input)
 *    Filename of the entry to delete
 * \param nlink (input)
 *    Number of hardlink on the object
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *           Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_deleteParent(fsal_posixdb_conn * p_conn,     /* IN */
                                                char *handleid_str,     /* IN */
                                                char *handlets_str,     /* IN */
                                                char *handleidparent_str,       /* IN */
                                                char *handletsparent_str,       /* IN */
                                                char *filename, /* IN */
                                                int nlink) /* IN */ ;

/**
 * fsal_posixdb_internal_delete:
 * Delete a Parent entry knowing its parent handle and its name
 *
 * \see fsal_posixdb_delete
 */
fsal_posixdb_status_t fsal_posixdb_internal_delete(fsal_posixdb_conn * p_conn,  /* IN */
                                                   char *handleidparent_str,    /* IN */
                                                   char *handletsparent_str,    /* IN */
                                                   char *filename,      /* IN */
                                                   fsal_posixdb_fileinfo_t *
                                                   p_object_info /* IN */ );

/**
 * fsal_posixdb_initPreparedQueries:
 * setup the prepared queries for a new connection
 *
 * \param p_conn (input)
 *    Connection to the database
 * \return - ERR_FSAL_POSIXDB_NOERR, if no error.
 *           Another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_initPreparedQueries(fsal_posixdb_conn * p_conn);

/** 
 * @brief Fill a fsal_posixdb_fileinfo_t struct from char* values
 * 
 * @param p_info 
 * @param devid_str 
 * @param inode_str 
 * @param nlink_str 
 * @param ctime_str 
 * @param ftype_str 
 * 
 * @return ERR_FSAL_POSIXDB_NOERR, if no error
 *         ERR_FSAL_POSIXDB_FAULT if p_info is NULL
 */
fsal_posixdb_status_t posixdb_internal_fillFileinfoFromStrValues(fsal_posixdb_fileinfo_t *
                                                                 p_info, char *devid_str,
                                                                 char *inode_str,
                                                                 char *nlink_str,
                                                                 char *ctime_str,
                                                                 char *ftype_str);

/* this manages a mini-cache for the last few handles handled
 * it is invalidated as soon has there is a modification in the database
 */

/* enter an entry in cache path */

void fsal_posixdb_CachePath(posixfsal_handle_t * p_handle,      /* IN */
                            fsal_path_t * p_path /* IN */ );

/* invalidate cache in case of a modification */

void fsal_posixdb_InvalidateCache();

/* get a path from the cache
 * return true if the entry is found,
 * false else.
 */

int fsal_posixdb_GetPathCache(posixfsal_handle_t * p_handle,    /* IN */
                              fsal_path_t * p_path /* OUT */ );

/* update informations about a handle */
int fsal_posixdb_UpdateInodeCache(posixfsal_handle_t * p_handle);       /* IN */

/* retrieve last informations about a handle */
int fsal_posixdb_GetInodeCache(posixfsal_handle_t * p_handle);  /* IN/OUT */

#endif
