/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "posixdb_internal.h"
#include <string.h>

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
                                                       p_info /* IN */ )
{
  result_handle_t res;
  fsal_posixdb_status_t st;
  char query[2048];

  BeginTransaction(p_conn);

  snprintf(query, 2048, "SELECT handleid, handlets, nlink, ctime, ftype "
           "FROM Handle WHERE deviceid=%llu AND inode=%llu "
           "FOR UPDATE", (unsigned long long)p_info->devid, (unsigned long long)p_info->inode);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    {
      RollbackTransaction(p_conn);
      return st;
    }

  mysql_free_result(res);

  /* Do not end the transaction, because it will be closed by the next call to a posixdb function */

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

/** 
 * @brief Unlock the Handle line previously locked by fsal_posixdb_lockHandleForUpdate
 * 
 * @param p_conn
 *        Database connection
 * 
 * @return ERR_FSAL_POSIXDB_NOERR if no error,
 *         another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_cancelHandleLock(fsal_posixdb_conn * p_conn /* IN */ )
{
  RollbackTransaction(p_conn);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
