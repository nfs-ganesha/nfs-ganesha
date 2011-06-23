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
  PGresult *p_res;
  char handleid_str[MAX_HANDLEIDSTR_SIZE];
  char handlets_str[MAX_HANDLETSSTR_SIZE];
  char handleidparent_str[MAX_HANDLEIDSTR_SIZE];
  char handletsparent_str[MAX_HANDLETSSTR_SIZE];
  char devid_str[MAX_DEVICEIDSTR_SIZE];
  char inode_str[MAX_INODESTR_SIZE];
  int found;
  const char *paramValues[6];
  fsal_posixdb_status_t st;

  /*******************
   * 1/ sanity check *
   *******************/

  /* parent_directory and filename are NULL only if it is the root directory */
  if(!p_conn || !p_object_info || !p_object_handle
     || (p_filename && !p_parent_directory_handle) || (!p_filename
                                                       && p_parent_directory_handle))
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  CheckConn(p_conn);

  LogFullDebug(COMPONENT_FSAL, "adding entry with parentid=%llu, id=%"PRIu64", name=%s\n",
         p_parent_directory_handle ? p_parent_directory_handle->data.id : 0,
         p_object_info ? p_object_info->inode : 0,
         p_filename ? p_filename->name : "NULL");

  BeginTransaction(p_conn, p_res);

  /*********************************
   * 2/ we check the parent handle *
   *********************************/

  if(p_parent_directory_handle)
    {                           /* the root has no parent */
      snprintf(handleidparent_str, MAX_HANDLEIDSTR_SIZE, "%llu",
               p_parent_directory_handle->data.id);
      snprintf(handletsparent_str, MAX_HANDLETSSTR_SIZE, "%i",
               p_parent_directory_handle->data.ts);
      paramValues[0] = handleidparent_str;
      paramValues[1] = handletsparent_str;
      p_res = PQexecPrepared(p_conn, "lookupHandle", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);

      if(PQntuples(p_res) != 1)
        {
          /* parent entry not found */
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_NOENT, 0);
        }
      PQclear(p_res);
    }
  /**********************************************************
   * 3/ Check if there is an existing Handle for the object *
   **********************************************************/
  snprintf(devid_str, MAX_DEVICEIDSTR_SIZE, "%llu",
           (unsigned long long int)p_object_info->devid);
  snprintf(inode_str, MAX_INODESTR_SIZE, "%llu",
           (unsigned long long int)p_object_info->inode);
  paramValues[0] = devid_str;
  paramValues[1] = inode_str;
  p_res = PQexecPrepared(p_conn, "lookupHandleByInodeFU", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);
  found = (PQntuples(p_res) == 1);

  if(found)
    {                           /* a Handle (that matches devid & inode) already exists */
      /* fill 'info' with information about the handle in the database */
      posixdb_internal_fillFileinfoFromStrValues(&(p_object_handle->data.info), NULL, NULL, PQgetvalue(p_res, 0, 2), /* nlink */
                                                 PQgetvalue(p_res, 0, 3),       /* ctime */
                                                 PQgetvalue(p_res, 0, 4)        /* ftype */
          );
      p_object_handle->data.info.inode = p_object_info->inode;
      p_object_handle->data.info.devid = p_object_info->devid;
      strncpy(handleid_str, PQgetvalue(p_res, 0, 0), MAX_HANDLEIDSTR_SIZE);
      strncpy(handlets_str, PQgetvalue(p_res, 0, 1), MAX_HANDLETSSTR_SIZE);
      PQclear(p_res);

      p_object_handle->data.id = atoll(handleid_str);
      p_object_handle->data.ts = atoi(handlets_str);

      /* check the consistency of the handle */
      if(fsal_posixdb_consistency_check(&(p_object_handle->data.info), p_object_info))
        {
          /* consistency check failed */
          /* p_object_handle has been filled in order to be able to fix the consistency later */
          RollbackTransaction(p_conn, p_res);
          ReturnCodeDB(ERR_FSAL_POSIXDB_CONSISTENCY, 0);
        }

      /* update nlink & ctime if needed */
      if(p_object_info->nlink != p_object_handle->data.info.nlink
         || p_object_info->ctime != p_object_handle->data.info.ctime)
        {
          char nlink_str[MAX_NLINKSTR_SIZE];
          char ctime_str[MAX_CTIMESTR_SIZE];

          snprintf(nlink_str, MAX_NLINKSTR_SIZE, "%i", p_object_info->nlink);
          snprintf(ctime_str, MAX_CTIMESTR_SIZE, "%i", (int)p_object_info->ctime);
          paramValues[0] = handleid_str;
          paramValues[1] = handlets_str;
          paramValues[2] = nlink_str;
          paramValues[3] = ctime_str;

          p_object_handle->data.info = *p_object_info;

          p_res = PQexecPrepared(p_conn, "updateHandle", 4, paramValues, NULL, NULL, 0);
          CheckCommand(p_res);
        }

      fsal_posixdb_UpdateInodeCache(p_object_handle);

    }
  else
    {                           /* no handle found */
      /* Handle does not exist, add a new Handle entry */
      char nlink_str[MAX_NLINKSTR_SIZE];
      char ctime_str[MAX_CTIMESTR_SIZE];
      char ftype_str[MAX_FTYPESTR_SIZE];
      PQclear(p_res);

      p_object_handle->data.ts = (int)time(NULL);
      p_object_handle->data.info = *p_object_info;
      snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_object_handle->data.ts);
      snprintf(nlink_str, MAX_NLINKSTR_SIZE, "%i", p_object_info->nlink);
      snprintf(ctime_str, MAX_CTIMESTR_SIZE, "%i", (int)p_object_info->ctime);
      snprintf(ftype_str, MAX_CTIMESTR_SIZE, "%i", (int)p_object_info->ftype);

      paramValues[0] = devid_str;
      paramValues[1] = inode_str;
      paramValues[2] = handlets_str;
      paramValues[3] = nlink_str;
      paramValues[4] = ctime_str;
      paramValues[5] = ftype_str;

      p_res = PQexecPrepared(p_conn, "insertHandle", 6, paramValues, NULL, NULL, 0);
      CheckCommand(p_res);

      PQclear(p_res);

      p_res =
          PQexecPrepared(p_conn, "lookupHandleByInodeFU", 2, paramValues, NULL, NULL, 0);
      CheckResult(p_res);

      strncpy(handleid_str, PQgetvalue(p_res, 0, 0), MAX_HANDLEIDSTR_SIZE);
      strncpy(handlets_str, PQgetvalue(p_res, 0, 1), MAX_HANDLETSSTR_SIZE);
      p_object_handle->data.id = atoll(PQgetvalue(p_res, 0, 0));
      PQclear(p_res);

      /* now, we have the handle id */
      fsal_posixdb_UpdateInodeCache(p_object_handle);

    }

  /************************************************
   * add (or update) an entry in the Parent table *
   ************************************************/
  paramValues[0] = p_parent_directory_handle ? handleidparent_str : handleid_str;
  paramValues[1] = p_parent_directory_handle ? handletsparent_str : handlets_str;
  paramValues[2] = p_filename ? p_filename->name : "";
  p_res = PQexecPrepared(p_conn, "lookupParent", 3, paramValues, NULL, NULL, 0);
  CheckResult(p_res);
  /* p-res contains handleid & handlets */
  found = (PQntuples(p_res) == 1);
  paramValues[3] = handleid_str;
  paramValues[4] = handlets_str;
  if(found)
    {
      /* update the Parent entry if necessary (there entry exists with another handle) */
      if((fsal_u64_t) atoll(PQgetvalue(p_res, 0, 0)) != p_object_handle->data.id
         || atoi(PQgetvalue(p_res, 0, 1)) != p_object_handle->data.ts)
        {
          /* steps :
             - check the nlink value of the Parent entry to be overwritten
             - if nlink = 1, then we can delete the handle.
             else we have to update it (nlink--) : that is done by fsal_posixdb_deleteParent
             - update the handle of the entry
           */
          char bad_handleid_str[MAX_HANDLEIDSTR_SIZE];
          char bad_handlets_str[MAX_HANDLETSSTR_SIZE];
          int nlink;

          strncpy(bad_handleid_str, PQgetvalue(p_res, 0, 0), MAX_HANDLEIDSTR_SIZE);
          strncpy(bad_handlets_str, PQgetvalue(p_res, 0, 1), MAX_HANDLETSSTR_SIZE);
          PQclear(p_res);       /* clear old res before a new query */

          /* check the nlink value of the entry to be updated */
          paramValues[0] = handleidparent_str;
          paramValues[1] = handletsparent_str;
          p_res = PQexecPrepared(p_conn, "lookupHandleFU", 2, paramValues, NULL, NULL, 0);
          CheckResult(p_res);

          found = (PQntuples(p_res) == 1);

          if(found)
            {                   /* we have retrieved the handle information of the bad entry */
              nlink = atoi(PQgetvalue(p_res, 0, 4));
              PQclear(p_res);   /* clear old res before a new query */

              /* a Parent entry already exists, we delete it */

              st = fsal_posixdb_deleteParent(p_conn, bad_handleid_str, bad_handlets_str,
                                             p_parent_directory_handle ?
                                             handleidparent_str : handleid_str,
                                             p_parent_directory_handle ?
                                             handletsparent_str : handlets_str,
                                             p_filename ? p_filename->name : "", nlink);
              if(FSAL_POSIXDB_IS_ERROR(st))
                {
                  RollbackTransaction(p_conn, p_res);
                  return st;
                }
            }
          else
            {                   /* the Handle line has been deleted */
              PQclear(p_res);   /* clear old res before a new query */
            }

          /* the bad entry has been deleted. Now we had a new Parent entry */
          goto add_new_parent_entry;

        }
      else
        {
          /* a Parent entry exists with our handle, nothing to do */
          PQclear(p_res);
        }
    }
  else
    {
      /* add a Parent entry */
      PQclear(p_res);
 add_new_parent_entry:
      paramValues[0] = p_parent_directory_handle ? handleidparent_str : handleid_str;
      paramValues[1] = p_parent_directory_handle ? handletsparent_str : handlets_str;
      paramValues[2] = p_filename ? p_filename->name : "";
      paramValues[3] = handleid_str;
      paramValues[4] = handlets_str;

      p_res = PQexecPrepared(p_conn, "insertParent", 5, paramValues, NULL, NULL, 0);
      CheckCommand(p_res);
      PQclear(p_res);
      /* XXX : is it possible to have unique key violation ? */
    }

  EndTransaction(p_conn, p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
