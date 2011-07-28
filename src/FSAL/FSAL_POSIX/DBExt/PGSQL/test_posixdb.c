#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "fsal_types.h"
#include "posixdb_internal.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
  fsal_posixdb_status_t st;
  fsal_posixdb_conn *p_conn;
  fsal_posixdb_conn_params_t dbparams;
  PGresult *p_res;
  int i;
  int field_handle;
  char path[FSAL_MAX_PATH_LEN];
  posixfsal_handle_t handle;
  posixfsal_handle_t handle2;
  fsal_posixdb_fileinfo_t info;
  fsal_path_t fsalpath;
  fsal_name_t fsalname;

  memset(&dbparams, 0, sizeof(dbparams));
  strcpy(dbparams.host, "localhost");
  strcpy(dbparams.dbname, "test");

  /* connexion */

  st = fsal_posixdb_connect(&dbparams, &p_conn);
  Logtest("%i (%i) : %p", st.major, PQstatus(p_conn), p_conn);

  /* simple query */
  //PQtrace(p_conn, stderr);
  /*
     p_res = PQexec(p_conn, "DELETE FROM Parent");
     Logtest("status : %s", PQresStatus(PQresultStatus(p_res)));
     PQclear(p_res);
     p_res = PQexec(p_conn, "DELETE FROM Handle");
     Logtest("status : %s", PQresStatus(PQresultStatus(p_res)));
     PQclear(p_res);
   */
  // ajout de la racine :
  /*
     puts("ajout de /");
     memset(&info, 0, sizeof(fsal_posixdb_fileinfo_t));
     info.devid = 801;
     info.inode = 2;
     info.nlink = 23;

     memset(&handle2, 0, sizeof(posixfsal_handle_t));
     memset(&fsalname, 0, sizeof(fsal_name_t));

     st = fsal_posixdb_add( p_conn, &info, &handle2, &fsalname, &handle);
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("handle %lli/%i", handle.id, handle.ts);
     }

     // ajout de la /tmp :
     puts("ajout de /tmp");
     memset(&info, 0, sizeof(fsal_posixdb_fileinfo_t));
     info.devid = 805;
     info.inode = 2;
     info.nlink = 21;

     memcpy(&handle2, &handle, sizeof(posixfsal_handle_t));
     memset(&fsalname, 0, sizeof(fsal_name_t));
     strcpy(fsalname.name, "tmp");
     fsalname.len=3;

     st = fsal_posixdb_add( p_conn, &info, &handle2, &fsalname, &handle);
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("handle %lli/%i", handle.id, handle.ts);
     }

     // ajout de /tmp une 2 eme fois
     puts("ajout de /tmp avec un autre inode");
     info.inode++;
     st = fsal_posixdb_add( p_conn, &info, &handle2, &fsalname, &handle);
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("handle %lli/%i", handle.id, handle.ts);
     }

     puts("ajout de /tmp/toto");
     info.devid=456;
     info.inode=8765;
     strcpy(fsalname.name, "toto");
     fsalname.len=4;

     st = fsal_posixdb_add( p_conn, &info, &handle, &fsalname, &handle2);
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("handle %lli/%i", handle2.id, handle2.ts);
     }

     puts("ajout de /tmp/toto/titi");
     info.devid=456;
     info.inode=8734;
     strcpy(fsalname.name, "titi");
     fsalname.len=4;

     st = fsal_posixdb_add( p_conn, &info, &handle2, &fsalname, &handle);
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("handle %lli/%i", handle.id, handle.ts);
     }

     puts("getInfoFromHandle de /tmp/toto");
     st = fsal_posixdb_getInfoFromHandle( p_conn, &handle2, &fsalpath, 1, &i, &info );
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("path: %s, handle %lli/%i", fsalpath.path, handle2.id, handle2.ts);
     }

     memset(&fsalpath, 0,sizeof(fsalpath));
     puts("getInfoFromHandle de /tmp/toto/titi");
     st = fsal_posixdb_getInfoFromHandle( p_conn, &handle, &fsalpath, 1, &i, &info );
     Logtest("status : %i %i", st.major, st.minor);
     if (st.major == ERR_FSAL_POSIXDB_NOERR) {
     Logtest("path: %s, handle %lli/%i", fsalpath.path, handle.id, handle.ts);
     }

     st = fsal_posixdb_delete( p_conn, &handle2, &fsalname, &info);
     Logtest("status : %i %i", st.major, st.minor);
   */

  handle.id = 3226283;
  handle.ts = 1143621188;
  st = fsal_posixdb_buildOnePath(p_conn, &handle, &fsalpath);
  Logtest("%s", fsalpath.path);

  return 0;
}
