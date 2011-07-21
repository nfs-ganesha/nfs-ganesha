/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "posixdb_internal.h"
#include "posixdb_consistency.h"
#include <string.h>

fsal_posixdb_status_t fsal_posixdb_add(fsal_posixdb_conn * p_conn,      /* IN */
                                       fsal_posixdb_fileinfo_t * p_object_info, /* IN */
                                       posixfsal_handle_t * p_parent_directory_handle,  /* IN */
                                       fsal_name_t * p_filename,        /* IN */
                                       posixfsal_handle_t * p_object_handle /* OUT */ )
{
  unsigned long long id;
  unsigned int ts;
  int found;
  fsal_posixdb_status_t st;
  result_handle_t res;
  char query[4096];
  MYSQL_ROW row;
  int add_parent_entry = FALSE;

  /*******************
   * 1/ sanity check *
   *******************/

  /* parent_directory and filename are NULL only if it is the root directory */
  if(!p_conn || !p_object_info || !p_object_handle
     || (p_filename && !p_parent_directory_handle)
     || (!p_filename && p_parent_directory_handle))
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  LogFullDebug(COMPONENT_FSAL, "adding entry with parentid=%llu, id=%"PRIu64", name=%s\n",
         p_parent_directory_handle ? p_parent_directory_handle->data.id : 0,
         p_object_info ? p_object_info->inode : 0,
         p_filename ? p_filename->name : "NULL");

  /***************************************
   * 2/ check that parent handle exists
   ***************************************/

  if(p_parent_directory_handle) /* the root has no parent */
    {
      snprintf(query, 4096,
               "SELECT Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
               "FROM Handle WHERE handleid=%llu AND handlets=%u",
               p_parent_directory_handle->data.id, p_parent_directory_handle->data.ts);

      st = db_exec_sql(p_conn, query, &res);
      if(FSAL_POSIXDB_IS_ERROR(st))
        goto rollback;

      if(mysql_num_rows(res) < 1)
        {
          /* parent entry not found */
          mysql_free_result(res);
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }

      mysql_free_result(res);

    }
  /**********************************************************
   * 3/ Check if there is an existing Handle for the object *
   **********************************************************/
  snprintf(query, 4096, "SELECT handleid, handlets, nlink, ctime, ftype "
           "FROM Handle "
           "WHERE deviceid=%llu AND inode=%llu "
           "FOR UPDATE", (unsigned long long)p_object_info->devid, (unsigned long long)p_object_info->inode);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  found = (mysql_num_rows(res) == 1);

  if(found)
    {
      row = mysql_fetch_row(res);
      if(!row)
        {
          /* Error */
          mysql_free_result(res);
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }

      /* a Handle (that matches devid & inode) already exists */
      /* fill 'info' with information about the handle in the database */
      posixdb_internal_fillFileinfoFromStrValues(&(p_object_handle->data.info), NULL, NULL, row[2],  /* nlink */
                                                 row[3],        /* ctime */
                                                 row[4]);       /* ftype */

      p_object_handle->data.info.inode = p_object_info->inode;
      p_object_handle->data.info.devid = p_object_info->devid;

      p_object_handle->data.id = atoll(row[0]);
      p_object_handle->data.ts = atoi(row[1]);
      mysql_free_result(res);

      /* check the consistency of the handle */
      if(fsal_posixdb_consistency_check(&(p_object_handle->data.info), p_object_info))
        {
          /* consistency check failed */
          /* p_object_handle has been filled in order to be able to fix the consistency later */
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_CONSISTENCY, 0);
        }

      /* update nlink & ctime if needed */
      if(p_object_info->nlink != p_object_handle->data.info.nlink
         || p_object_info->ctime != p_object_handle->data.info.ctime)
        {

          snprintf(query, 4096, "UPDATE Handle "
                   "SET ctime=%u, nlink=%u "
                   "WHERE handleid=%llu AND handlets=%u",
                   (unsigned int)p_object_info->ctime,
                   p_object_info->nlink, p_object_handle->data.id, p_object_handle->data.ts);

          p_object_handle->data.info = *p_object_info;

          st = db_exec_sql(p_conn, query, NULL);
          if(FSAL_POSIXDB_IS_ERROR(st))
            goto rollback;
        }

      fsal_posixdb_UpdateInodeCache(p_object_handle);

    }
  else                          /* no handle found */
    {
      mysql_free_result(res);

      /* Handle does not exist, add a new Handle entry */

      p_object_handle->data.ts = (int)time(NULL);
      p_object_handle->data.info = *p_object_info;

      snprintf(query, 4096,
               "INSERT INTO Handle(deviceid, inode, handlets, nlink, ctime, ftype) "
               "VALUES ( %u, %llu, %u, %u, %u, %u)", (unsigned int)p_object_info->devid,
               (unsigned long long)p_object_info->inode,
               (unsigned int)p_object_handle->data.ts, (unsigned int)p_object_info->nlink,
               (unsigned int)p_object_info->ctime, (unsigned int)p_object_info->ftype);

      st = db_exec_sql(p_conn, query, NULL);
      if(FSAL_POSIXDB_IS_ERROR(st))
        goto rollback;

      p_object_handle->data.id = mysql_insert_id(&p_conn->db_conn);

      /* now, we have the handle id */
      fsal_posixdb_UpdateInodeCache(p_object_handle);

    }

  /************************************************
   * add (or update) an entry in the Parent table *
   ************************************************/
  snprintf(query, 4096, "SELECT handleid, handlets "
           "FROM Parent WHERE handleidparent=%llu AND handletsparent=%u AND name='%s'",
           p_parent_directory_handle ? p_parent_directory_handle->data.id : p_object_handle->
           data.id,
           p_parent_directory_handle ? p_parent_directory_handle->data.ts : p_object_handle->
           data.ts, p_filename ? p_filename->name : "");

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    goto rollback;

  /* res contains handleid & handlets */
  found = (mysql_num_rows(res) == 1);

  if(found)
    {
      row = mysql_fetch_row(res);
      if(!row)
        {
          /* Error */
          mysql_free_result(res);
          RollbackTransaction(p_conn);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }

      id = atoll(row[0]);
      ts = atoi(row[1]);
      mysql_free_result(res);

      /* update the Parent entry if necessary (there entry exists with another handle) */
      if((id != p_object_handle->data.id) || (ts != p_object_handle->data.ts))
        {
          /* steps :
             - check the nlink value of the Parent entry to be overwritten
             - if nlink = 1, then we can delete the handle.
             else we have to update it (nlink--) : that is done by fsal_posixdb_deleteParent
             - update the handle of the entry
           */
          int nlink;

          snprintf(query, 4096,
                   "SELECT Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
                   "FROM Handle WHERE handleid=%llu AND handlets=%u FOR UPDATE",
                   p_parent_directory_handle ? p_parent_directory_handle->data.id :
                   p_object_handle->data.id,
                   p_parent_directory_handle ? p_parent_directory_handle->data.ts :
                   p_object_handle->data.ts);

          /* check the nlink value of the entry to be updated */
          st = db_exec_sql(p_conn, query, &res);
          if(FSAL_POSIXDB_IS_ERROR(st))
            goto rollback;

          found = (mysql_num_rows(res) == 1);

          if(found)
            {

              row = mysql_fetch_row(res);
              if(!row)
                {
                  /* Error */
                  mysql_free_result(res);
                  RollbackTransaction(p_conn);
                  ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
                }

              /* we have retrieved the handle information of the bad entry */
              nlink = atoi(row[2]);
              mysql_free_result(res);   /* clear old res before a new query */

              /* a Parent entry already exists, we delete it */

              st = fsal_posixdb_deleteParent(p_conn, id, ts,
                                             p_parent_directory_handle ?
                                             p_parent_directory_handle->data.id
                                             : p_object_handle->data.id,
                                             p_parent_directory_handle ?
                                             p_parent_directory_handle->data.ts
                                             : p_object_handle->data.ts,
                                             p_filename ? p_filename->name : "", nlink);
              if(FSAL_POSIXDB_IS_ERROR(st))
                goto rollback;
            }
          else
            {
              /* the Handle line has been deleted */
              mysql_free_result(res);   /* clear old res before a new query */
            }

          /* the bad entry has been deleted. Now we had a new Parent entry */
          add_parent_entry = TRUE;

        }
      else
        {
          /* a Parent entry exists with our handle, nothing to do */
          mysql_free_result(res);
        }
    }
  else                          /* no parent entry found */
    {
      mysql_free_result(res);
      add_parent_entry = TRUE;
    }

  if(add_parent_entry)
    {
      /* add a Parent entry */

      snprintf(query, 4096,
               "INSERT INTO Parent(handleidparent, handletsparent, name, handleid, handlets) "
               "VALUES(%llu, %u, '%s', %llu, %u)",
               p_parent_directory_handle ? p_parent_directory_handle->data.id :
               p_object_handle->data.id,
               p_parent_directory_handle ? p_parent_directory_handle->data.ts :
               p_object_handle->data.ts, p_filename ? p_filename->name : "",
               p_object_handle->data.id, p_object_handle->data.ts);

      st = db_exec_sql(p_conn, query, NULL);
      if(FSAL_POSIXDB_IS_ERROR(st))
        goto rollback;

      /* XXX : is it possible to have unique key violation ? */
    }

  return EndTransaction(p_conn);

 rollback:
  RollbackTransaction(p_conn);
  return st;
}
