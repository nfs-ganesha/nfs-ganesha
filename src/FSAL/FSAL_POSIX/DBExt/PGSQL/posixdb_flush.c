#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"

fsal_posixdb_status_t fsal_posixdb_flush(fsal_posixdb_conn * p_conn)
{
  PGresult *p_res;

  p_res = PQexec(p_conn, "DELETE FROM Parent");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn, "DELETE FROM Handle");
  CheckCommand(p_res);
  PQclear(p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
