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
  PGresult *p_res;
  char handleid_str[MAX_HANDLEIDSTR_SIZE], handlets_str[MAX_HANDLETSSTR_SIZE];
  const char *paramValues[2] = { NULL, NULL };
  unsigned int i;

  /* sanity check */
  if(!p_conn || !p_parent_directory_handle || !(p_children) || !p_count)
    ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);

  snprintf(handleid_str, MAX_HANDLEIDSTR_SIZE, "%lli", p_parent_directory_handle->data.id);
  snprintf(handlets_str, MAX_HANDLETSSTR_SIZE, "%i", p_parent_directory_handle->data.ts);
  paramValues[0] = handleid_str;
  paramValues[1] = handlets_str;
  p_res = PQexecPrepared(p_conn, "countChildren", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  *p_count = atoi(PQgetvalue(p_res, 0, 0));
  PQclear(p_res);

  if(*p_count == 0)
    {
      *p_children = NULL;
      ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
    }

  if(max_count && (*p_count > max_count))
    {
      *p_children = NULL;
      LogCrit(COMPONENT_FSAL, "Children count %u exceed max_count %u in fsal_posixdb_getChildren",
                 *p_count, max_count);
      ReturnCodeDB(ERR_FSAL_POSIXDB_TOOMANYPATHS, 0);
    }

  p_res = PQexecPrepared(p_conn, "lookupChildren", 2, paramValues, NULL, NULL, 0);
  CheckResult(p_res);

  *p_count = PQntuples(p_res);
  *p_children = (fsal_posixdb_child *) Mem_Alloc(sizeof(fsal_posixdb_child) * (*p_count));
  if(*p_children == NULL)
    {
      PQclear(p_res);
      ReturnCodeDB(ERR_FSAL_POSIXDB_FAULT, 0);
    }

  for(i = 0; i < *p_count; i++)
    {

      char *pq_id = PQgetvalue(p_res, i, 0);
      char *pq_ts = PQgetvalue(p_res, i, 1);

      FSAL_str2name(PQgetvalue(p_res, i, 2), FSAL_MAX_NAME_LEN, &((*p_children)[i].name));

      (*p_children)[i].handle.data.id = atoll(pq_id);
      (*p_children)[i].handle.data.ts = atoi(pq_ts);
      posixdb_internal_fillFileinfoFromStrValues(&((*p_children)[i].handle.data.info),
                                                 PQgetvalue(p_res, i, 4),
                                                 PQgetvalue(p_res, i, 3),
                                                 PQgetvalue(p_res, i, 5),
                                                 PQgetvalue(p_res, i, 6),
                                                 PQgetvalue(p_res, i, 7));
    }
  PQclear(p_res);

  ReturnCodeDB(ERR_FSAL_POSIXDB_NOERR, 0);
}
