/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
  result_handle_t res;
  fsal_posixdb_status_t st;
  char query[2048];

    /*******************
     * 1/ sanity check *
     *******************/

  if(!p_conn || !p_parent_directory_handle || !p_filename)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  BeginTransaction(p_conn);

    /*******************************
     * 2/ we check the file exists *
     *******************************/

  snprintf(query, 2048, "SELECT Parent.handleid, Parent.handlets, "
           "Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
           "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid "
           "AND Parent.handlets=Handle.handlets "
           "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s' "
           "FOR UPDATE", p_parent_directory_handle->data.id,
           p_parent_directory_handle->data.ts, p_filename->name);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  if(mysql_num_rows(res) != 1)
    {
      /* parent entry not found */
      mysql_free_result(res);
      RollbackTransaction(p_conn);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }
  mysql_free_result(res);

    /***********************************************
     * 3/ Get information about the file to delete *
     ***********************************************/

  st = fsal_posixdb_internal_delete(p_conn, p_parent_directory_handle->data.id,
                                    p_parent_directory_handle->data.ts,
                                    p_filename->name, p_object_info);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  return EndTransaction(p_conn);

 rollback:
  RollbackTransaction(p_conn);
  return st;
}

fsal_posixdb_status_t fsal_posixdb_deleteHandle(fsal_posixdb_conn * p_conn,     /* IN */
                                                posixfsal_handle_t *
                                                p_parent_directory_handle /* IN */ )
{
/*    char           handleid_str[MAX_HANDLEIDSTR_SIZE];
    char           handlets_str[MAX_HANDLETSSTR_SIZE];
    const char    *paramValues[2]; */
  int found;
  result_handle_t res;
  fsal_posixdb_status_t st;
  char query[2048];

  BeginTransaction(p_conn);

  LogFullDebug(COMPONENT_FSAL, "Deleting %llu.%u\n", p_parent_directory_handle->data.id,
         p_parent_directory_handle->data.ts);

  snprintf(query, 2048,
           "SELECT Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
           "FROM Handle WHERE handleid=%llu AND handlets=%u FOR UPDATE",
           p_parent_directory_handle->data.id, p_parent_directory_handle->data.ts);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  found = mysql_num_rows(res);
  mysql_free_result(res);

  if(found)
    {
      /* entry found */
      st = fsal_posixdb_recursiveDelete(p_conn, p_parent_directory_handle->data.id,
                                        p_parent_directory_handle->data.ts, FSAL_TYPE_DIR);
      if(FSAL_POSIXDB_IS_ERROR(st))
        goto rollback;
    }

  return EndTransaction(p_conn);

 rollback:
  RollbackTransaction(p_conn);
  return st;

}
