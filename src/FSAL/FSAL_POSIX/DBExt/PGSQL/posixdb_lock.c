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
  PGresult *p_res;
  char devid_str[MAX_DEVICEIDSTR_SIZE];
  char inode_str[MAX_INODESTR_SIZE];
  const char *paramValues[2];

  CheckConn(p_conn);

  BeginTransaction(p_conn, p_res);

  snprintf(devid_str, MAX_DEVICEIDSTR_SIZE, "%lli", (long long int)p_info->devid);
  snprintf(inode_str, MAX_INODESTR_SIZE, "%lli", (long long int)p_info->inode);
  paramValues[0] = devid_str;
  paramValues[1] = inode_str;
  p_res = PQexecPrepared(p_conn, "lookupHandleByInodeFU", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);
  PQclear(p_res);

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
  PGresult *p_res;
  PGTransactionStatusType transStatus = PQtransactionStatus(p_conn);

  if(transStatus == PQTRANS_ACTIVE || transStatus == PQTRANS_INTRANS)
    {
      RollbackTransaction(p_conn, p_res);
    }

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
