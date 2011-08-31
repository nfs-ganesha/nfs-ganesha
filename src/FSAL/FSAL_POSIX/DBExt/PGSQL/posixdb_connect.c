#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"

fsal_posixdb_status_t fsal_posixdb_initPreparedQueries(fsal_posixdb_conn * p_conn);

fsal_posixdb_status_t fsal_posixdb_connect(fsal_posixdb_conn_params_t * dbparams,
                                           fsal_posixdb_conn ** p_conn)
{
  ConnStatusType st;

  *p_conn = PQsetdbLogin(dbparams->host,        /* Hostname */
                         dbparams->port,        /* Port number */
                         NULL,  /* Options */
                         NULL,  /* tty */
                         dbparams->dbname,      /* Database name */
                         dbparams->login,       /* Login */
                         NULL   /* Password is in a file */
      );
  if((st = PQstatus(*p_conn)) == CONNECTION_OK)
    {
      /*
         prepared statements
       */
      return fsal_posixdb_initPreparedQueries(*p_conn);
    }
  else
    {
      LogEvent(COMPONENT_FSAL, "ERROR: could not connect to database : %s",
               PQerrorMessage(*p_conn));
      PQfinish(*p_conn);
      ReturnCodeDB(ERR_FSAL_POSIXDB_BADCONN, (int)st);
      /* error message available with PQerrorMessage(dbconn) */
    }
}

fsal_posixdb_status_t fsal_posixdb_disconnect(fsal_posixdb_conn * p_conn)
{
  PQfinish(p_conn);
  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_initPreparedQueries(fsal_posixdb_conn * p_conn)
{
  PGresult *p_res;

#ifdef _PGSQL8
  p_res = PQprepare(p_conn,
                    "buildOnePath",
                    "SELECT '/' || name, handleidparent, handletsparent FROM Parent WHERE handleid=$1::bigint AND handlets=$2::int",
                    2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

#ifdef _WITH_PLPGSQL
  p_res = PQprepare(p_conn, "buildOnePathPL", "SELECT buildOnePath($1::bigint, $2::int)",       /* handleid, handlets */
                    2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);
#endif

  p_res = PQprepare(p_conn, "lookupPaths", "SELECT name, handleidparent, handletsparent \
                      FROM Parent \
                      WHERE handleid=$1::bigint AND handleTs=$2::int", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupPathsExt",
                    "SELECT Parent.name, Parent.handleidparent, Parent.handletsparent, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent LEFT JOIN Handle ON Parent.handleidparent = Handle.handleid AND Parent.handletsparent=Handle.handleTs\
                      WHERE Parent.handleid=$1::bigint AND Parent.handleTs=$2::int",
                    2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupHandleByName",
                    "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE handleidparent=$1::bigint AND handletsparent=$2::int AND name=$3::text",
                    3, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupHandleByNameFU",
                    "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE handleidparent=$1::bigint AND handletsparent=$2::int AND name=$3::text \
                      FOR UPDATE", 3, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupRootHandle",
                    "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE Parent.handleidparent=Parent.handleid AND Parent.handletsparent=Parent.handlets",
                    0, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupHandleByInodeFU",
                    "SELECT handleId, handleTs, nlink, ctime, ftype\
                      FROM Handle \
                      WHERE deviceid=$1::bigint AND inode=$2::bigint \
                      FOR UPDATE", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupHandleFU",
                    "SELECT Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Handle \
                      WHERE handleid=$1::bigint AND handleTs=$2::int \
                      FOR UPDATE", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupHandle",
                    "SELECT Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Handle \
                      WHERE handleid=$1::bigint AND handleTs=$2::int \
                      ", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn, "updateHandle", "UPDATE Handle \
                      SET ctime=$4::int, nlink=$3::smallint \
                      WHERE handleid=$1::bigint AND handleTs=$2::int", 4, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn, "updateHandleNlink", "UPDATE Handle \
                      SET nlink=$3::smallint \
                      WHERE handleid=$1::bigint AND handleTs=$2::int", 3, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn, "lookupParent", "SELECT handleid, handlets \
                      FROM Parent \
                      WHERE handleidparent=$1::bigint AND handletsparent=$2::int AND name=$3::text ", 3, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupChildrenFU",
                    "SELECT Handle.handleid, Handle.handlets, Handle.ftype, Parent.name, Handle.nlink \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1::bigint AND Parent.handletsparent=$2::int \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets) \
                      FOR UPDATE", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "lookupChildren",
                    "SELECT Handle.handleid, Handle.handlets, Parent.name, Handle.inode, Handle.deviceid, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1::bigint AND Parent.handletsparent=$2::int \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets)",
                    2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn, "countChildren", "SELECT count(*) \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1::bigint AND Parent.handletsparent=$2::int \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets)", 2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "insertHandle",
                    "INSERT INTO Handle(deviceid, inode, handleTs, nlink, ctime, ftype) \
                      VALUES ($1::int, $2::bigint, $3::bigint, $4::smallint, $5::int, $6::int)", 6, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn, "updateParent", "UPDATE Parent \
                     SET handleidparent=$4::bigint, handletsparent=$5::int, name=$6::text \
                     WHERE handleidparent=$1::bigint AND handletsparent=$2::int AND name=$3::text", 5, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "insertParent",
                    "INSERT INTO Parent(handleidparent, handletsparent, name, handleid, handlets) \
                     VALUES($1::bigint, $2::int, $3::text, $4::bigint, $5::int)",
                    5, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "deleteParent",
                    "DELETE FROM Parent WHERE handleidparent=$1::bigint AND handletsparent=$2::int AND name=$3::text",
                    3, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQprepare(p_conn,
                    "deleteHandle",
                    "DELETE FROM Handle WHERE handleid=$1::bigint AND handlets=$2::int",
                    2, NULL);
  CheckCommand(p_res);
  PQclear(p_res);

#else
  /* For PostgreSQL versions < 8 that do not have PQprepare functions */
  p_res = PQexec(p_conn,
                 "PREPARE \"buildOnePath\"(bigint, int) AS "
                 "SELECT '/' || name, handleidparent, handletsparent FROM Parent WHERE handleid=$1 AND handlets=$2");
  CheckCommand(p_res);
  PQclear(p_res);

#ifdef _WITH_PLPGSQL
  p_res = PQexec(p_conn,
                 "PREPARE \"buildOnePathPL\"(bigint, int) AS "
                 "SELECT buildOnePath($1, $2)" /* handleid, handlets */ );
  CheckCommand(p_res);
  PQclear(p_res);
#endif

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupPaths\"(bigint, int) AS "
                 "SELECT name, handleidparent, handletsparent \
                      FROM Parent \
                      WHERE handleid=$1 AND handleTs=$2");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupPathsExt\"(bigint, int) AS "
                 "SELECT Parent.name, Parent.handleidparent, Parent.handletsparent, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent LEFT JOIN Handle ON Parent.handleidparent = Handle.handleid AND Parent.handletsparent=Handle.handleTs\
                      WHERE Parent.handleid=$1 AND Parent.handleTs=$2");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupHandleByName\"(bigint, int, text) AS "
                 "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE handleidparent=$1 AND handletsparent=$2 AND name=$3");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupHandleByNameFU\"(bigint, int, text) AS "
                 "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE handleidparent=$1 AND handletsparent=$2 AND name=$3 \
                      FOR UPDATE");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupRootHandle\" AS "
                 "SELECT Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handleTs \
                      WHERE Parent.handleidparent=Parent.handleid AND Parent.handletsparent=Parent.handlets");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupHandleByInodeFU\"(bigint, int) AS "
                 "SELECT handleId, handleTs, nlink, ctime, ftype\
                      FROM Handle \
                      WHERE deviceid=$1 AND inode=$2 \
                      FOR UPDATE");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupHandleFU\"(bigint, int) AS "
                 "SELECT Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Handle \
                      WHERE handleid=$1 AND handleTs=$2 \
                      FOR UPDATE");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupHandle\"(bigint, int) AS "
                 "SELECT Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Handle \
                      WHERE handleid=$1 AND handleTs=$2");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"updateHandle\"(bigint, int, smallint, int) AS "
                 "UPDATE Handle \
                      SET ctime=$4, nlink=$3 \
                      WHERE handleid=$1 AND handleTs=$2");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"updateHandleNlink\"(bigint, int, smallint) AS "
                 "UPDATE Handle \
                      SET nlink=$3 \
                      WHERE handleid=$1 AND handleTs=$2");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupParent\"(bigint, int, text) AS "
                 "SELECT handleid, handlets \
                      FROM Parent \
                      WHERE handleidparent=$1 AND handletsparent=$2 AND name=$3");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupChildrenFU\"(bigint, int) AS "
                 "SELECT Handle.handleid, Handle.handlets, Handle.ftype, Parent.name, Handle.nlink \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1 AND Parent.handletsparent=$2 \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets) \
                      FOR UPDATE");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"lookupChildren\"(bigint, int) AS "
                 "SELECT Handle.handleid, Handle.handlets, Parent.name, Handle.inode, Handle.deviceid, Handle.nlink, Handle.ctime, Handle.ftype \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1 AND Parent.handletsparent=$2 \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets)");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn, "PREPARE \"countChildren\"(bigint, int) AS " "SELECT count(*) \
                      FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets\
                      WHERE Parent.handleidparent=$1 AND Parent.handletsparent=$2 \
                        AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets)");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"insertHandle\"(int, bigint, bigint, smallint, int, int) AS "
                 "INSERT INTO Handle(deviceid, inode, handleTs, nlink, ctime, ftype) \
                      VALUES ($1, $2, $3, $4, $5, $6)");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"updateParent\"(bigint, int, text, bigint, int, text) AS "
                 "UPDATE Parent \
                     SET handleidparent=$4, handletsparent=$5, name=$6 \
                     WHERE handleidparent=$1 AND handletsparent=$2 AND name=$3");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"insertParent\"(bigint, int, text, bigint, int) AS "
                 "INSERT INTO Parent(handleidparent, handletsparent, name, handleid, handlets) \
                     VALUES($1, $2, $3, $4, $5)");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"deleteParent\"(bigint, int, text) AS "
                 "DELETE FROM Parent WHERE handleidparent=$1 AND handletsparent=$2 AND name=$3");
  CheckCommand(p_res);
  PQclear(p_res);

  p_res = PQexec(p_conn,
                 "PREPARE \"deleteHandle\"(bigint, int) AS "
                 "DELETE FROM Handle WHERE handleid=$1::bigint AND handlets=$2::int");
  CheckCommand(p_res);
  PQclear(p_res);

#endif

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
