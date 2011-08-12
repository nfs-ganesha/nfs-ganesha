#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"
#include <string.h>

fsal_posixdb_status_t fsal_posixdb_delete(fsal_posixdb_conn * p_conn,   /* IN */
                                          posixfsal_handle_t * p_parent_directory_handle,       /* IN */
                                          fsal_name_t * p_filename,     /* IN */
                                          fsal_posixdb_fileinfo_t *
                                          p_object_info /* IN */ )
{
  PGresult *p_res;
  char handleidparent_str[MAX_HANDLEIDSTR_SIZE] = { 0 };
  char handletsparent_str[MAX_HANDLETSSTR_SIZE] = { 0 };
  fsal_posixdb_status_t st;
  const char *paramValues[3];

  /*******************
   * 1/ sanity check *
   *******************/

  if(!p_conn || !p_parent_directory_handle || !p_filename)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  CheckConn(p_conn);

  BeginTransaction(p_conn, p_res);

  /*******************************
   * 2/ we check the file exists *
   *******************************/

  snprintf(handleidparent_str, MAX_HANDLEIDSTR_SIZE, "%lli",
           p_parent_directory_handle->data.id);
  snprintf(handletsparent_str, MAX_HANDLETSSTR_SIZE, "%i", p_parent_directory_handle->data.ts);
  paramValues[0] = handleidparent_str;
  paramValues[1] = handletsparent_str;
  paramValues[2] = p_filename->name;
  p_res = PQexecPrepared(p_conn, "lookupHandleByNameFU", 3, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  if(PQntuples(p_res) != 1)
    {
      /* parent entry not found */
      RollbackTransaction(p_conn, p_res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }
  PQclear(p_res);

  /***********************************************
   * 3/ Get information about the file to delete *
   ***********************************************/

  st = fsal_posixdb_internal_delete(p_conn, handleidparent_str, handletsparent_str,
                                    p_filename->name, p_object_info);
  if(FSAL_POSIXDB_IS_ERROR(st))
    {
      RollbackTransaction(p_conn, p_res);
      return st;
    }

  EndTransaction(p_conn, p_res);

  return st;
}

fsal_posixdb_status_t fsal_posixdb_deleteHandle(fsal_posixdb_conn * p_conn,     /* IN */
                                                posixfsal_handle_t *
                                                p_parent_directory_handle /* IN */ )
{
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  const char *paramValues[2];
  int found;
  PGresult *p_res;
  fsal_posixdb_status_t st;

  CheckConn(p_conn);

  BeginTransaction(p_conn, p_res);

  LogFullDebug(COMPONENT_FSAL, "Deleting %lli.%i\n", p_parent_directory_handle->data.id,
               p_parent_directory_handle->data.ts);

  snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_parent_directory_handle->data.id);
  snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_parent_directory_handle->data.ts);

  paramValues[0] = handleid_str;
  paramValues[1] = handlets_str;
  p_res = PQexecPrepared(p_conn, "lookupHandleFU", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  found = PQntuples(p_res);
  PQclear(p_res);

  if(found)
    {
      /* entry found */
      st = fsal_posixdb_recursiveDelete(p_conn, handleid_str, handlets_str,
                                        FSAL_TYPE_DIR);
      if(FSAL_POSIXDB_IS_ERROR(st))
        {
          RollbackTransaction(p_conn, p_res);
          return st;
        }
    }

  EndTransaction(p_conn, p_res);

  return st;
}
