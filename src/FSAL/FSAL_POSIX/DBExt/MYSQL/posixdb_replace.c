/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
  result_handle_t res;
  fsal_posixdb_status_t st;
  char query[4096];
  MYSQL_ROW row;
  int re_update = FALSE;

    /*******************
     * 1/ sanity check *
     *******************/

  if(!p_conn || !p_object_info || !p_parent_directory_handle_old || !p_filename_old
     || !p_parent_directory_handle_new || !p_filename_new)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  BeginTransaction(p_conn);

    /**************************************************************************
     * 2/ check that 'p_filename_old' exists in p_parent_directory_handle_old *
     **************************************************************************/

  /* 
     There are three cases :
     * the entry do not exists -> return an error (NOENT)
     * the entry exists.
     * the entry exists but its information are not consistent with p_object_info -> return an error (CONSISTENCY)
   */

  /* check if info is in cache or if this info is inconsistent */
  if(!fsal_posixdb_GetInodeCache(p_parent_directory_handle_old)
     || fsal_posixdb_consistency_check(&(p_parent_directory_handle_old->data.info),
                                       p_object_info))
    {
      snprintf(query, 4096,
               "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, "
               "Handle.nlink, Handle.ctime, Handle.ftype "
               "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid "
               "AND Parent.handlets=Handle.handlets "
               "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s'",
               p_parent_directory_handle_old->data.id, p_parent_directory_handle_old->data.ts,
               p_filename_old->name);

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

      row = mysql_fetch_row(res);
      if(!row)
        {
          /* Error */
          mysql_free_result(res);
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
        }

      /* fill 'infodb' with information about the handle in the database */
      posixdb_internal_fillFileinfoFromStrValues(&(p_parent_directory_handle_old->data.info),
                                                 row[2], row[3], row[4], row[5], row[6]);

      /* check consistency */

      if(fsal_posixdb_consistency_check
         (&(p_parent_directory_handle_old->data.info), p_object_info))
        {
          LogCrit(COMPONENT_FSAL, "Consistency check failed while renaming a file : Handle deleted");
          st = fsal_posixdb_recursiveDelete(p_conn, atoll(row[0]), atoi(row[1]),
                                            FSAL_TYPE_DIR);
          mysql_free_result(res);
          return EndTransaction(p_conn);
        }

      mysql_free_result(res);
    }

    /**********************************************************************************
     * 3/ update the parent entry (in order to change its name and its parent handle) *
     **********************************************************************************/

  /*
     Different cases :
     * a line has been updated -> everything is OK
     * no line has been updated -> the entry does not exists in the database. -> return NOENT
     *        (should never happen because of the previous check)
     * foreign key constraint violation -> new parentdir handle does not exists -> return NOENT
     * unique constraint violation -> there is already a file with this name in the directory -> replace it !
   */

  /* Remove target entry if it exists */

  snprintf(query, 4096,
           "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, "
           "Handle.nlink, Handle.ctime, Handle.ftype "
           "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handlets "
           "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s' FOR UPDATE",
           p_parent_directory_handle_new->data.id, p_parent_directory_handle_new->data.ts,
           p_filename_new->name);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  if(mysql_num_rows(res) > 0)
    {
      row = mysql_fetch_row(res);
      if(!row)
        {
          /* Error */
          mysql_free_result(res);
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
        }

      st = fsal_posixdb_deleteParent(p_conn, atoll(row[0]), atoi(row[1]),
                                     p_parent_directory_handle_new->data.id,
                                     p_parent_directory_handle_new->data.ts,
                                     p_filename_new->name, atoi(row[4]) /* nlink */ );

      if(FSAL_POSIXDB_IS_ERROR(st) && !FSAL_POSIXDB_IS_NOENT(st))
        {
          mysql_free_result(res);
          goto rollback;
        }

    }

  mysql_free_result(res);

  do
    {

      re_update = FALSE;

      /* invalidate name cache */
      fsal_posixdb_InvalidateCache();

      snprintf(query, 4096, "UPDATE Parent "
               "SET handleidparent=%llu, handletsparent=%u, name='%s' "
               "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s' ",
               p_parent_directory_handle_new->data.id,
               p_parent_directory_handle_new->data.ts,
               p_filename_new->name,
               p_parent_directory_handle_old->data.id,
               p_parent_directory_handle_old->data.ts, p_filename_old->name);

      st = db_exec_sql(p_conn, query, NULL);

      if(!FSAL_POSIXDB_IS_ERROR(st))
        {
          /* how many rows updated ? */

          if(mysql_affected_rows(&p_conn->db_conn) == 1)
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
          /* error (switch on MySQL status) */

          switch (st.minor)
            {
            case ER_NO_REFERENCED_ROW:
              /* Foreign key violation : new parentdir does not exist, do nothing */
              st.major = ERR_FSAL_POSIXDB_NOENT;
              st.minor = st.minor;
              break;

            case ER_DUP_UNIQUE:
              /* Unique violation : there is already a file with the same name in parentdir_new */
              /* Delete the existing entry, and then do the update again */

              snprintf(query, 4096,
                       "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, "
                       "Handle.nlink, Handle.ctime, Handle.ftype "
                       "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handlets "
                       "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s' FOR UPDATE",
                       p_parent_directory_handle_new->data.id,
                       p_parent_directory_handle_new->data.ts, p_filename_new->name);

              st = db_exec_sql(p_conn, query, &res);
              if(FSAL_POSIXDB_IS_ERROR(st))
                goto rollback;

              if(mysql_num_rows(res) > 0)
                {
                  row = mysql_fetch_row(res);
                  if(!row)
                    {
                      /* Error */
                      mysql_free_result(res);
                      RollbackTransaction(p_conn);
                      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
                    }

                  st = fsal_posixdb_deleteParent(p_conn, atoll(row[0]), atoi(row[1]), p_parent_directory_handle_new->data.id, p_parent_directory_handle_new->data.ts, p_filename_new->name, atoi(row[4]));        /* nlink */

                  if(FSAL_POSIXDB_IS_ERROR(st) && !FSAL_POSIXDB_IS_NOENT(st))
                    {
                      mysql_free_result(res);
                      break;
                    }
                }

              mysql_free_result(res);

              /* the entry has been deleted, the update can now be done */
              re_update = TRUE;

              break;

            default:           /* keep error code as it is => do nothing */
              ;
            }
        }

    }
  while(re_update);

  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;
  else
    return EndTransaction(p_conn);

 rollback:
  RollbackTransaction(p_conn);
  return st;
}
