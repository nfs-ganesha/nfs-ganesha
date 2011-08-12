#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include "posixdb_internal.h"

fsal_posixdb_status_t fsal_posixdb_getInfoFromName(fsal_posixdb_conn * p_conn,  /* IN */
                                                   posixfsal_handle_t * p_parent_directory_handle,      /* IN/OUT */
                                                   fsal_name_t * p_objectname,  /* IN */
                                                   fsal_path_t * p_path,        /* OUT */
                                                   posixfsal_handle_t *
                                                   p_handle /* OUT */ )
{
  PGresult *p_res;
  fsal_posixdb_status_t st;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  const char *paramValues[3] = { handleid_str, handlets_str, p_objectname->name };

  /* sanity check */
  if(!p_conn || !p_handle)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  CheckConn(p_conn);

  LogFullDebug(COMPONENT_FSAL, "object_name='%s'\n", p_objectname->name);

  BeginTransaction(p_conn, p_res);
  /* lookup for the handle of the file */
  if(p_parent_directory_handle && p_parent_directory_handle->data.id)
    {
      snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_parent_directory_handle->data.id);
      snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_parent_directory_handle->data.ts);
      p_res = PQexecPrepared(p_conn, "lookupHandleByName", 3, paramValues, NULL, NULL, 0);
      CheckResult(p_res);
    }
  else
    {
      // get root handle :
      p_res = PQexecPrepared(p_conn, "lookupRootHandle", 0, NULL, NULL, NULL, 0);
      CheckResult(p_res);
    }
  /* p_res contains : Parent.handleid, Parent.handlets, Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype  */

  /* entry not found */
  if(PQntuples(p_res) != 1)
    {
      PQclear(p_res);
      RollbackTransaction(p_conn, p_res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }

  p_handle->data.id = atoll(PQgetvalue(p_res, 0, 0));
  p_handle->data.ts = atoi(PQgetvalue(p_res, 0, 1));
  posixdb_internal_fillFileinfoFromStrValues(&(p_handle->data.info), PQgetvalue(p_res, 0, 2), PQgetvalue(p_res, 0, 3), PQgetvalue(p_res, 0, 4),      /* nlink */
                                             PQgetvalue(p_res, 0, 5),   /* ctime */
                                             PQgetvalue(p_res, 0, 6)    /* ftype */
      );
  PQclear(p_res);

  /* Build the path of the object */
  if(p_path && p_objectname)
    {
      /* build the path of the Parent */
      st = fsal_posixdb_buildOnePath(p_conn, p_parent_directory_handle, p_path);
      if(st.major != ERR_FSAL_POSIXDB_NOERR)
        {
          RollbackTransaction(p_conn, p_res);
          return st;
        }

      /* then concatenate the filename */
      if(!(p_path->len + 1 + p_objectname->len < FSAL_MAX_PATH_LEN))
        {
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);
        }
      p_path->path[p_path->len] = '/';
      strcpy(&p_path->path[p_path->len + 1], p_objectname->name);
      p_path->len += 1 + p_objectname->len;

      /* add the the path to cache */
      fsal_posixdb_CachePath(p_handle, p_path);
    }
  else
    {
      /* update handle if it was in cache */
      fsal_posixdb_UpdateInodeCache(p_handle);
    }

  EndTransaction(p_conn, p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_getInfoFromHandle(fsal_posixdb_conn * p_conn,        /* IN */
                                                     posixfsal_handle_t * p_object_handle,      /* IN/OUT */
                                                     fsal_path_t * p_paths,     /* OUT */
                                                     int paths_size,    /* IN */
                                                     int *p_count /* OUT */ )
{
  PGresult *p_res;
  fsal_posixdb_status_t st;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  posixfsal_handle_t parent_directory_handle;
  int i_path;
  int toomanypaths = 0;
  const char *paramValues[2] = { handleid_str, handlets_str };

  /* sanity check */
  if(!p_conn || !p_object_handle || ((!p_paths || !p_count) && paths_size > 0))
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  CheckConn(p_conn);

  LogFullDebug(COMPONENT_FSAL, "OBJECT_ID=%lli\n", p_object_handle->data.id);

  BeginTransaction(p_conn, p_res);

  /* lookup for the handle of the file */
  snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_object_handle->data.id);
  snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_object_handle->data.ts);

  if(!fsal_posixdb_GetInodeCache(p_object_handle))
    {

      p_res = PQexecPrepared(p_conn, "lookupHandle", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);
      /* p_res contains : Handle.deviceId, Handle.inode, Handle.nlink, Handle.ctime, Handle.ftype  */

      LogDebug(COMPONENT_FSAL, "lookupHandle(%u,%u)", (unsigned int)p_object_handle->data.id,
               (unsigned int)p_object_handle->data.ts);

      /* entry not found */
      if(PQntuples(p_res) != 1)
        {
          LogDebug(COMPONENT_FSAL, "lookupHandle=%d entries", PQntuples(p_res));
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }

      posixdb_internal_fillFileinfoFromStrValues(&(p_object_handle->data.info), PQgetvalue(p_res, 0, 0), PQgetvalue(p_res, 0, 1), PQgetvalue(p_res, 0, 2),   /* nlink */
                                                 PQgetvalue(p_res, 0, 3),       /* ctime */
                                                 PQgetvalue(p_res, 0, 4)        /* ftype */
          );
      PQclear(p_res);

      /* update the inode */
      fsal_posixdb_UpdateInodeCache(p_object_handle);
    }

  /* Build the paths of the object */
  if(p_paths)
    {
      /* find all the paths to the object */
      p_res = PQexecPrepared(p_conn, "lookupPaths", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);
      /* p_res contains name, handleidparent, handletsparent */
      *p_count = PQntuples(p_res);
      if(*p_count == 0)
        {
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOPATH, 0);
        }
      if(*p_count > paths_size)
        {
          toomanypaths = 1;

          LogCrit(COMPONENT_FSAL, "Too many paths found for object %s.%s: found=%u, max=%d",
                  handleid_str, handlets_str, *p_count, paths_size);

          *p_count = paths_size;
        }

      for(i_path = 0; i_path < *p_count; i_path++)
        {
          unsigned int tmp_len;

          /* build the path of the parent directory */
          parent_directory_handle.data.id = atoll(PQgetvalue(p_res, i_path, 1));
          parent_directory_handle.data.ts = atoi(PQgetvalue(p_res, i_path, 2));

          st = fsal_posixdb_buildOnePath(p_conn, &parent_directory_handle,
                                         &p_paths[i_path]);
          if(st.major != ERR_FSAL_POSIXDB_NOERR)
            {
              RollbackTransaction(p_conn, p_res);
              return st;
            }

          tmp_len = p_paths[i_path].len;

          if((tmp_len > 0) && (p_paths[i_path].path[tmp_len - 1] == '/'))
            {
              /* then concatenate the name of the file */
              /* but not concatenate '/' */
              if((tmp_len + strlen(PQgetvalue(p_res, i_path, 0)) >= FSAL_MAX_PATH_LEN))
                {
                  RollbackTransaction(p_conn, p_res);
                  ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);
                }
              strcpy(&p_paths[i_path].path[tmp_len], PQgetvalue(p_res, i_path, 0));
              p_paths[i_path].len += strlen(PQgetvalue(p_res, i_path, 0));

            }
          else
            {
              /* then concatenate the name of the file */
              if((tmp_len + 1 + strlen(PQgetvalue(p_res, i_path, 0)) >=
                  FSAL_MAX_PATH_LEN))
                {
                  RollbackTransaction(p_conn, p_res);
                  ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);
                }
              p_paths[i_path].path[tmp_len] = '/';
              strcpy(&p_paths[i_path].path[tmp_len + 1], PQgetvalue(p_res, i_path, 0));
              p_paths[i_path].len += 1 + strlen(PQgetvalue(p_res, i_path, 0));
            }

          /* insert the object into cache */
          fsal_posixdb_CachePath(p_object_handle, &p_paths[i_path]);

        }
      PQclear(p_res);
    }

  EndTransaction(p_conn, p_res);

  ReturnCodeDB(toomanypaths ? ERR_FSAL_POSIXDB_TOOMANYPATHS : ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_getParentDirHandle(fsal_posixdb_conn * p_conn,       /* IN */
                                                      posixfsal_handle_t * p_object_handle,     /* IN */
                                                      posixfsal_handle_t * p_parent_directory_handle    /* OUT */
    )
{
  PGresult *p_res;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  const char *paramValues[2] = { handleid_str, handlets_str };

  /* sanity check */
  if(!p_conn || !p_parent_directory_handle || !p_object_handle)

    CheckConn(p_conn);

  /* no need to start a transaction, there is anly one query */
  snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_object_handle->data.id);
  snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_object_handle->data.ts);
  p_res = PQexecPrepared(p_conn, "lookupPathsExt", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  /* entry not found */
  if(!PQntuples(p_res))
    {
      PQclear(p_res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }
  LogDebug(COMPONENT_FSAL, "lookupPathsExt");

  p_parent_directory_handle->data.id = atoll(PQgetvalue(p_res, 0, 1));
  p_parent_directory_handle->data.ts = atoi(PQgetvalue(p_res, 0, 2));
  posixdb_internal_fillFileinfoFromStrValues(&(p_parent_directory_handle->data.info), PQgetvalue(p_res, 0, 3), PQgetvalue(p_res, 0, 4), PQgetvalue(p_res, 0, 5),     /* nlink */
                                             PQgetvalue(p_res, 0, 6),   /* ctime */
                                             PQgetvalue(p_res, 0, 7)    /* ftype */
      );

  PQclear(p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
