/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "posixdb_internal.h"
#include "stuff_alloc.h"

#include <ctype.h>
#include <string.h>

/* forward declaration of function */
fsal_posixdb_status_t fsal_posixdb_initPreparedQueries(fsal_posixdb_conn * p_conn);

/* read a password in a file */
static int ReadPasswordFromFile(char *filename, char *password)
{
  FILE *passfile;
  char errstr[1024];
  int rc;

  passfile = fopen(filename, "r");
  if(!passfile)
    {
      rc = errno;
      strerror_r(rc, errstr, 1024);
      LogCrit(COMPONENT_FSAL, "Error openning password file '%s' : %s", filename, errstr);
      return rc;
    }
  fscanf(passfile, "%1023s", password);
  if(ferror(passfile))
    {
      rc = errno;
      strerror_r(rc, errstr, 1024);
      LogCrit(COMPONENT_FSAL, "Error reading password file '%s' : %s", filename, errstr);
      fclose(passfile);
      return rc;
    }
  fclose(passfile);
  return 0;
}

/** connection to database */
fsal_posixdb_status_t fsal_posixdb_connect(fsal_posixdb_conn_params_t * dbparams,
                                           fsal_posixdb_conn ** p_conn)
{
  my_bool reconnect = 1;
  char password[1024] = "";
  int rc;
  unsigned int port;

  /* read password from password file */
  rc = ReadPasswordFromFile(dbparams->passwdfile, password);
  if(rc)
    ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, rc);

  /* resolve the port number */
  if(dbparams->port[0] != '\0')
    {
      if(!isdigit(dbparams->port[0]))
        {
          LogCrit(COMPONENT_FSAL,
               "Numerical value expected for database port number (invalid value: %s)",
               dbparams->port);
          ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, 0);
        }

      port = atoi(dbparams->port);
    }
  else
    port = 0;

  *p_conn = (fsal_posixdb_conn *) Mem_Alloc(sizeof(fsal_posixdb_conn));
  if(*p_conn == NULL)
    {
      LogCrit(COMPONENT_FSAL, "ERROR: failed to allocate memory");
      ReturnCodeDB(ERR_FSAL_POSIXDB_NO_MEM, errno);
    }

  /* Init client structure */
  if(mysql_init(&(*p_conn)->db_conn) == NULL)
    {
      Mem_Free(*p_conn);
      LogCrit(COMPONENT_FSAL, "ERROR: failed to create MySQL client struct");
      ReturnCodeDB(ERR_FSAL_POSIXDB_BADCONN, errno);
    }
#if ( MYSQL_VERSION_ID >= 50013 )
  /* set auto-reconnect option */
  mysql_options(&(*p_conn)->db_conn, MYSQL_OPT_RECONNECT, &reconnect);
#else
  /* older version */
  (*p_conn)->db_conn->reconnect = 1;
#endif

  /* connect to server */
  if(!mysql_real_connect(&(*p_conn)->db_conn, dbparams->host, dbparams->login,
                         password, dbparams->dbname, port, NULL, 0))
    {
      int rc;
      LogCrit(COMPONENT_FSAL, "Failed to connect to MySQL server: Error: %s",
                 mysql_error(&(*p_conn)->db_conn));
      rc = mysql_errno(&(*p_conn)->db_conn);
      Mem_Free(*p_conn);
      ReturnCodeDB(ERR_FSAL_POSIXDB_BADCONN, rc);
    }

  /* Note [MySQL reference guide]: mysql_real_connect()  incorrectly reset
   * the MYSQL_OPT_RECONNECT  option to its default value before MySQL 5.1.6.
   * Therefore, prior to that version, if you want reconnect to be enabled for
   * each connection, you must call mysql_options() with the MYSQL_OPT_RECONNECT
   * option after each call to mysql_real_connect().
   */
#if (MYSQL_VERSION_ID >= 50013) && (MYSQL_VERSION_ID < 50106)
  /* reset auto-reconnect option */
  mysql_options(&(*p_conn)->db_conn, MYSQL_OPT_RECONNECT, &reconnect);
#endif

  LogEvent(COMPONENT_FSAL, "Logged on to database sucessfully");

  /* Create prepared statements */
  return fsal_posixdb_initPreparedQueries(*p_conn);

}

fsal_posixdb_status_t fsal_posixdb_disconnect(fsal_posixdb_conn * p_conn)
{
  mysql_close(&p_conn->db_conn);
  Mem_Free(p_conn);
  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_initPreparedQueries(fsal_posixdb_conn * p_conn)
{
  int rc;
  unsigned int retry = 1;

  static const char *buildonepath_query =
      "SELECT CONCAT('/',name), handleidparent, handletsparent FROM Parent WHERE handleid=? AND handlets=?";

  /* @TODO  retry = lmgr_config.connect_retry_min; */

  /* create prepared statements */

  do
    {
      /* First create the prepared statement */
      p_conn->stmt_tab[BUILDONEPATH] = mysql_stmt_init(&p_conn->db_conn);

      /* retry if connection to server failed */
      if((p_conn->stmt_tab[BUILDONEPATH] == NULL)
         && db_is_retryable(mysql_errno(&p_conn->db_conn)))
        {
          LogCrit(COMPONENT_FSAL, "Connection to database lost in %s()... Retrying in %u sec.",
                     __FUNCTION__, retry);
          sleep(retry);
          retry *= 2;
          /*if ( retry > lmgr_config.connect_retry_max )
             retry = lmgr_config.connect_retry_max; */
        }
      else
        break;

    }
  while(1);

  if(!p_conn->stmt_tab[BUILDONEPATH])
    ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, mysql_errno(&p_conn->db_conn));

  /* another retry loop */
  /* @TODO retry = lmgr_config.connect_retry_min; */
  retry = 1;

  do
    {
      /* prepare the request */
      rc = mysql_stmt_prepare(p_conn->stmt_tab[BUILDONEPATH], buildonepath_query,
                              strlen(buildonepath_query));

      if(rc && db_is_retryable(mysql_stmt_errno(p_conn->stmt_tab[BUILDONEPATH])))
        {
          LogCrit(COMPONENT_FSAL, "Connection to database lost in %s()... Retrying in %u sec.",
                     __FUNCTION__, retry);
          sleep(retry);
          retry *= 2;
          /*if ( retry > lmgr_config.connect_retry_max )
             retry = lmgr_config.connect_retry_max; */

        }
      else
        break;

    }
  while(1);

  if(rc)
    {
      LogCrit(COMPONENT_FSAL, "Failed to create prepared statement: Error: %s (query='%s')",
                 mysql_stmt_error(p_conn->stmt_tab[BUILDONEPATH]), buildonepath_query);
      mysql_stmt_close(p_conn->stmt_tab[BUILDONEPATH]);
      ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, rc);
    }

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
