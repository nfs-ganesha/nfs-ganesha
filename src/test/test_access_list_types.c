#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "nfs_exports.h"
#include "config_parsing.h"
#include "log_macros.h"
#include "stuff_alloc.h"
#include "nfs_core.h"
#include "nfs_stat.h"
#include "../MainNFSD/nfs_init.h"

/* These parameters are used throughout Ganesha code and must be initilized. */
nfs_parameter_t nfs_param;
char ganesha_exec_path[MAXPATHLEN] = "/usr/bin/gpfs.ganesha.nfsd";

void init_vars(hash_table_t **ht_ip_stats, struct prealloc_pool **ip_stats_pool)
{
  /*ht_ip_stats*/
  *ht_ip_stats = nfs_Init_ip_stats(nfs_param.ip_stats_param);
  if(*ht_ip_stats == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS_INIT: Error while initializing IP/stats cache");
      exit(1);
    }

  /*ip_stats_pool*/
  MakePool(*ip_stats_pool,
           100,//           nfs_param.worker_param.nb_ip_stats_prealloc,
           nfs_ip_stats_t, NULL, NULL);
  NamePool(*ip_stats_pool, "IP Stats Cache Pool %d", i);

  if(!IsPoolPreallocated(*ip_stats_pool))
    {
      LogCrit(COMPONENT_INIT, "NFS_INIT: Error while allocating IP stats cache pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      exit(1);
    }

  nfs_set_param_default(&nfs_param);

  /* Maybe we should read the actual configuration file as well? */
}

int test_access(char *addr, char *hostname,
                hash_table_t *ht_ip_stats,
                struct prealloc_pool *ip_stats_pool,
                exportlist_t *pexport, int uid, bool_t proc_makes_write)
{
  struct svc_req ptr_req;
  exportlist_client_entry_t pclient_found;
  struct user_cred user_credentials;
  unsigned int nfs_prog;
  unsigned int mnt_prog;
  struct addrinfo *res;
  struct sockaddr_storage *pssaddr;
  int errcode;
  int export_check_result;

  /*pssaddr*/
  errcode = getaddrinfo(addr, NULL, NULL, &res);
  if (errcode != 0)
    {
      perror ("getaddrinfo");
      return -1;
    }
  pssaddr = (struct sockaddr_storage *) res;

  /*nfs_prog*/
  nfs_prog = nfs_param.core_param.nfs_program;

  /*ptr_req*/
  ptr_req.rq_prog = nfs_prog;

  /*mnt_prog*/
  mnt_prog = nfs_param.core_param.mnt_program;

  /*pclient_found*/
  // memset(); ??

  /*user_credentials*/
  user_credentials.caller_uid = uid;

  export_check_result = nfs_export_check_access(pssaddr,
                                                &ptr_req,
                                                pexport,
                                                nfs_prog,
                                                mnt_prog,
                                                ht_ip_stats,
                                                ip_stats_pool,
                                                &pclient_found,
                                                &user_credentials,
                                                proc_makes_write);
  return export_check_result;
}

void expected(int expected_result, int export_check_result) {
  if (export_check_result == expected_result)
      LogTest("PASS");
  else
      LogTest("FAIL");
  
  if (export_check_result == EXPORT_PERMISSION_DENIED)
    {
      LogTest("result = EXPORT_PERMISSION_DENIED");
    } 
  else if (export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_RO)
    {
      LogTest("EXPORT_WRITE_ATTEMPT_WHEN_RO");
    }
  else if (export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO)
    {
      LogTest("EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO");
    }
  else if (export_check_result == EXPORT_PERMISSION_GRANTED)
    {
      LogTest("EXPORT_PERMISSION_GRANTED");
    }
  else if (export_check_result == EXPORT_MDONLY_GRANTED)
    {
      LogTest("EXPORT_MDONLY_GRANTED");
    }
  else
    {
      LogTest("An unexpected result was returned from nfs_export_check_access().");
    }
}

#define ROOT_UID 0
#define USER_UID 1000

int main(int argc, char *argv[])
{
  int export_check_result;
  hash_table_t *ht_ip_stats;
  struct prealloc_pool *ip_stats_pool;
  exportlist_t *pexport = (exportlist_t *) Mem_Alloc(sizeof(exportlist_t));
  int root, read, write, mdonly_read, mdonly_write, root_user, uid, makes_write;
  bool_t proc_makes_write;
  char *ip = "192.0.2.10";
  char *hostname = "hostname";

  init_vars(&ht_ip_stats, &ip_stats_pool);

/* loop through all combinations of new access list uses. */
  for(root=0; root < 2; root++)
    for(read=0; read < 2; read++)
      for(write=0; write < 2; write++)
        for(mdonly_read=0; mdonly_read < 2; mdonly_read++)
          for(mdonly_write=0; mdonly_write < 2; mdonly_write++)
            {
              Mem_Free(pexport);
              pexport = (exportlist_t *) Mem_Alloc(sizeof(exportlist_t));

              if (root)
                  parseAccessParam("Root_Access", ip, pexport, EXPORT_OPTION_ROOT);
              if (read)
                  parseAccessParam("R_Access", ip, pexport, EXPORT_OPTION_READ_ACCESS);
              if (write)
                  parseAccessParam("RW_Access", ip, pexport,
                                   EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
              if (mdonly_read)
                  parseAccessParam("MDONLY_RO_Access", ip, pexport, EXPORT_OPTION_MD_READ_ACCESS);
              if (mdonly_write)
                  parseAccessParam("MDONLY_Access", ip, pexport,
                                   EXPORT_OPTION_MD_WRITE_ACCESS | EXPORT_OPTION_MD_READ_ACCESS);

              /* With this export entry test both root and user access. */
              for(root_user=0; root_user < 2; root_user++)
                for(makes_write=0; makes_write < 2; makes_write++)
                {
                  if (root_user == 0)
                    uid = USER_UID;
                  else
                    uid = ROOT_UID;

                  if (makes_write == 0)
                    proc_makes_write = FALSE;
                  else
                    proc_makes_write = TRUE;

                  export_check_result = test_access(ip, hostname, ht_ip_stats, ip_stats_pool, pexport, 1000, proc_makes_write);
                  expected(EXPORT_PERMISSION_DENIED, export_check_result);
                }
            }

/* loop through all combinations of old access list uses. */


  return 0;
}
