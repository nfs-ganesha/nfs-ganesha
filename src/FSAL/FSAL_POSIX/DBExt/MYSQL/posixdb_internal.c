/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=4:tabstop=4:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include "posixdb_internal.h"
#include "posixdb_consistency.h"
#include "RW_Lock.h"

/* cyclic cache of paths */

typedef struct cache_path_entry__
{
  int is_set;
  int path_is_set;
  int info_is_set;
  posixfsal_handle_t handle;
  fsal_path_t path;
  rw_lock_t entry_lock;

} cache_path_entry_t;

#define CACHE_PATH_SIZE 509     /* prime near 512 */

static cache_path_entry_t cache_array[CACHE_PATH_SIZE];

int fsal_posixdb_cache_init()
{
#ifdef _ENABLE_CACHE_PATH
  unsigned int i;

  memset((char *)cache_array, 0, CACHE_PATH_SIZE * sizeof(cache_path_entry_t));

  for(i = 0; i < CACHE_PATH_SIZE; i++)
    {
      if(rw_lock_init(&cache_array[i].entry_lock))
        return -1;

      cache_array[i].is_set = 0;
      cache_array[i].path_is_set = 0;
      cache_array[i].info_is_set = 0;
    }

#endif
  return 0;
}

static unsigned int hash_cache_path(fsal_u64_t id, int ts)
{
  return (1999 * id + 3 * ts + 5) % CACHE_PATH_SIZE;
}

/* @todo pour augmenter les performances d'acces au cache,
 * on hash le handle, et on considere qu'un seul element
 * ayant un hash donne peut occuper une case du tableau.
 */

void fsal_posixdb_CachePath(posixfsal_handle_t * p_handle,      /* IN */
                            fsal_path_t * p_path /* IN */ )
{

#ifdef _ENABLE_CACHE_PATH

  unsigned int i;

  LogDebug(COMPONENT_FSAL, "fsal_posixdb_CachePath: %u, %u = %s", (unsigned int)(p_handle->id),
           (unsigned int)(p_handle->ts), p_path->path);

  i = hash_cache_path(p_handle->id, p_handle->ts);

  /* in the handle already in cache ? */
  P_w(&cache_array[i].entry_lock);

  if(cache_array[i].is_set
     && cache_array[i].handle.id == p_handle->id
     && cache_array[i].handle.ts == p_handle->ts)
    {
      cache_array[i].path_is_set = TRUE;

      /* override it */
      cache_array[i].path = *p_path;

      V_w(&cache_array[i].entry_lock);
      return;
    }

  /* add it (replace previous handle) */
  cache_array[i].is_set = TRUE;
  cache_array[i].path_is_set = TRUE;
  cache_array[i].info_is_set = FALSE;
  cache_array[i].handle = *p_handle;
  cache_array[i].path = *p_path;
  V_w(&cache_array[i].entry_lock);

#endif
  return;
}

/* set/update informations about a handle */
int fsal_posixdb_UpdateInodeCache(posixfsal_handle_t * p_handle)        /* IN */
{
#ifdef _ENABLE_CACHE_PATH

  unsigned int i;

  LogDebug(COMPONENT_FSAL, "UpdateInodeCache: inode_id=%llu", p_handle->info.inode);

  i = hash_cache_path(p_handle->id, p_handle->ts);

  /* in the handle in cache ? */
  P_w(&cache_array[i].entry_lock);

  if(cache_array[i].is_set
     && cache_array[i].handle.id == p_handle->id
     && cache_array[i].handle.ts == p_handle->ts)
    {
      /* update its inode info */
      cache_array[i].handle.info = p_handle->info;
      cache_array[i].info_is_set = TRUE;

      LogDebug(COMPONENT_FSAL, "fsal_posixdb_UpdateInodeCache: %u, %u (existing entry)",
               (unsigned int)(p_handle->id), (unsigned int)(p_handle->ts));

      V_w(&cache_array[i].entry_lock);

      return TRUE;
    }
  LogDebug(COMPONENT_FSAL, "fsal_posixdb_UpdateInodeCache: %u, %u (new entry)",
           (unsigned int)(p_handle->id), (unsigned int)(p_handle->ts));

  /* add it (replace previous handle) */
  cache_array[i].is_set = TRUE;
  cache_array[i].path_is_set = FALSE;
  cache_array[i].info_is_set = TRUE;
  cache_array[i].handle = *p_handle;
  memset(&cache_array[i].path, 0, sizeof(fsal_path_t));

  V_w(&cache_array[i].entry_lock);
#endif
  return FALSE;

}

/* retrieve last informations about a handle */
int fsal_posixdb_GetInodeCache(posixfsal_handle_t * p_handle)   /* IN/OUT */
{
#ifdef _ENABLE_CACHE_PATH
  unsigned int i;

  i = hash_cache_path(p_handle->id, p_handle->ts);

  /* in the handle in cache ? */
  P_r(&cache_array[i].entry_lock);
  if(cache_array[i].is_set
     && cache_array[i].handle.id == p_handle->id
     && cache_array[i].handle.ts == p_handle->ts)
    {
      if(cache_array[i].info_is_set)
        {
          p_handle->info = cache_array[i].handle.info;

          LogDebug(COMPONENT_FSAL, "fsal_posixdb_GetInodeCache(%u, %u)", (unsigned int)(p_handle->id),
                   (unsigned int)(p_handle->ts));
          V_r(&cache_array[i].entry_lock);

          return TRUE;
        }
    }
  V_r(&cache_array[i].entry_lock);
#endif
  return FALSE;

}

void fsal_posixdb_InvalidateCache()
{
#ifdef _ENABLE_CACHE_PATH
  unsigned int i;

  LogDebug(COMPONENT_FSAL, "fsal_posixdb_InvalidateCache");

  for(i = 0; i < CACHE_PATH_SIZE; i++)
    {
      P_w(&cache_array[i].entry_lock);
      cache_array[i].is_set = FALSE;
      cache_array[i].path_is_set = FALSE;
      cache_array[i].info_is_set = FALSE;
      cache_array[i].handle.id = 0;
      cache_array[i].handle.ts = 0;
      V_w(&cache_array[i].entry_lock);
    }

#endif
}

int fsal_posixdb_GetPathCache(posixfsal_handle_t * p_handle,    /* IN */
                              fsal_path_t * p_path /* OUT */ )
{
#ifdef _ENABLE_CACHE_PATH

  unsigned int i;

  i = hash_cache_path(p_handle->id, p_handle->ts);

  /* in the handle in cache ? */
  P_r(&cache_array[i].entry_lock);
  if(cache_array[i].is_set
     && cache_array[i].handle.id == p_handle->id
     && cache_array[i].handle.ts == p_handle->ts)
    {
      if(cache_array[i].path_is_set)
        {
          /* return path it */
          memcpy(p_path, &cache_array[i].path, sizeof(fsal_path_t));
          V_r(&cache_array[i].entry_lock);

          LogDebug(COMPONENT_FSAL, "fsal_posixdb_GetPathCache(%u, %u)=%s",
                   (unsigned int)p_handle->id, (unsigned int)p_handle->ts,
                   p_path->path);
          return TRUE;
        }
    }
  V_r(&cache_array[i].entry_lock);
#endif
  return FALSE;
}

fsal_posixdb_status_t mysql_error_convert(int err)
{
  switch (err)
    {
    case 0:
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    case ER_NO_SUCH_TABLE:
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, err);
    case ER_DUP_ENTRY:
      ReturnCodeDB(ERR_FSAL_POSIXDB_CONSISTENCY, err);
    case ER_BAD_FIELD_ERROR:
    case ER_PARSE_ERROR:
      LogCrit(COMPONENT_FSAL, "SQL request parse error or invalid field");
      ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, err);
    default:
      LogMajor(COMPONENT_FSAL,
                      "Unhandled error %d: default conversion to ERR_FSAL_POSIXDB_CMDFAILED",
                      err);
      ReturnCodeDB(ERR_FSAL_POSIXDB_CMDFAILED, err);
    }
}

int db_is_retryable(int sql_err)
{
  switch (sql_err)
    {
    case ER_SERVER_SHUTDOWN:
    case CR_CONNECTION_ERROR:
    case CR_SERVER_GONE_ERROR:
    case CR_SERVER_LOST:
      return TRUE;
    default:
      return FALSE;
    }
}

fsal_posixdb_status_t db_exec_sql(fsal_posixdb_conn * conn, const char *query,
                                  result_handle_t * p_result)
{
  int rc;
  /* TODO manage retry period */
/*    unsigned int   retry = lmgr_config.connect_retry_min; */
  unsigned int retry = 1;

  LogFullDebug(COMPONENT_FSAL, "SQL query: %s", query);

  do
    {
      rc = mysql_real_query(&conn->db_conn, query, strlen(query));

      if(rc && db_is_retryable(mysql_errno(&conn->db_conn)))
        {
          LogMajor(COMPONENT_FSAL, "Connection to database lost... Retrying in %u sec.",
                   retry);
          sleep(retry);
          retry *= 2;
          /*if ( retry > lmgr_config.connect_retry_max )
             retry = lmgr_config.connect_retry_max; */
        }

    }
  while(rc && db_is_retryable(mysql_errno(&conn->db_conn)));

  if(rc)
    {
      LogMajor(COMPONENT_FSAL, "DB request failed: %s (query: %s)",
               mysql_error(&conn->db_conn), query);
      return mysql_error_convert(mysql_errno(&conn->db_conn));
    }
  else
    {
      if(p_result)
        *p_result = mysql_store_result(&conn->db_conn);

      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    }
}

fsal_posixdb_status_t fsal_posixdb_buildOnePath(fsal_posixdb_conn * p_conn,
                                                posixfsal_handle_t * p_handle,
                                                fsal_path_t * p_path)
{
  unsigned int shift;
  char *new_pos;
  int toomanypaths = 0;

  MYSQL_BIND input[2];          /* input = id, timestamp */

  const int output_num = 3;
  MYSQL_BIND output[output_num];        /* output = path, id, timestamp */
  my_bool is_null[output_num];
  unsigned long length[output_num];
  my_bool error[output_num];

  unsigned long long last_id;
  unsigned int last_ts;
  unsigned long long id;
  unsigned int ts;

  char name[FSAL_MAX_NAME_LEN];
  int root_reached = FALSE;
  MYSQL_STMT *stmt;
  int rc;

  memset(output, 0, sizeof(MYSQL_BIND) * output_num);
  memset(is_null, 0, sizeof(my_bool) * output_num);
  memset(length, 0, sizeof(unsigned long) * output_num);
  memset(error, 0, sizeof(my_bool) * output_num);

  if(!p_conn || !p_handle || !p_path)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  stmt = p_conn->stmt_tab[BUILDONEPATH];

  /* init values */
  memset(p_path, 0, sizeof(fsal_path_t));

  /* Nothing to do, it's the root path */
  if(p_handle->data.id == 0 && p_handle->data.ts == 0)
    ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);

  /* check if the entry is in the cache */
  if(fsal_posixdb_GetPathCache(p_handle, p_path))
    ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);

  last_id = p_handle->data.id;
  last_ts = p_handle->data.ts;

  /* Bind input parameters */

  input[0].buffer_type = MYSQL_TYPE_LONGLONG;
  input[0].buffer = (char *)&last_id;
  input[0].is_null = (my_bool *) 0;
  input[0].is_unsigned = 1;
  input[0].length = NULL;

  input[1].buffer_type = MYSQL_TYPE_LONG;
  input[1].buffer = (char *)&last_ts;
  input[1].is_null = (my_bool *) 0;
  input[1].is_unsigned = 1;
  input[1].length = NULL;

  if(mysql_stmt_bind_param(stmt, input))
    {
      LogCrit(COMPONENT_FSAL, "mysql_stmt_bind_param() failed: %s", mysql_stmt_error(stmt));
      return mysql_error_convert(mysql_stmt_errno(stmt));
    }

  /* Bind output values */

  output[0].buffer_type = MYSQL_TYPE_STRING;
  output[0].buffer = (char *)name;
  output[0].buffer_length = FSAL_MAX_NAME_LEN;
  output[0].is_null = &is_null[0];
  output[0].length = &length[0];
  output[0].error = &error[0];

  output[1].buffer_type = MYSQL_TYPE_LONGLONG;
  output[1].buffer = (char *)&id;
  output[1].is_unsigned = 1;
  output[1].is_null = &is_null[1];
  output[1].length = &length[1];
  output[1].error = &error[1];

  output[2].buffer_type = MYSQL_TYPE_LONG;
  output[2].buffer = (char *)&ts;
  output[2].is_unsigned = 1;
  output[2].is_null = &is_null[2];
  output[2].length = &length[2];
  output[2].error = &error[2];

  if(mysql_stmt_bind_result(stmt, output))
    {
      LogCrit(COMPONENT_FSAL, "mysql_stmt_bind_result() failed: %s", mysql_stmt_error(stmt));
      return mysql_error_convert(mysql_stmt_errno(stmt));
    }

  root_reached = FALSE;

  while(!root_reached)
    {

      rc = mysql_stmt_execute(stmt);
      if(rc)
        return mysql_error_convert(mysql_stmt_errno(stmt));

      if(mysql_stmt_store_result(stmt))
        {
          LogCrit(COMPONENT_FSAL, "mysql_stmt_store_result() failed: %s", mysql_stmt_error(stmt));
          return mysql_error_convert(mysql_stmt_errno(stmt));
        }

      /* retrieve result */
      rc = mysql_stmt_fetch(stmt);
      if(rc == MYSQL_NO_DATA)
        {
          /* clean prepared statement */
          mysql_stmt_free_result(stmt);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);      /* not found */
        }
      else if(rc)
        {
          /* clean prepared statement */
          mysql_stmt_free_result(stmt);
          LogCrit(COMPONENT_FSAL, "mysql_stmt_fetch() failed: %s", mysql_stmt_error(stmt));
          return mysql_error_convert(mysql_stmt_errno(stmt));
        }

      /* @TODO check if several results are returned */

      if((id == last_id) && (ts == last_ts))    /* handle is equal to its parent handle (root reached) */
        {
          root_reached = TRUE;
          break;
        }

      /* prepare next step */
      last_id = id;
      last_ts = ts;

      /* insert the name at the beginning of the path */
      shift = strlen(name);
      if(p_path->len + shift >= FSAL_MAX_PATH_LEN)
        ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);
      new_pos = p_path->path + shift;
      memmove(new_pos, p_path->path, p_path->len);
      memcpy(p_path->path, name, shift);
      p_path->len += shift;
    }

  /* clean prepared statement */
  mysql_stmt_free_result(stmt);

  if(toomanypaths)
    {
      LogCrit(COMPONENT_FSAL, "Returned path: %s", p_path->path);
      ReturnCodeDB(ERR_FSAL_POSIXDB_TOOMANYPATHS, toomanypaths);        /* too many entries */
    }
  else
    {
      /* set result in cache and return */
      fsal_posixdb_CachePath(p_handle, p_path);

      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    }
}

fsal_posixdb_status_t fsal_posixdb_recursiveDelete(fsal_posixdb_conn * p_conn,
                                                   unsigned long long id, unsigned int ts,
                                                   fsal_nodetype_t ftype)
{
  fsal_posixdb_status_t st;
  fsal_nodetype_t ftype_tmp;
  char query[2048];
  result_handle_t res;
  MYSQL_ROW row;

  /* Sanity check */
  if(!p_conn)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  if(ftype == FSAL_TYPE_DIR)
    {
      /* find all the children of the directory in order to delete them, and then we delete the current handle */

      snprintf(query, 2048,
               "SELECT Handle.handleid, Handle.handlets, Handle.ftype, Parent.name, Handle.nlink "
               "FROM Parent INNER JOIN Handle ON Handle.handleid=Parent.handleid "
               "AND Handle.handlets=Parent.handlets "
               "WHERE Parent.handleidparent=%llu AND Parent.handletsparent=%u "
               "AND NOT (Parent.handleidparent = Parent.handleid AND Parent.handletsparent = Parent.handlets) "
               "FOR UPDATE", id, ts);
      st = db_exec_sql(p_conn, query, &res);
      if(FSAL_POSIXDB_IS_ERROR(st))
        return st;

      while((row = mysql_fetch_row(res)) != NULL)
        {
          ftype_tmp = (fsal_nodetype_t) atoi(row[2]);
          if(ftype_tmp == FSAL_TYPE_DIR)
            {
              st = fsal_posixdb_recursiveDelete(p_conn, atoll(row[0]), atoi(row[1]),
                                                ftype_tmp);
            }
          else
            {
              st = fsal_posixdb_deleteParent(p_conn, atoll(row[0]),     /* handleidparent */
                                             atoi(row[1]),      /* handletsparent */
                                             id, ts, row[3],    /* filename */
                                             atoi(row[4]));     /* nlink */
            }
          if(FSAL_POSIXDB_IS_ERROR(st))
            {
              mysql_free_result(res);
              return st;
            }
        }
      mysql_free_result(res);
    }

  /* invalidate name cache */
  fsal_posixdb_InvalidateCache();

  /* Delete the Handle (this also delete entries in Parent, thanks to DELETE CASCADE) */
  snprintf(query, 2048, "DELETE FROM Handle WHERE handleid=%llu AND handlets=%u", id, ts);

  st = db_exec_sql(p_conn, query, NULL);
  if(FSAL_POSIXDB_IS_ERROR(st))
    return st;

#ifdef _NO_DELETE_CASCADE
  /* Delete this handle from parent table (it is supposed not having children now) */

  snprintf(query, 2048, "DELETE FROM Parent WHERE (handleid=%llu AND handlets=%u)", id,
           ts);

  st = db_exec_sql(p_conn, query, NULL);
  if(FSAL_POSIXDB_IS_ERROR(st))
    return st;
#endif

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_deleteParent(fsal_posixdb_conn * p_conn,     /* IN */
                                                unsigned long long id,  /* IN */
                                                unsigned int ts,        /* IN */
                                                unsigned long long idparent,    /* IN */
                                                unsigned int tsparent,  /* IN */
                                                char *filename, /* IN */
                                                int nlink)      /* IN */
{
  char query[1024];
  fsal_posixdb_status_t st;

  /* Sanity check */
  if(!p_conn || !filename || nlink < 1)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  snprintf(query, 1024,
           "DELETE FROM Parent WHERE handleidparent=%llu AND handletsparent=%u AND name='%s'",
           idparent, tsparent, filename);
  st = db_exec_sql(p_conn, query, NULL);
  if(FSAL_POSIXDB_IS_ERROR(st))
    return st;

  /* delete the handle or update it */
  if(nlink == 1)
    {

      /* invalidate cache */
      fsal_posixdb_InvalidateCache();

      /* delete the handle */

      snprintf(query, 1024, "DELETE FROM Handle WHERE handleid=%llu AND handlets=%u", id,
               ts);
      st = db_exec_sql(p_conn, query, NULL);
      if(FSAL_POSIXDB_IS_ERROR(st))
        return st;

#ifdef _NO_DELETE_CASCADE
      /* Delete from parent table */

      snprintf(query, 1024, "DELETE FROM Parent WHERE handleid=%llu AND handlets=%u", id,
               ts);
      st = db_exec_sql(p_conn, query, NULL);
      if(FSAL_POSIXDB_IS_ERROR(st))
        return st;
#endif

    }
  else
    {
      /* invalidate cache */
      fsal_posixdb_InvalidateCache();

      /* update the Handle entry ( Handle.nlink <- (nlink - 1) ) */
      snprintf(query, 1024,
               "UPDATE Handle SET nlink=%u WHERE handleid=%llu AND handlets=%u",
               nlink - 1, id, ts);
      st = db_exec_sql(p_conn, query, NULL);
      if(FSAL_POSIXDB_IS_ERROR(st))
        return st;
    }

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_internal_delete(fsal_posixdb_conn * p_conn,  /* IN */
                                                   unsigned long long idparent, /* IN */
                                                   unsigned int tsparent,       /* IN */
                                                   char *filename,      /* IN */
                                                   fsal_posixdb_fileinfo_t *
                                                   p_object_info /* IN */ )
{
  unsigned long long id;
  unsigned int ts;
  char query[2048];

  fsal_posixdb_status_t st;
  fsal_posixdb_fileinfo_t infodb;
  result_handle_t res;
  MYSQL_ROW row;

  if(!p_conn || !filename)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  snprintf(query, 2048,
           "SELECT Parent.handleid, Parent.handlets, Handle.deviceid, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype "
           "FROM Parent INNER JOIN Handle ON Parent.handleid = Handle.handleid AND Parent.handlets=Handle.handlets "
           "WHERE handleidparent=%llu AND handletsparent=%u AND name='%s' "
           "FOR UPDATE", idparent, tsparent, filename);

  st = db_exec_sql(p_conn, query, &res);
  if(FSAL_POSIXDB_IS_ERROR(st))
    return st;

  /* result contains : handleid, handlets, deviceId, inode, nlink, ctime, ftype  */

  /* no entry found */
  if((mysql_num_rows(res) < 1) || ((row = mysql_fetch_row(res)) == NULL))
    {
      mysql_free_result(res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }

  id = atoll(row[0]);
  ts = atoi(row[1]);

  /* consistency check */
  /* fill 'infodb' with information about the handle in the database */
  /* no need to compare inode & devid, they are the same */
  posixdb_internal_fillFileinfoFromStrValues(&infodb, row[2], row[3], row[4], row[5],
                                             row[6]);
  mysql_free_result(res);

  if(p_object_info && fsal_posixdb_consistency_check(&infodb, p_object_info))
    {
      /* not consistent, the bad handle have to be deleted */
      LogCrit(COMPONENT_FSAL, "Consistency check failed while deleting a Path : Handle deleted");
      infodb.ftype = FSAL_TYPE_DIR;     /* considers that the entry is a directory in order to delete all its Parent entries and its Handle */
    }

  switch (infodb.ftype)
    {
    case FSAL_TYPE_DIR:
      /* directory */
      st = fsal_posixdb_recursiveDelete(p_conn, id, ts, infodb.ftype);
      break;
    default:
      st = fsal_posixdb_deleteParent(p_conn,
                                     id, ts, idparent, tsparent, filename, infodb.nlink);
    }
  return st;
}

fsal_posixdb_status_t posixdb_internal_fillFileinfoFromStrValues(fsal_posixdb_fileinfo_t *
                                                                 p_info, char *devid_str,
                                                                 char *inode_str,
                                                                 char *nlink_str,
                                                                 char *ctime_str,
                                                                 char *ftype_str)
{

  if(!p_info)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  p_info->devid = devid_str ? (dev_t) atoll(devid_str) : 0;
  p_info->inode = inode_str ? (ino_t) atoll(inode_str) : 0;
  p_info->nlink = nlink_str ? atoi(nlink_str) : 0;
  p_info->ctime = ctime_str ? (time_t) atoi(ctime_str) : 0;
  p_info->ftype = ftype_str ? (fsal_nodetype_t) atoi(ftype_str) : 0;

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
