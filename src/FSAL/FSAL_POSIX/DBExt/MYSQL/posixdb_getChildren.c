/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "fsal.h"
#include "posixdb_internal.h"
#include "stuff_alloc.h"

/**
 * fsal_posixdb_getChildren:
 * retrieve all the children of a directory handle.
 *
 * \param p_conn (input)
 *        Database connection
 * \param p_parent_directory_handle (input):
 *        Handle of the directory where the objects to be retrieved are.
 * \param p_children:
 *        Children of p_parent_directory_handle. It is dynamically allocated inside the function. It have to be freed outside the function !!!
 * \param p_count:
 *        Number of children returned in p_children
 * \return - FSAL_POSIXDB_NOERR, if no error.
 *         - another error code else.
 */
fsal_posixdb_status_t fsal_posixdb_getChildren(fsal_posixdb_conn * p_conn,      /* IN */
                                               posixfsal_handle_t * p_parent_directory_handle,  /* IN */
                                               unsigned int max_count, fsal_posixdb_child ** p_children,        /* OUT */
                                               unsigned int *p_count /* OUT */ )
{
  unsigned int i;
  char query[2048];
  result_handle_t res;
  fsal_posixdb_status_t st;

  /* sanity check */
  if(!p_conn || !p_parent_directory_handle || !(p_children) || !p_count)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  snprintf(query, 2048, "SELECT Handle.handleid, Handle.handlets, Parent.name, "
           "Handle.inode, Handle.deviceid, Handle.nlink, Handle.ctime, Handle.ftype "
           "FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid AND Handle.handlets=Parent.handlets "
           "WHERE Parent.handleidparent=%llu AND Parent.handletsparent=%u "
           "AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets)",
           p_parent_directory_handle->data.id, p_parent_directory_handle->data.ts);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    return st;

  *p_count = mysql_num_rows(res);

  if(*p_count == 0)
    {
      *p_children = NULL;
      mysql_free_result(res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    }

  if(max_count && (*p_count > max_count))
    {
      *p_children = NULL;
      mysql_free_result(res);
      LogCrit(COMPONENT_FSAL, "Children count %u exceed max_count %u in fsal_posixdb_getChildren",
                 *p_count, max_count);
      ReturnCodeDB(ERR_FSAL_POSIXDB_TOOMANYPATHS, 0);
    }

  *p_children = (fsal_posixdb_child *) Mem_Alloc(sizeof(fsal_posixdb_child) * (*p_count));
  if(*p_children == NULL)
    {
      mysql_free_result(res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  for(i = 0; i < *p_count; i++)
    {
      MYSQL_ROW row;

      row = mysql_fetch_row(res);
      if(!row)
        {
          /* Error */
          mysql_free_result(res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
        }

      FSAL_str2name(row[2], FSAL_MAX_NAME_LEN, &((*p_children)[i].name));

      (*p_children)[i].handle.data.id = atoll(row[0]);
      (*p_children)[i].handle.data.ts = atoi(row[1]);
      posixdb_internal_fillFileinfoFromStrValues(&((*p_children)[i].handle.data.info),
                                                 row[4], row[3], row[5], row[6], row[7]);
    }

  mysql_free_result(res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
