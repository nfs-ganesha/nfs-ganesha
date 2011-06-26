#include "config.h"

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

#undef ANON_UID
#undef ANON_GID

#define OP_MOUNT 0
#define OP_READ 1
#define OP_WRITE 2
#define OP_MDONLY_READ 3
#define OP_MDONLY_WRITE 4

const char *opnames[] = {"MOUNT", "READ", "WRITE", "MDONLY_READ", "MDONLY_WRITE" };

#define ROOT_UID 0
#define ROOT_GID 0
#define USER_UID 1000
#define USER_GID 1000
#define ANON_UID -234
#define ANON_GID -782
#define INVALID_UID -9999
#define INVALID_GID -9999

void init_vars(hash_table_t **ht_ip_stats, struct prealloc_pool *ip_stats_pool)
{
  int rc;

  /* Get the FSAL functions */
  FSAL_LoadFunctions();

  /* Get the FSAL consts */
  FSAL_LoadConsts();

  /* Initialize buddy malloc */
  if((rc = BuddyInit(NULL)) != BUDDY_SUCCESS)
    {
      /* Failed init */
      LogTest("Memory manager could not be initialized");
      exit(1);
    }

  nfs_set_param_default(&nfs_param);

  /*ht_ip_stats*/
  *ht_ip_stats = nfs_Init_ip_stats(nfs_param.ip_stats_param);
  /*  if((*ht_ip_stats = HashTable_Init(nfs_param.ip_stats_param.hash_param)) == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS IP_STATS: Cannot init IP stats cache");
      return NULL;
      }*/

  if(*ht_ip_stats == NULL)
    {
      LogCrit(COMPONENT_INIT, "NFS_INIT: Error while initializing IP/stats cache");
      exit(1);
    }

  /*ip_stats_pool*/
  MakePool(ip_stats_pool,
           100,//           nfs_param.worker_param.nb_ip_stats_prealloc,
           nfs_ip_stats_t, NULL, NULL);
  NamePool(ip_stats_pool, "IP Stats Cache Pool");

  if(!IsPoolPreallocated(ip_stats_pool))
    {
      LogCrit(COMPONENT_INIT, "NFS_INIT: Error while allocating IP stats cache pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      exit(1);
    }
}

struct user_cred test_access(char *addr, char *hostname,
                             hash_table_t *ht_ip_stats,
                             struct prealloc_pool *ip_stats_pool,
                             exportlist_t *pexport, int uid,
			     int gid, int operation)
{
  struct svc_req ptr_req;
  exportlist_client_entry_t pclient_found;
  struct user_cred user_credentials;
  unsigned int nfs_prog;
  unsigned int mnt_prog;
  sockaddr_t ssaddr;
  sockaddr_t *pssaddr = &ssaddr;
  int errcode;
  int export_check_result;
  bool_t proc_makes_write;

  if (operation == OP_WRITE || operation == OP_MDONLY_WRITE)
    proc_makes_write = TRUE;
  else
    proc_makes_write = FALSE;

  user_credentials.caller_uid = INVALID_UID;
  user_credentials.caller_gid = INVALID_GID;

  /*pssaddr*/
  errcode = ipstring_to_sockaddr(addr, pssaddr);
  if (errcode != 0)
    {
      perror ("getaddrinfo");
      return user_credentials;
    }

  /* ptr_req */
  memset(&ptr_req, 0, sizeof(struct svc_req));
  ptr_req.rq_cred.oa_flavor = AUTH_UNIX;
  ptr_req.rq_proc = 23232;
  /*nfs_prog*/
  nfs_prog = nfs_param.core_param.program[P_NFS];

  /*mnt_prog*/
  mnt_prog = nfs_param.core_param.program[P_MNT];
  if (operation == OP_MOUNT)
    ptr_req.rq_prog = mnt_prog;
  else
    ptr_req.rq_prog = nfs_prog;

  /*user_credentials*/
  user_credentials.caller_uid = uid;
  user_credentials.caller_gid = gid;

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

  /* This is the function used to changed uid/gid when they should
   * be anonymous. */
  nfs_check_anon(&pclient_found, pexport, &user_credentials);
  return user_credentials;
}

int main(int argc, char *argv[])
{
  hash_table_t *ht_ip_stats;
  struct prealloc_pool ip_stats_pool;
  exportlist_t pexport;
  int root, read, write, mdonly_read, mdonly_write,
    root_user, uid, operation,
    nonroot, accesstype,
    squashall, gid, return_status=0;
  char *ip = "192.0.2.10";
  //  char *match_str = "192.0.2.10";
  char *match_str = "*";
  char *hostname = "hostname";
  struct user_cred user_credentials;

  SetDefaultLogging("TEST");
  SetNamePgm("test_mnt_proto");

  init_vars(&ht_ip_stats, &ip_stats_pool);

  printf("TESTING THE NEW ACCESS LIST FORMAT\n------------------------------------\n");
  printf("TEST: root read write mdonly_read mdonly_write : uid operation\n");
/* loop through all combinations of new access list uses. */
  for(root=0; root < 2; root++)
    for(read=0; read < 2; read++)
      for(write=0; write < 2; write++)
        for(mdonly_read=0; mdonly_read < 2; mdonly_read++)
          for(mdonly_write=0; mdonly_write < 2; mdonly_write++)
            for(squashall=0; squashall < 2; squashall++)
              {
                memset(&pexport, 0, sizeof(pexport));
                
                /* These strings come from local defines in ../support/exports.c */
                if (root)
                  parseAccessParam("Root_Access", match_str, &pexport, EXPORT_OPTION_ROOT);
                if (read)
                  parseAccessParam("R_Access", match_str, &pexport, EXPORT_OPTION_READ_ACCESS);
                if (write)
                  parseAccessParam("RW_Access", match_str, &pexport,
                                   EXPORT_OPTION_READ_ACCESS | EXPORT_OPTION_WRITE_ACCESS);
                if (mdonly_read)
                  parseAccessParam("MDONLY_RO_Access", match_str, &pexport, EXPORT_OPTION_MD_READ_ACCESS);
                if (mdonly_write)
                  parseAccessParam("MDONLY_Access", match_str, &pexport,
                                   EXPORT_OPTION_MD_WRITE_ACCESS | EXPORT_OPTION_MD_READ_ACCESS);
                
                /* This is critical */
                pexport.new_access_list_version = 1;
                pexport.all_anonymous = squashall;
                pexport.anonymous_uid = ANON_UID;
                pexport.anonymous_gid = ANON_GID;
                
                /* With this export entry test both root and user access. */
                for(root_user=0; root_user < 2; root_user++)
                  for(operation=0; operation < 5; operation++)
                    {
                      if (root_user == 0) {
                        uid = USER_UID;
                        gid = USER_GID;
		      }
                      else {
                        uid = ROOT_UID;
			gid = ROOT_GID;
		      }
                      
                      printf("TEST: %d %d %d %d %d : %d SQ%d -- %s",
                             root, read, write, mdonly_read, mdonly_write, uid, squashall, opnames[operation]);
		      struct user_cred user_credentials;

                      user_credentials = test_access(ip, hostname, ht_ip_stats, &ip_stats_pool,
                                                        &pexport, uid, gid, operation);
                      if (user_credentials.caller_uid == INVALID_UID || user_credentials.caller_gid == INVALID_GID)
			{
			  printf(" ... FAIL - INVALID uid/gid\n");
			  return_status = 1;
			}
                      else if (squashall && (user_credentials.caller_uid != ANON_UID || user_credentials.caller_gid != ANON_GID))
			{
			  printf(" ... FAIL [%d,%d] - uid/gid should be anonymous when squashall is activated.\n",
				 user_credentials.caller_uid, user_credentials.caller_gid);
			  return_status = 1;
			}
                      else if (operation != OP_MOUNT && !squashall && uid == 0 && !root &&
                               (read || write || mdonly_read || mdonly_write) &&
                               (user_credentials.caller_uid != ANON_UID || user_credentials.caller_gid != ANON_GID))
			{
			  printf(" ... FAIL [%d,%d] - Root user should be anonymous when access is obtained through nonroot client entry.\n",
				 user_credentials.caller_uid, user_credentials.caller_gid);
			  return_status = 1;
			}
                      else if (operation != OP_MOUNT && !squashall && uid == 0 && root &&
			       (user_credentials.caller_uid == ANON_UID || user_credentials.caller_gid == ANON_GID))
			{
			  printf(" ... FAIL [%d,%d] - Root user should not be anonymous when access is obtained through root client entry.\n",
				 user_credentials.caller_uid, user_credentials.caller_gid);
			  return_status = 1;
			}
                      else
                        printf(" ... PASS\n");
                    }
              }
  
  /* loop through all combinations of old access list uses. */
  printf("\n\nTESTING THE OLDER ACCESS LIST FORMAT\n------------------------------------\n");
  printf("TEST: root nonroot accesstype : uid operation\n");
  /* loop through all combinations of new access list uses. */
  for(root=0; root < 2; root++)
    for(nonroot=0; nonroot < 2; nonroot++)
      for(accesstype=1; accesstype < 4; accesstype++)
        for(squashall=0; squashall < 2; squashall++)
          {
            memset(&pexport, 0, sizeof(pexport));
            
            /* These strings come from local defines in ../support/exports.c */
            if (root)
              parseAccessParam("Root_Access", match_str, &pexport, EXPORT_OPTION_ROOT);
            if (nonroot)
              parseAccessParam("Access", match_str, &pexport, EXPORT_OPTION_READ_ACCESS|EXPORT_OPTION_WRITE_ACCESS);
            
            if (accesstype == OP_READ)
              pexport.access_type = ACCESSTYPE_RO;
            else if (accesstype == OP_WRITE)
              pexport.access_type = ACCESSTYPE_RW;
            else if (accesstype == OP_MDONLY_READ)
              pexport.access_type = ACCESSTYPE_MDONLY_RO;
            else if (accesstype == OP_MDONLY_WRITE)
              pexport.access_type = ACCESSTYPE_MDONLY;
            else
              printf("FAIL: INVALID access_type \n");
            
            /* This is critical */
            pexport.new_access_list_version = 0;
            pexport.all_anonymous = squashall;
            pexport.anonymous_uid = ANON_UID;
            pexport.anonymous_gid = ANON_GID;
            
            /* With this export entry test both root and user access. */
            for(root_user=0; root_user < 2; root_user++)
              for(operation=0; operation < 5; operation++)
                {
                  if (root_user == 0) {
                    uid = USER_UID;
		    gid = USER_GID;
		  }
                  else {
                    uid = ROOT_UID;
		    gid = ROOT_GID;
		  }
                  
                  printf("TEST: %d %d SQ%d -- ", root, nonroot, squashall);
                  if (accesstype == OP_READ)
                    printf("ACCESSTYPE_RO ");
                  else if (accesstype == OP_WRITE)
                    printf("ACCESSTYPE_RW ");
                  else if (accesstype == OP_MDONLY_READ)
                    printf("ACCESSTYPE_MDONLY_RO ");
                  else if (accesstype == OP_MDONLY_WRITE)
                    printf("ACCESSTYPE_MDONLY ");
                  else
                    printf("INVALID ");
                  printf(": %d ",uid);
                  printf("%s", opnames[operation]);
                  
                  user_credentials = test_access(ip, hostname, ht_ip_stats, &ip_stats_pool,
						 &pexport, uid, gid, operation);

		  /* anonymous uid/gid doesn't apply during a mount */
		  if (operation == OP_MOUNT)
		    printf(" ... PASS - uid/gid doesn't matter during mount\n");
		  /* a write on a ro filesystem would return an error, uid/gid don't matter. */
		  else if ((operation == OP_MDONLY_WRITE || operation == OP_WRITE) && (accesstype == OP_MDONLY_READ || accesstype == OP_READ))
		    printf(" ... PASS - uid/gid doesn't matter when access is denied\n");
		  /* a write on an mdonly-rw filesystem would return an error, uid/gid don't matter. */
		  else if (operation == OP_WRITE && accesstype == OP_MDONLY_WRITE)
		    printf(" ... PASS - uid/gid doesn't matter when access is denied\n");
                  else if (user_credentials.caller_uid == INVALID_UID || user_credentials.caller_gid == INVALID_GID)
		    {
		      printf(" ... FAIL INVALID uid/gid\n");
		      return_status = 1;
		    }
                  else if (squashall && (user_credentials.caller_uid != ANON_UID || user_credentials.caller_gid != ANON_GID))
		    {
		      printf(" ... FAIL [%d,%d] Squash all was active but uid/gid was not anonymous.\n",
			     user_credentials.caller_uid, user_credentials.caller_gid);
		      return_status = 1;
		    }
                  else if (!squashall && uid == 0 && !root && nonroot &&
			   (user_credentials.caller_uid != ANON_UID || user_credentials.caller_gid != ANON_GID))
		    {
		      printf(" ... FAIL [%d,%d] Root user gained access through nonroot client entry, should be anonymous.\n",
			     user_credentials.caller_uid, user_credentials.caller_gid);
		      return_status = 1;
		    }
                  else if (!squashall && uid == 0 && root &&
			   (user_credentials.caller_uid == ANON_UID || user_credentials.caller_gid == ANON_GID))
		    {
		      printf(" ... FAIL [%d,%d] Root user gained permission through root client entry, should not be anonymous.\n",
			     user_credentials.caller_uid, user_credentials.caller_gid);
		      return_status = 1;
		    }
                  else
                    printf(" ... PASS\n");
                }
          }

  printf("----------------------------------------------------\n");
  if (!return_status)
    printf("ALL ANONYMOUS SUPPORT TESTS COMPLETED SUCCESSFULLY!!\n");
  else
    printf("ANONYMOUS SUPPORT TESTS FAILED!!\n");
  printf("----------------------------------------------------\n");
  return return_status;
}
