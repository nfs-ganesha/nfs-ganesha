/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "posixdb_internal.h"

fsal_posixdb_status_t fsal_posixdb_flush(fsal_posixdb_conn * p_conn)
{
  fsal_posixdb_status_t rc;

  rc = db_exec_sql(p_conn, "DELETE FROM Parent", NULL);
  if(FSAL_POSIXDB_IS_ERROR(rc))
    return rc;

  rc = db_exec_sql(p_conn, "DELETE FROM Handle", NULL);
  if(FSAL_POSIXDB_IS_ERROR(rc))
    return rc;

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
