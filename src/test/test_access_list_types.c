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

#define TEST_MOUNT 0
#define TEST_READ 1
#define TEST_WRITE 2
#define MDONLY_READ 3
#define MDONLY_WRITE 4

void init_vars(hash_table_t **ht_ip_stats, struct prealloc_pool **ip_stats_pool)
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
  return;
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
  MakePool(*ip_stats_pool,
           100,//           nfs_param.worker_param.nb_ip_stats_prealloc,
           nfs_ip_stats_t, NULL, NULL);
  NamePool(*ip_stats_pool, "IP Stats Cache Pool");

  if(!IsPoolPreallocated(*ip_stats_pool))
    {
      LogCrit(COMPONENT_INIT, "NFS_INIT: Error while allocating IP stats cache pool");
      LogError(COMPONENT_INIT, ERR_SYS, ERR_MALLOC, errno);
      exit(1);
    }
}

int test_access(char *addr, char *hostname,
                hash_table_t *ht_ip_stats,
                struct prealloc_pool *ip_stats_pool,
                exportlist_t *pexport, int uid, int operation)
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

  if (operation == TEST_WRITE || operation == MDONLY_WRITE)
    proc_makes_write = TRUE;
  else
    proc_makes_write = FALSE;

  /*pssaddr*/
  errcode = ipstring_to_sockaddr(addr, pssaddr);
  if (errcode != 0)
    {
      perror ("getaddrinfo");
      return -1;
    }

  /* ptr_req */
  memset(&ptr_req, 0, sizeof(struct svc_req));
  ptr_req.rq_cred.oa_flavor = AUTH_UNIX;
  ptr_req.rq_proc = 23232;
  /*nfs_prog*/
  nfs_prog = nfs_param.core_param.program[P_NFS];

  /*mnt_prog*/
  mnt_prog = nfs_param.core_param.program[P_MNT];
  if (operation == TEST_MOUNT)
    ptr_req.rq_prog = mnt_prog;
  else
    ptr_req.rq_prog = nfs_prog;

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

int expected(int expected_result, int export_check_result) {
  if (export_check_result == expected_result)
    return 0;
  else {
      printf("\tFAIL: ");
  }
  
  if (export_check_result == EXPORT_PERMISSION_DENIED)
    {
      printf("received EXPORT_PERMISSION_DENIED, ");
    } 
  else if (export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_RO)
    {
      printf("received EXPORT_WRITE_ATTEMPT_WHEN_RO, ");
    }
  else if (export_check_result == EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO)
    {
      printf("received EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO, ");
    }
  else if (export_check_result == EXPORT_PERMISSION_GRANTED)
    {
      printf("received EXPORT_PERMISSION_GRANTED, ");
    }
  else if (export_check_result == EXPORT_MDONLY_GRANTED)
    {
      printf("received EXPORT_MDONLY_GRANTED, ");
    }
  else
    {
      printf("Not sure what we received, ");
    }

  if (expected_result == EXPORT_PERMISSION_DENIED)
    {
      printf("expected EXPORT_PERMISSION_DENIED\n");
    } 
  else if (expected_result == EXPORT_WRITE_ATTEMPT_WHEN_RO)
    {
      printf("expected EXPORT_WRITE_ATTEMPT_WHEN_RO\n");
    }
  else if (expected_result == EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO)
    {
      printf("expected EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO\n");
    }
  else if (expected_result == EXPORT_PERMISSION_GRANTED)
    {
      printf("expected EXPORT_PERMISSION_GRANTED\n");
    }
  else if (expected_result == EXPORT_MDONLY_GRANTED)
    {
      printf("expected EXPORT_MDONLY_GRANTED\n");
    }
  else
    {
      printf("Not sure what to expect\n");
    }
  return 1;
}

int predict(char *addr, char *hostname, int root, int read, int write,
	    int md_read, int md_write, int uid, int operation)

{
  /* if uid = 0 then uid will be anonymous and may be declined in the FSAL.
   * However we still give permissions to continue executing request. */

  if (operation == TEST_MOUNT)
    return EXPORT_PERMISSION_GRANTED;

  if (operation == TEST_WRITE || operation == MDONLY_WRITE)
    {
      if (root && uid == 0)
        return EXPORT_PERMISSION_GRANTED;

      if (write)
        return EXPORT_PERMISSION_GRANTED;

      if (md_write)
	return EXPORT_MDONLY_GRANTED;

      return EXPORT_PERMISSION_DENIED;
    }

  if (operation == TEST_READ || operation == MDONLY_READ)
    {
      if (root && uid == 0)
	return EXPORT_PERMISSION_GRANTED;

      if (read || write)
	return EXPORT_PERMISSION_GRANTED;

      if (md_read || md_write)
	return EXPORT_MDONLY_GRANTED;
      
      return EXPORT_PERMISSION_DENIED;
    }  
  
  /* What else could we default to? */
  return -1;
}

int old_predict(char *ip, char *hostname, int root, int nonroot,
		int accesstype, int uid, int operation)
{
  if (operation == TEST_MOUNT)
    return EXPORT_PERMISSION_GRANTED;

  if (operation == TEST_WRITE)
    {
      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  accesstype == TEST_WRITE)
        return EXPORT_PERMISSION_GRANTED;

      if (accesstype == MDONLY_READ)
	return EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO;
      if (accesstype == TEST_READ)
	return EXPORT_WRITE_ATTEMPT_WHEN_RO;

      return EXPORT_PERMISSION_DENIED;
    }

  if (operation == MDONLY_WRITE)
    {
      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  accesstype == TEST_WRITE)
        return EXPORT_PERMISSION_GRANTED;

      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  accesstype == MDONLY_WRITE)
        return EXPORT_MDONLY_GRANTED;

      if (accesstype == MDONLY_READ)
	return EXPORT_WRITE_ATTEMPT_WHEN_MDONLY_RO;
      if (accesstype == TEST_READ)
	return EXPORT_WRITE_ATTEMPT_WHEN_RO;

      return EXPORT_PERMISSION_DENIED;
    }

  if (operation == TEST_READ)
    {
      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  (accesstype == TEST_READ || accesstype == TEST_WRITE))
        return EXPORT_PERMISSION_GRANTED;

      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  (accesstype == MDONLY_READ || accesstype == MDONLY_WRITE))
        return EXPORT_MDONLY_GRANTED;

      return EXPORT_PERMISSION_DENIED;
    }

  if (operation == MDONLY_READ)
    {
      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  (accesstype == TEST_READ || accesstype == TEST_WRITE))
        return EXPORT_PERMISSION_GRANTED;

      if (((nonroot && uid != 0) || (nonroot && uid == 0) || (root && uid == 0)) &&
	  accesstype == MDONLY_READ)
        return EXPORT_MDONLY_GRANTED;

      return EXPORT_PERMISSION_DENIED;      
    }
  
  /* What else could we default to? */
  return -1;
}

#define ROOT_UID 0
#define USER_UID 1000

int main(int argc, char *argv[])
{
  hash_table_t *ht_ip_stats;
  struct prealloc_pool *ip_stats_pool;
  exportlist_t pexport;
  int root, read, write, mdonly_read, mdonly_write,
    root_user, uid, operation, export_check_result,
    predicted_result, nonroot, accesstype,
    return_status=0;
  char *ip = "127.0.0.1";
  char *match_str = "*";
  char *hostname = "localhost";

  /*
  int rc;
  sockaddr_t pipaddr;
  char hostname[MAXHOSTNAMELEN];
  rc = ipstring_to_sockaddr(ip, &pipaddr);
  if (rc != 0)
    {
      printf("FAIL: Could not create sockaddr from ip %s\n", ip);
      perror ("getaddrinfo");
      return 1;
    }
  rc = getnameinfo((struct sockaddr *)&pipaddr, sizeof(pipaddr),
                   hostname, sizeof(hostname),
                   NULL, 0, 0);
  if(rc != 0)
    {
       printf("FAIL: Cannot resolve address %s, error %s", ip, gai_strerror(rc));
       return 1;
    }
  */
  printf("Using IP=%s and Hostname=%s\n", ip, hostname);

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

              /* With this export entry test both root and user access. */
              for(root_user=0; root_user < 2; root_user++)
                for(operation=0; operation < 5; operation++)
		  {
		    if (root_user == 0)
		      uid = USER_UID;
		    else
		      uid = ROOT_UID;
		    
		    printf("TEST: %d %d %d %d %d : %d ",
			   root, read, write, mdonly_read, mdonly_write,uid);
		    if (operation == TEST_MOUNT)
		      printf("TEST_MOUNT\n");
		    else if (operation == TEST_READ)
		      printf("READ\n");
		    else if (operation == TEST_WRITE)
		      printf("WRITE\n");
		    else if (operation == MDONLY_READ)
		      printf("MDONLY_READ\n");
		    else if (operation == MDONLY_WRITE)
		      printf("MDONLY_WRITE\n");

		    export_check_result = test_access(ip, hostname, ht_ip_stats, ip_stats_pool,
						      &pexport, uid, operation);
		    
		    /* predict how the result will turn out */
		    predicted_result = predict(ip, hostname, root, read, write,
					       mdonly_read, mdonly_write, uid, operation);
		    
		    /* report on what was expected vs what we received */
		    if (expected(predicted_result, export_check_result) == 1)
		      return_status = 1;
		  }
            }
  
  /* loop through all combinations of old access list uses. */
  printf("\n\nTESTING THE OLDER ACCESS LIST FORMAT\n------------------------------------\n");
  printf("TEST: root nonroot accesstype : uid operation\n");
/* loop through all combinations of new access list uses. */
  for(root=0; root < 2; root++)
    for(nonroot=0; nonroot < 2; nonroot++)
      for(accesstype=1; accesstype < 4; accesstype++)
      {
	memset(&pexport, 0, sizeof(pexport));

	/* These strings come from local defines in ../support/exports.c */
	if (root)
	  parseAccessParam("Root_Access", match_str, &pexport, EXPORT_OPTION_ROOT);
	if (nonroot)
	  parseAccessParam("Access", match_str, &pexport, EXPORT_OPTION_READ_ACCESS|EXPORT_OPTION_WRITE_ACCESS);

	if (accesstype == TEST_READ)
	  pexport.access_type = ACCESSTYPE_RO;
	else if (accesstype == TEST_WRITE)
	  pexport.access_type = ACCESSTYPE_RW;
	else if (accesstype == MDONLY_READ)
	  pexport.access_type = ACCESSTYPE_MDONLY_RO;
	else if (accesstype == MDONLY_WRITE)
	  pexport.access_type = ACCESSTYPE_MDONLY;
	else
	  printf("FAIL: INVALID access_type \n");

	/* This is critical */
	pexport.new_access_list_version = 0;
	
	/* With this export entry test both root and user access. */
	for(root_user=0; root_user < 2; root_user++)
	  for(operation=0; operation < 5; operation++)
	    {
	      if (root_user == 0)
		uid = USER_UID;
	      else
		uid = ROOT_UID;
	      
	      printf("TEST: %d %d ", root, nonroot);
	      if (accesstype == TEST_READ)
		printf("ACCESSTYPE_RO ");
	      else if (accesstype == TEST_WRITE)
		printf("ACCESSTYPE_RW ");
	      else if (accesstype == MDONLY_READ)
		printf("ACCESSTYPE_MDONLY_RO ");
	      else if (accesstype == MDONLY_WRITE)
		printf("ACCESSTYPE_MDONLY ");
	      else
		printf("INVALID ");
	      printf(": %d ",uid);
	      if (operation == TEST_MOUNT)
		printf("TEST_MOUNT\n");
	      else if (operation == TEST_READ)
		printf("READ\n");
	      else if (operation == TEST_WRITE)
		printf("WRITE\n");
	      else if (operation == MDONLY_READ)
		printf("MDONLY_READ\n");
	      else if (operation == MDONLY_WRITE)
		printf("MDONLY_WRITE\n");
	      
	      export_check_result = test_access(ip, hostname, ht_ip_stats, ip_stats_pool,
						&pexport, uid, operation);
	      
	      /* predict how the result will turn out */
	      if ((operation == TEST_WRITE || operation == MDONLY_WRITE) && accesstype == TEST_READ)
		predicted_result = EXPORT_WRITE_ATTEMPT_WHEN_RO;
	      else
		predicted_result = old_predict(ip, hostname, root, nonroot,
					       accesstype, uid, operation);
	      
	      /* report on what was expected vs what we received */
	      if (expected(predicted_result, export_check_result) == 1)
		return_status = 1;
	    }
      }

  printf("----------------------------------------------------\n");
  if (!return_status)
    printf("ALL ACCESS LIST TYPE TESTS COMPLETED SUCCESSFULLY!!\n");
  else
    printf("ACCESS LIST TYPE TESTS FAILED!!\n");
  printf("----------------------------------------------------\n");

  return return_status;
}
