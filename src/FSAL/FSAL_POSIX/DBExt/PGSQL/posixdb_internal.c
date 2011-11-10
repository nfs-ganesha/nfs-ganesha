#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "posixdb_internal.h"
#include "posixdb_consistency.h"
#include <string.h>
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

fsal_posixdb_status_t fsal_posixdb_buildOnePath(fsal_posixdb_conn * p_conn,
                                                posixfsal_handle_t * p_handle,
                                                fsal_path_t * p_path)
{
  PGresult *p_res;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  unsigned int shift;
  char *new_pos;
  int toomanypaths = 0;
  const char *paramValues[2] = { handleid_str, handlets_str };

  if(!p_conn || !p_handle || !p_path)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  /* init values */
  memset(p_path, 0, sizeof(fsal_path_t));

  /* Nothing to do, it's the root path */
  if(p_handle->data.id == 0 && p_handle->data.ts == 0)
    ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);

  /* check if the entry is in the cache */
  if(fsal_posixdb_GetPathCache(p_handle, p_path))
    ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);

  snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_handle->data.id);
  snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_handle->data.ts);

  /* with PL/PGSQL */
#ifdef _WITH_PLPGSQL
  p_res = PQexecPrepared(p_conn, "buildOnePathPL", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  p_path->len = strlen(PQgetvalue(p_res, 0, 0));

  if(p_path->len >= FSAL_MAX_PATH_LEN)
    ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);

  strcpy(p_path->path, PQgetvalue(p_res, 0, 0));

  /* set result in cache */
  fsal_posixdb_CachePath(p_handle, p_path);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);

#else
  /* without PL/PGSQL */
  while(handleid_str[0] != '\0')
    {
      p_res = PQexecPrepared(p_conn, "buildOnePath", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);

      if(PQntuples(p_res) == 0)
        ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);        /* not found */
      if(PQntuples(p_res) > 1)
        {
          LogCrit(COMPONENT_FSAL, "Too many paths found for object %s.%s: found=%d, expected=1",
                  handleid_str, handlets_str, PQntuples(p_res));

          toomanypaths++;       /* too many entries */
        }

      if(!strncmp(handleid_str, PQgetvalue(p_res, 0, 1), MAX_HANDLEIDSTR_SIZE)
         && !strncmp(handlets_str, PQgetvalue(p_res, 0, 2), MAX_HANDLETSSTR_SIZE))
        break;                  /* handle is equal to its parent handle (root reached) */
      strncpy(handleid_str, PQgetvalue(p_res, 0, 1), MAX_HANDLEIDSTR_SIZE);
      strncpy(handlets_str, PQgetvalue(p_res, 0, 2), MAX_HANDLETSSTR_SIZE);

      /* insertion of the name at the beginning of the path */
      shift = strlen(PQgetvalue(p_res, 0, 0));
      if(p_path->len + shift >= FSAL_MAX_PATH_LEN)
        ReturnCodeDB(ERR_FSAL_POSIXDB_PATHTOOLONG, 0);
      new_pos = p_path->path + shift;
      memmove(new_pos, p_path->path, p_path->len);
      memcpy(p_path->path, PQgetvalue(p_res, 0, 0), shift);
      p_path->len += shift;

      PQclear(p_res);
    }

  if(toomanypaths)
    {
      LogCrit(COMPONENT_FSAL, "Returned path: %s", p_path->path);
      ReturnCodeDB(ERR_FSAL_POSIXDB_TOOMANYPATHS, toomanypaths);        /* too many entries */
    }
  else
    {
      /* set result in cache */
      fsal_posixdb_CachePath(p_handle, p_path);

      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    }
#endif
}

fsal_posixdb_status_t fsal_posixdb_recursiveDelete(fsal_posixdb_conn * p_conn,
                                                   char *handleid_str, char *handlets_str,
                                                   fsal_nodetype_t ftype)
{
  PGresult *p_res;
  fsal_posixdb_status_t st;
  fsal_nodetype_t ftype_tmp;
  unsigned int i;
  unsigned int i_max;
  const char *paramValues[2];

  /* Sanity check */
  if(!p_conn || !handleid_str || !handlets_str)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  if(ftype == FSAL_TYPE_DIR)
    {
      /* We find all the children of the directory in order to delete them, and then we delete the current handle */
      paramValues[0] = handleid_str;
      paramValues[1] = handlets_str;
      p_res = PQexecPrepared(p_conn, "lookupChildrenFU", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);
      i_max = (unsigned int)PQntuples(p_res);
      for(i = 0; i < i_max; i++)
        {
          ftype_tmp = (fsal_nodetype_t) atoi(PQgetvalue(p_res, i, 2));
          if(ftype_tmp == FSAL_TYPE_DIR)
            {
              st = fsal_posixdb_recursiveDelete(p_conn, PQgetvalue(p_res, i, 0),
                                                PQgetvalue(p_res, i, 1), ftype_tmp);
            }
          else
            {
              st = fsal_posixdb_deleteParent(p_conn, PQgetvalue(p_res, i, 0),   /* handleidparent */
                                             PQgetvalue(p_res, i, 1),   /* handletsparent */
                                             handleid_str, handlets_str, PQgetvalue(p_res, i, 3),       /* filename */
                                             atoi(PQgetvalue(p_res, i, 4))      /* nlink */
                  );
            }
          if(FSAL_POSIXDB_IS_ERROR(st))
            {
              PQclear(p_res);
              return st;
            }
        }
      PQclear(p_res);
    }

  /* Delete the Handle */
  /* All Parent entries having this handle will be deleted thanks to foreign keys */
  paramValues[0] = handleid_str;
  paramValues[1] = handlets_str;

  /* invalidate name cache */
  fsal_posixdb_InvalidateCache();

  p_res = PQexecPrepared(p_conn, "deleteHandle", 2, paramValues, NULL, NULL, 0);
  CheckCommand(p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_deleteParent(fsal_posixdb_conn * p_conn,     /* IN */
                                                char *handleid_str,     /* IN */
                                                char *handlets_str,     /* IN */
                                                char *handleidparent_str,       /* IN */
                                                char *handletsparent_str,       /* IN */
                                                char *filename, /* IN */
                                                int nlink)      /* IN */
{
  PGresult *p_res;
  char nlink_str[MAX_NLINKSTR_SIZE];
  const char *paramValues[3];

  /* Sanity check */
  if(!p_conn || !filename || nlink < 1)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  /* delete the Parent entry */
  paramValues[0] = handleidparent_str;
  paramValues[1] = handletsparent_str;
  paramValues[2] = filename;

  /* invalidate name cache */
  fsal_posixdb_InvalidateCache();

  p_res = PQexecPrepared(p_conn, "deleteParent", 3, paramValues, NULL, NULL, 0);
  CheckCommand(p_res);

  /* delete the handle or update it */
  if(nlink == 1)
    {
      /* delete the handle */
      /* If there are other entries in the Parent table with this Handle, they will be deleted (thanks to foreign keys) */
      paramValues[0] = handleid_str;
      paramValues[1] = handlets_str;

      /* invalidate cache */
      fsal_posixdb_InvalidateCache();

      p_res = PQexecPrepared(p_conn, "deleteHandle", 2, paramValues, NULL, NULL, 0);
      CheckCommand(p_res);
    }
  else
    {
      /* update the Handle entry ( Handle.nlink <- (nlink - 1) ) */
      paramValues[0] = handleid_str;
      paramValues[1] = handlets_str;
      snprintf(nlink_str, MAX_NLINKSTR_SIZE, "%i", nlink - 1);
      paramValues[2] = nlink_str;

      /* invalidate cache */
      fsal_posixdb_InvalidateCache();

      p_res = PQexecPrepared(p_conn, "updateHandleNlink", 3, paramValues, NULL, NULL, 0);
      CheckCommand(p_res);
    }

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}

fsal_posixdb_status_t fsal_posixdb_internal_delete(fsal_posixdb_conn * p_conn,  /* IN */
                                                   char *handleidparent_str,    /* IN */
                                                   char *handletsparent_str,    /* IN */
                                                   char *filename,      /* IN */
                                                   fsal_posixdb_fileinfo_t *
                                                   p_object_info /* IN */ )
{

  PGresult *p_res;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  fsal_posixdb_status_t st;
  fsal_posixdb_fileinfo_t infodb;
  const char *paramValues[3];

  if(!p_conn || !handleidparent_str || !handletsparent_str || !filename)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  paramValues[0] = handleidparent_str;
  paramValues[1] = handletsparent_str;
  paramValues[2] = filename;
  p_res = PQexecPrepared(p_conn, "lookupHandleByNameFU", 3, paramValues, NULL, NULL, 0);
  CheckResult(p_res);
  /* p_res contains : handleid, handlets, deviceId, inode, nlink, ctime, ftype  */

  /* no entry found */
  if(PQntuples(p_res) != 1)
    {
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
    }

  strncpy(handleid_str, PQgetvalue(p_res, 0, 0), MAX_HANDLEIDSTR_SIZE);
  strncpy(handlets_str, PQgetvalue(p_res, 0, 1), MAX_HANDLETSSTR_SIZE);

  /* consistency check */
  /* fill 'infodb' with information about the handle in the database */
  posixdb_internal_fillFileinfoFromStrValues(&infodb, PQgetvalue(p_res, 0, 2),  /* no need to compare inode & devid, they are the same */
                                             PQgetvalue(p_res, 0, 3), PQgetvalue(p_res, 0, 4),  /* nlink */
                                             PQgetvalue(p_res, 0, 5),   /* ctime */
                                             PQgetvalue(p_res, 0, 6)    /* ftype */
      );
  PQclear(p_res);

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
      st = fsal_posixdb_recursiveDelete(p_conn, handleid_str, handlets_str, infodb.ftype);
      break;
    default:
      st = fsal_posixdb_deleteParent(p_conn,
                                     handleid_str,
                                     handlets_str,
                                     handleidparent_str,
                                     handletsparent_str, filename, infodb.nlink);
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
