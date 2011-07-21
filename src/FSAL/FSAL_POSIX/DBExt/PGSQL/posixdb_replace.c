#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"
#include "posixdb_consistency.h"
#include <string.h>

fsal_posixdb_status_t fsal_posixdb_replace(fsal_posixdb_conn * p_conn,  /* IN */
                                           fsal_posixdb_fileinfo_t * p_object_info,     /* IN */
                                           posixfsal_handle_t * p_parent_directory_handle_old,  /* IN */
                                           fsal_name_t * p_filename_old,        /* IN */
                                           posixfsal_handle_t * p_parent_directory_handle_new,  /* IN */
                                           fsal_name_t * p_filename_new /* IN */ )
{
  PGresult *p_res;
  char handleidparentold_str[MAX_HANDLEIDSTR_SIZE];
  char handletsparentold_str[MAX_HANDLETSSTR_SIZE];
  char handleidparentnew_str[MAX_HANDLEIDSTR_SIZE];
  char handletsparentnew_str[MAX_HANDLETSSTR_SIZE];
  const char *paramValues[6];
  const char *paramValuesL[3];
  fsal_posixdb_status_t st;

  /*******************
   * 1/ sanity check *
   *******************/

  if(!p_conn || !p_object_info || !p_parent_directory_handle_old || !p_filename_old
     || !p_parent_directory_handle_new || !p_filename_new)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  CheckConn(p_conn);

  BeginTransaction(p_conn, p_res);

  /**************************************************************************
   * 2/ check that 'p_filename_old' exists in p_parent_directory_handle_old *
   **************************************************************************/

  /* 
     There are three cases :
     * the entry do not exists -> return an error (NOENT)
     * the entry exists.
     * the entry exists but its information are not consistent with p_object_info -> return an error (CONSISTENCY)
   */
  /* uses paramValues[0] & paramValues[1] from the last request */
  snprintf(handleidparentold_str, MAX_HANDLEIDSTR_SIZE, "%lli",
           p_parent_directory_handle_old->data.id);
  snprintf(handletsparentold_str, MAX_HANDLETSSTR_SIZE, "%i",
           p_parent_directory_handle_old->data.ts);
  paramValues[0] = handleidparentold_str;
  paramValues[1] = handletsparentold_str;
  paramValues[2] = p_filename_old->name;

  /* check if info is in cache or if this info is inconsistent */
  if(!fsal_posixdb_GetInodeCache(p_parent_directory_handle_old)
     || fsal_posixdb_consistency_check(&(p_parent_directory_handle_old->data.info),
                                       p_object_info))
    {

      p_res = PQexecPrepared(p_conn, "lookupHandleByName", 3, paramValues, NULL, NULL, 0);
      CheckResult(p_res);

      if(PQntuples(p_res) != 1)
        {
          /* parent entry not found */
          PQclear(p_res);
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }

      /* fill 'infodb' with information about the handle in the database */
      posixdb_internal_fillFileinfoFromStrValues(&(p_parent_directory_handle_old->data.info), PQgetvalue(p_res, 0, 2),       /* devid */
                                                 PQgetvalue(p_res, 0, 3),       /* inode */
                                                 PQgetvalue(p_res, 0, 4),       /* nlink */
                                                 PQgetvalue(p_res, 0, 5),       /* ctime */
                                                 PQgetvalue(p_res, 0, 6)        /* ftype */
          );
      /* check consistency */

      if(fsal_posixdb_consistency_check
         (&(p_parent_directory_handle_old->data.info), p_object_info))
        {
          LogCrit(COMPONENT_FSAL, "Consistency check failed while renaming a file : Handle deleted");
          st = fsal_posixdb_recursiveDelete(p_conn, PQgetvalue(p_res, 0, 0),
                                            PQgetvalue(p_res, 0, 1), FSAL_TYPE_DIR);
          PQclear(p_res);
          EndTransaction(p_conn, p_res);
          return st;
        }

      PQclear(p_res);
    }

  /**********************************************************************************
   * 3/ update the parent entry (in order to change its name and its parent handle) *
   **********************************************************************************/

  /*
     Different cases :
     * a line has been updated -> everything goes well.
     * no line has been updated -> the entry does not exists in the database. -> return NOENT (should never happen because of the previous check)
     * foreign key constraint violation -> new parentdir handle does not exists -> return NOENT
     * unique constraint violation -> there is already a file with this name in the directory -> replace it !
     PQresultErrorField( p_res, PG_DIAG_SQLSTATE ) -> "23503" pour foreign key violation et "23505" pour unique violation
     (cf http://docs.postgresqlfr.org/pgsql-8.1.3-fr/errcodes-appendix.html )
   */
  /* uses paramValues[0..2] from the last call */
  snprintf(handleidparentnew_str, MAX_HANDLEIDSTR_SIZE, "%lli",
           p_parent_directory_handle_new->data.id);
  snprintf(handletsparentnew_str, MAX_HANDLETSSTR_SIZE, "%i",
           p_parent_directory_handle_new->data.ts);
  paramValues[3] = handleidparentnew_str;
  paramValues[4] = handletsparentnew_str;
  paramValues[5] = p_filename_new->name;

  /* Remove target entry if it exists */
  paramValuesL[0] = handleidparentnew_str;
  paramValuesL[1] = handletsparentnew_str;
  paramValuesL[2] = p_filename_new->name;

  p_res = PQexecPrepared(p_conn, "lookupHandleByNameFU", 3, paramValuesL, NULL, NULL, 0);

  if(PQntuples(p_res) > 0)
    {

      st = fsal_posixdb_deleteParent(p_conn, PQgetvalue(p_res, 0, 0),
                                     PQgetvalue(p_res, 0, 1), handleidparentnew_str,
                                     handletsparentnew_str, p_filename_new->name,
                                     atoi(PQgetvalue(p_res, 0, 4)) /* nlink */ );

      if(FSAL_POSIXDB_IS_ERROR(st) && !FSAL_POSIXDB_IS_NOENT(st))
        {
          PQclear(p_res);
          RollbackTransaction(p_conn, p_res);
          return st;
        }

    }
  PQclear(p_res);

 update:

  /* invalidate name cache */
  fsal_posixdb_InvalidateCache();

  p_res = PQexecPrepared(p_conn, "updateParent", 6, paramValues, NULL, NULL, 0);

  if(PQresultStatus(p_res) == PGRES_COMMAND_OK)
    {
      if((PQcmdTuples(p_res) != NULL) && (atoi(PQcmdTuples(p_res)) == 1))
        {
          /* there was 1 update */
          st.major = ERR_FSAL_POSIXDB_NOERR;
          st.minor = 0;
        }
      else
        {
          /* no row updated */
          st.major = ERR_FSAL_POSIXDB_NOENT;
          st.minor = 0;
        }
    }
  else
    {
      /* error */
      const char *paramValuesL[3];
      char *resultError = PQresultErrorField(p_res, PG_DIAG_SQLSTATE);
      int sqlstate;

      if(resultError)
        sqlstate = atoi(resultError);
      else
        sqlstate = -1;

      PQclear(p_res);

      switch (sqlstate)
        {

        case 23503:
          /* Foreign key violation : new parentdir does not exist, do nothing */
          st.major = ERR_FSAL_POSIXDB_NOENT;
          st.minor = sqlstate;
          break;

        case 23505:
          /* Unique violation : there is already a file with the same name in parentdir_new */
          /* Delete the existing entry, and then do the update again */
          paramValuesL[0] = handleidparentnew_str;
          paramValuesL[1] = handletsparentnew_str;
          paramValuesL[2] = p_filename_new->name;

          p_res =
              PQexecPrepared(p_conn, "lookupHandleByNameFU", 3, paramValuesL, NULL, NULL,
                             0);

          CheckResult(p_res);

          if(PQntuples(p_res) > 0)
            {

              st = fsal_posixdb_deleteParent(p_conn, PQgetvalue(p_res, 0, 0),
                                             PQgetvalue(p_res, 0, 1),
                                             handleidparentnew_str, handletsparentnew_str,
                                             p_filename_new->name,
                                             atoi(PQgetvalue(p_res, 0, 4)) /* nlink */ );

              if(FSAL_POSIXDB_IS_ERROR(st) && !FSAL_POSIXDB_IS_NOENT(st))
                {
                  PQclear(p_res);
                  break;
                }
            }

          PQclear(p_res);

          /* the entry has been deleted, the update can now be done */
          goto update;

          break;

        default:
          st.major = ERR_FSAL_POSIXDB_CMDFAILED;
          st.minor = sqlstate;
        }
    }

  if(FSAL_POSIXDB_IS_ERROR(st))
    RollbackTransaction(p_conn, p_res);
  else
    EndTransaction(p_conn, p_res);

  return st;
}
