/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * ---------------------------------------
 */

/**
 * \file    commands_NFS.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 07:42:53 $
 * \version $Revision: 1.34 $
 * \brief   calls to NFSv2, NFSv3, MNTv1, MNTv3 commands.
 *
 *
 * $Log: commands_NFS.c,v $
 * Revision 1.34  2006/02/23 07:42:53  leibovic
 * Adding -n option to shell.
 *
 * Revision 1.33  2006/02/22 09:07:45  leibovic
 * Correcting bug for returned value.
 *
 * Revision 1.32  2006/02/17 13:37:44  leibovic
 * Ghost FS is back !
 *
 * Revision 1.31  2006/01/27 10:28:37  deniel
 * Now support rpm
 *
 * Revision 1.30  2006/01/26 07:54:44  leibovic
 * Compil AIX_5 et Linux OK.
 *
 * Revision 1.29  2006/01/25 09:39:44  deniel
 * pb include DCE
 *
 * Revision 1.28  2006/01/24 13:49:33  leibovic
 * Adding missing includes.
 *
 * Revision 1.27  2006/01/24 11:43:05  deniel
 * Code cleaning in progress
 *
 * Revision 1.26  2006/01/20 14:44:14  leibovic
 * altgroups support.
 *
 * Revision 1.25  2006/01/18 08:02:04  deniel
 * Order in includes and libraries
 *
 * Revision 1.24  2006/01/17 16:40:54  leibovic
 * New interface for readexport.
 *
 * Revision 1.22  2005/11/21 09:55:18  leibovic
 * Once for all thread's credential initialization.
 *
 * Revision 1.21  2005/11/08 15:22:24  deniel
 * WildCard and Netgroup entry for exportlist are now supported
 *
 * Revision 1.20  2005/11/04 15:12:58  deniel
 * Added basic authentication support
 *
 * Revision 1.19  2005/10/12 11:30:10  leibovic
 * NFSv2.
 *
 * Revision 1.18  2005/10/10 12:39:08  leibovic
 * Using mnt/nfs free functions.
 *
 * Revision 1.17  2005/09/28 11:02:29  leibovic
 * Adding su command to NFS layer.
 *
 * Revision 1.16  2005/09/27 08:15:13  leibovic
 * Adding traces and changhing readexport prototype.
 *
 * Revision 1.15  2005/09/07 14:08:32  leibovic
 * Adding stat command for NFS.
 *
 * Revision 1.14  2005/08/12 07:07:23  leibovic
 * Adding ln command for nfs.
 *
 * Revision 1.13  2005/08/10 14:55:05  leibovic
 * NFS support of setattr, rename, link, symlink.
 *
 * Revision 1.12  2005/08/10 10:57:18  leibovic
 * Adding removal functions.
 *
 * Revision 1.11  2005/08/09 14:52:58  leibovic
 * Addinf create and mkdir commands.
 *
 * Revision 1.10  2005/08/08 11:42:50  leibovic
 * Adding some stardard unix calls through NFS (ls, cd, pwd).
 *
 * Revision 1.9  2005/08/05 15:17:57  leibovic
 * Adding mount and pwd commands for browsing.
 *
 * Revision 1.8  2005/08/05 07:59:07  leibovic
 * some nfs3 commands added.
 *
 * Revision 1.7  2005/08/04 06:57:41  leibovic
 * some NFSv2 commands are completed.
 *
 * Revision 1.6  2005/08/03 12:51:16  leibovic
 * MNT3 protocol OK.
 *
 * Revision 1.5  2005/08/03 11:51:10  leibovic
 * MNT1 protocol OK.
 *
 * Revision 1.4  2005/08/03 08:16:23  leibovic
 * Adding nfs layer structures.
 *
 * Revision 1.3  2005/05/09 12:23:55  leibovic
 * Version 2 of ganeshell.
 *
 * Revision 1.2  2005/03/04 10:12:15  leibovic
 * New debug functions.
 *
 * Revision 1.1  2005/01/21 09:40:31  leibovic
 * Integrating NFS and MNT protocol commands.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include "rpc.h"
#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "commands.h"
#include "stuff_alloc.h"
#include "Getopt.h"
#include "cmd_nfstools.h"
#include "cmd_tools.h"
#include "nfs_file_handle.h"
#include "nfs_core.h"
#include <ctype.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include "cmd_tools.h"

writeverf3 NFS3_write_verifier;

/* Function used for debugging */
#ifdef _DEBUG_NFS_SHELL
void print_nfs_res(nfs_res_t * p_res)
{
  int index;
  for(index = 0; index < sizeof(nfs_res_t); index++)
    {
      if((index + 1) % 32 == 0)
        printf("%02X\n", ((char *)p_res)[index]);
      else
        printf("%02X.", ((char *)p_res)[index]);
    }
  printf("\n");
}
#endif

/* --------------- INTERNAL FH3 REPRESENTATION ---------------- */
/* used for keeping handle value after
 * freeing nfs res.
 */
typedef struct shell_fh3__
{
  u_int data_len;
  char data_val[NFS3_FHSIZE];
} shell_fh3_t;

static void set_shell_fh3(shell_fh3_t * p_int_fh3, nfs_fh3 * p_nfshdl)
{
  p_int_fh3->data_len = p_nfshdl->data.data_len;
  memcpy(p_int_fh3->data_val, p_nfshdl->data.data_val, p_nfshdl->data.data_len);
}

static void set_nfs_fh3(nfs_fh3 * p_nfshdl, shell_fh3_t * p_int_fh3)
{
  p_nfshdl->data.data_len = p_int_fh3->data_len;
  p_nfshdl->data.data_val = p_int_fh3->data_val;
}

/* ------------------------- END ------------------------------ */

/* The cache hash table (defined in "commands_Cache_inode.c") */
extern hash_table_t *ht;

extern cache_inode_client_parameter_t cache_client_param;
extern cache_content_client_parameter_t datacache_client_param;

/* NFS layer initialization status*/
static int is_nfs_layer_initialized = FALSE;

/* Global variable: export list */
exportlist_t exportlist[128];
exportlist_t *pexportlist = exportlist;

/* Global variable: local host name */
static char localmachine[256];

/* thread specific variables */

typedef struct cmdnfs_thr_info__
{

  int is_thread_init;

  /* export context : on for each thread,
   * on order to make it possible for them
   * to access different filesets.
   */
  fsal_export_context_t exp_context;

  /** context for accessing the filesystem */
  fsal_op_context_t context;

  /* AuthUnix_params for this thread */
  struct authunix_parms authunix_struct;

  /** Thread specific variable : the client for the cache */
  cache_inode_client_t client;
  cache_content_client_t dc_client;

  /* info for advanced commands (pwd, ls, cd, ...) */
  int is_mounted_path;

  shell_fh3_t mounted_path_hdl;
  char mounted_path[NFS2_MAXPATHLEN];

  shell_fh3_t current_path_hdl;
  char current_path[NFS2_MAXPATHLEN];

} cmdnfs_thr_info_t;

/* pthread key to manage thread specific configuration */

static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init pthtread_key for current thread */

static void init_keys(void)
{
  time_t ServerBootTime = time(NULL);

  if(pthread_key_create(&thread_key, NULL) == -1)
    printf("Error %d creating pthread key for thread %p : %s\n",
           errno, (caddr_t) pthread_self(), strerror(errno));

  memset(NFS3_write_verifier, 0, sizeof(writeverf3));
  memcpy(NFS3_write_verifier, &ServerBootTime, sizeof(time_t));

  return;
}                               /* init_keys */

cmdnfs_thr_info_t *GetNFSClient()
{
  cmdnfs_thr_info_t *p_thr_info;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      printf("Error %d calling pthread_once for thread %p : %s\n",
             errno, (caddr_t) pthread_self(), strerror(errno));
      return NULL;
    }

  p_thr_info = (cmdnfs_thr_info_t *) pthread_getspecific(thread_key);

  /* we allocate the thread context if this is the first time */
  if(p_thr_info == NULL)
    {

      /* allocates thread structure */
      p_thr_info = (cmdnfs_thr_info_t *) Mem_Alloc(sizeof(cmdnfs_thr_info_t));

      /* panic !!! */
      if(p_thr_info == NULL)
        {
          printf("%p:commands_NFS: Not enough memory\n", (caddr_t) pthread_self());
          return NULL;
        }

      /* Clean thread context */

      memset(p_thr_info, 0, sizeof(cmdnfs_thr_info_t));

      p_thr_info->is_thread_init = FALSE;
      p_thr_info->is_mounted_path = FALSE;

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_thr_info);

    }

  return p_thr_info;

}                               /* GetNFSClient */

int InitNFSClient(cmdnfs_thr_info_t * p_thr_info)
{
  uid_t uid;
  fsal_status_t st;
  struct passwd *pw_struct;

  if(p_thr_info == NULL)
    return -1;

  if(p_thr_info->is_thread_init == TRUE)
    return 0;

  /* for the moment, create export context for root fileset */
  st = FSAL_BuildExportContext(&p_thr_info->exp_context, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      printf
          ("%p:commands_NFS: Error %d initializing credentials for thread (FSAL_InitThreadCred)\n",
           (caddr_t) pthread_self(), st.major);
      return st.major;
    }

  /* initialize FSAL credential for this thread */

  st = FSAL_InitClientContext(&p_thr_info->context);

  if(FSAL_IS_ERROR(st))
    {
      printf
          ("%p:commands_NFS: Error %d initializing credentials for thread (FSAL_InitThreadCred)\n",
           (caddr_t) pthread_self(), st.major);
      return st.major;
    }

  uid = getuid();
  pw_struct = getpwuid(uid);

  if(pw_struct == NULL)
    {
      printf("commands_NFS: Unknown user %u\n", uid);
      return errno;
    }

  st = FSAL_GetClientContext(&p_thr_info->context, &p_thr_info->exp_context,
                             uid, pw_struct->pw_gid, NULL, 0);

  if(FSAL_IS_ERROR(st))
    {
      printf("%p:commands_NFS: Error %d getting contexte for uid %d (FSAL_GetUserCred)\n",
             (caddr_t) pthread_self(), st.major, uid);
      return st.major;
    }

  p_thr_info->authunix_struct.aup_machname = localmachine;
  p_thr_info->authunix_struct.aup_uid = uid;
  p_thr_info->authunix_struct.aup_gid = getgid();
  p_thr_info->authunix_struct.aup_len = 0; /** @todo No secondary groups support. */

  /* Init the cache_inode client */
  if(cache_inode_client_init(&p_thr_info->client, cache_client_param, 0, NULL) != 0)
    return 1;

  /* Init the cache content client */
  if(cache_content_client_init(&p_thr_info->dc_client, datacache_client_param, "") != 0)
    return 1;

  p_thr_info->client.pcontent_client = (caddr_t) & p_thr_info->dc_client;

  p_thr_info->is_thread_init = TRUE;

  return 0;

}                               /* InitNFSClient */

void nfs_layer_SetLogLevel(int log_lvl)
{

  /* Nothing to do. */
  return;

}

static void getopt_init()
{
  /* disables getopt error message */
  Opterr = 0;
  /* reinits getopt processing */
  Optind = 1;
}

int nfs_init(char *filename, int flag_v, FILE * output)
{
  config_file_t config_file;
  int rc;

  nfs_param.cache_layers_param.cache_content_client_param.nb_prealloc_entry = 100;
  nfs_param.cache_layers_param.cache_content_client_param.flush_force_fsal = 1;
  nfs_param.cache_layers_param.cache_content_client_param.max_fd_per_thread = 20;
  nfs_param.cache_layers_param.cache_content_client_param.use_cache = 0;
  nfs_param.cache_layers_param.cache_content_client_param.retention = 60;
  strcpy(nfs_param.cache_layers_param.cache_content_client_param.cache_dir,
         "/tmp/ganesha.datacache");

  /* Parse config file */

  config_file = config_ParseFile(filename);

  if(!config_file)
    {
      fprintf(output, "nfs_init: Error parsing %s: %s\n", filename, config_GetErrorMsg());
      return -1;
    }

  if((rc =
      cache_content_read_conf_client_parameter(config_file,
                                               &nfs_param.cache_layers_param.
                                               cache_content_client_param)) !=
     CACHE_CONTENT_SUCCESS)
    {
      fprintf(output, "nfs_init: Error %d reading cache content parameters.\n", -rc);
      return -1;
    }

  /* Read export list from file */

  rc = ReadExports(config_file, &pexportlist);

  if(rc < 0)
    {
      fprintf(output, "nfs_init: Error %d while parsing exports file.\n", -rc);
      return -1;
    }

  /* initalize export entries */
  if((rc = nfs_export_create_root_entry(pexportlist, ht)) != TRUE)
    {
      fprintf(output, "nfs_init: Error %d initializing root entries, exiting...", -rc);
      return -1;
    }

  /* geting the hostname */
  rc = gethostname(localmachine, sizeof(localmachine));
  if(rc != 0)
    {
      fprintf(output, "nfs_init: Error %d while getting hostname.\n", rc);
      return -1;
    }

  /** @todo Are there other things to initialize ? */

  is_nfs_layer_initialized = TRUE;

  if(flag_v)
    fprintf(output, "\tNFS layer successfully initialized.\n");

  return 0;
}

/** Init nfs layer */
int fn_nfs_init(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    )
{
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int option;
  char *filename = NULL;
  int rc;

  char format[] = "hv";

  const char help_nfs_init[] =
/*  "usage: nfs_init [options] <ganesha_config_file>\n"*/
      "usage: nfs_init [options] <ganesha_config_file>\n"
      "options :\n" "\t-h print this help\n" "\t-v verbose mode\n";

  if(is_nfs_layer_initialized != FALSE)
    {
      fprintf(output, "\tNFS layer is already initialized.\n");
      return 0;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "nfs_init: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "nfs_init: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "nfs_init: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }                       /* switch */
    }                           /* while */

  if(flag_h)
    {
      fprintf(output, help_nfs_init);
      return 0;
    }

  /* verifies mandatory argument */
  if(Optind != (argc - 1))
    {
      /* too much or not enough arguments */
      err_flag++;
    }
  else
    filename = argv[Optind];

  if(err_flag)
    {
      fprintf(output, help_nfs_init);
      return -1;
    }

  rc = nfs_init(filename, flag_v, output);

  return rc;

}                               /* fn_nfs_init */

/** process MNT1 protocol's command. */
int fn_MNT1_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  cmdnfs_funcdesc_t *funcdesc = mnt1_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;
  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {

          /* encoding args */

          if(funcdesc->func_encode(CMDNFS_ENCODE,
                                   argc - 1, argv + 1,
                                   0, NULL, (caddr_t) & nfs_arg) == FALSE)
            {
              fprintf(output, "%s: bad arguments.\n", argv[0]);
              fprintf(output, "Usage: %s\n", funcdesc->func_help);
              return -1;
            }

          /* preparing request identifier */
          req.rq_prog = MOUNTPROG;
          req.rq_vers = MOUNT_V1;

          req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

          /* nfs call */

          rc = funcdesc->func_call(&nfs_arg,
                                   pexportlist,
                                   &(p_thr_info->context),
                                   &(p_thr_info->client), ht, &req, &nfs_res);

          /* freeing args */

          funcdesc->func_encode(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

          /* decoding output */

#ifdef _DEBUG_NFS_SHELL
          printf("MNTv1: RETURNED STRUCTURE:\n");
          print_nfs_res(&nfs_res);
#endif

          funcdesc->func_decode(CMDNFS_DECODE, 0, NULL, 0, output, (caddr_t) & nfs_res);

          funcdesc->func_free(&nfs_res);

          /* returning status */
          return rc;

        }

      /* pointer to the next cmdnfs_funcdesc_t */
      funcdesc++;
    }

  fprintf(output, "%s: command not found in MNT1 protocol.\n", argv[0]);
  return -1;

}                               /* fn_MNT1_command */

/** process MNT3 protocol's command. */
int fn_MNT3_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  cmdnfs_funcdesc_t *funcdesc = mnt3_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;
  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {

          /* encoding args */

          if(funcdesc->func_encode(CMDNFS_ENCODE,
                                   argc - 1, argv + 1,
                                   0, NULL, (caddr_t) & nfs_arg) == FALSE)
            {
              fprintf(output, "%s: bad arguments.\n", argv[0]);
              fprintf(output, "Usage: %s\n", funcdesc->func_help);
              return -1;
            }

          /* preparing request identifier */
          req.rq_prog = MOUNTPROG;
          req.rq_vers = MOUNT_V3;

          req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

          /* nfs call */

          rc = funcdesc->func_call(&nfs_arg,
                                   pexportlist,
                                   &(p_thr_info->context),
                                   &(p_thr_info->client), ht, &req, &nfs_res);

          /* freeing args */

          funcdesc->func_encode(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

          /* decoding output */
#ifdef _DEBUG_NFS_SHELL
          printf("MNTv3: RETURNED STRUCTURE:\n");
          print_nfs_res(&nfs_res);
#endif

          funcdesc->func_decode(CMDNFS_DECODE, 0, NULL, 0, output, (caddr_t) & nfs_res);

          funcdesc->func_free(&nfs_res);

          /* returning status */
          return rc;

        }

      /* pointer to the next cmdnfs_funcdesc_t */
      funcdesc++;
    }

  fprintf(output, "%s: command not found in MNT3 protocol.\n", argv[0]);
  return -1;

}

/** process NFS2 protocol's command. */
int fn_NFS2_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  cmdnfs_funcdesc_t *funcdesc = nfs2_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;
  cmdnfs_thr_info_t *p_thr_info = NULL;

  short exportid;
  exportlist_t *pexport;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {

          /* encoding args */

          if(funcdesc->func_encode(CMDNFS_ENCODE,
                                   argc - 1, argv + 1,
                                   0, NULL, (caddr_t) & nfs_arg) == FALSE)
            {
              fprintf(output, "%s: bad arguments.\n", argv[0]);
              fprintf(output, "Usage: %s\n", funcdesc->func_help);
              return -1;
            }

          /* preparing request identifier */

          req.rq_prog = NFS_PROGRAM;
          req.rq_vers = NFS_V2;
          req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

          /* the only function that doesn't take a filehandle */
          if(funcdesc->func_call != nfs_Null)
            {
              exportid = nfs2_FhandleToExportId((fhandle2 *) & nfs_arg);
              if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
                {
                  /* invalid handle */
                  fprintf(output, "\t%s: Bad arguments: Invalid file handle.\n", argv[0]);
                  return -1;
                }
            }
          else
            pexport = NULL;

          /* nfs call */

          rc = funcdesc->func_call(&nfs_arg,
                                   pexport,
                                   &(p_thr_info->context),
                                   &(p_thr_info->client), ht, &req, &nfs_res);

          /* freeing args */

          funcdesc->func_encode(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

#ifdef _DEBUG_NFS_SHELL
          printf("NFSv2: RETURNED STRUCTURE:\n");
          print_nfs_res(&nfs_res);
#endif

          /* decoding output */

          funcdesc->func_decode(CMDNFS_DECODE, 0, NULL, 0, output, (caddr_t) & nfs_res);

          funcdesc->func_free(&nfs_res);

          /* returning status */
          return rc;

        }

      /* pointer to the next cmdnfs_funcdesc_t */
      funcdesc++;
    }

  fprintf(output, "%s: command not found in NFS2 protocol.\n", argv[0]);
  return -1;

}

/** process NFS3 protocol's command. */
int fn_NFS3_command(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  cmdnfs_funcdesc_t *funcdesc = nfs3_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;
  cmdnfs_thr_info_t *p_thr_info = NULL;

  short exportid;
  exportlist_t *pexport;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {

          /* encoding args */

          if(funcdesc->func_encode(CMDNFS_ENCODE,
                                   argc - 1, argv + 1,
                                   0, NULL, (caddr_t) & nfs_arg) == FALSE)
            {
              fprintf(output, "%s: bad arguments.\n", argv[0]);
              fprintf(output, "Usage: %s\n", funcdesc->func_help);
              return -1;
            }

          /* preparing request identifier */

          req.rq_prog = NFS_PROGRAM;
          req.rq_vers = NFS_V3;
          req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

          /* the only function that doesn't take a filehandle */
          if(funcdesc->func_call != nfs_Null)
            {
              exportid = nfs3_FhandleToExportId((nfs_fh3 *) & nfs_arg);
              if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
                {
                  /* invalid handle */
                  fprintf(output, "\t%s: Bad arguments: Invalid file handle.\n", argv[0]);
                  return -1;
                }
            }
          else
            pexport = NULL;

          /* nfs call */

          rc = funcdesc->func_call(&nfs_arg,
                                   pexport,
                                   &(p_thr_info->context),
                                   &(p_thr_info->client), ht, &req, &nfs_res);

          /* freeing args */

          funcdesc->func_encode(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

#ifdef _DEBUG_NFS_SHELL
          printf("NFSv3: RETURNED STRUCTURE:\n");
          print_nfs_res(&nfs_res);
#endif

          /* decoding output */

          funcdesc->func_decode(CMDNFS_DECODE, 0, NULL, 0, output, (caddr_t) & nfs_res);

          funcdesc->func_free(&nfs_res);

          /* returning status */
          return rc;

        }

      /* pointer to the next cmdnfs_funcdesc_t */
      funcdesc++;
    }

  fprintf(output, "%s: command not found in NFS3 protocol.\n", argv[0]);
  return -1;

}                               /* fn_NFS3_command */

/*------------------------------------------------------------
 *     Wrapping of NFS calls (used by high level commands)
 *-----------------------------------------------------------*/

/* solves a relative or aboslute path */
static int nfs_solvepath(cmdnfs_thr_info_t * p_thr_info, char *io_global_path,  /* global path */
                         int size_global_path,  /* max size for global path */
                         char *i_spec_path,     /* specified path */
                         shell_fh3_t * p_current_hdl,   /* current directory handle */
                         shell_fh3_t * pnew_hdl,        /* pointer to solved handle */
                         FILE * output)
{
  char str_path[NFS2_MAXPATHLEN];
  char *pstr_path = str_path;

  char tmp_path[NFS2_MAXPATHLEN];
  char *next_name;
  char *curr;
  int last = 0;
  int rc;

  shell_fh3_t hdl_lookup;
  nfs_fh3 hdl_param;

  diropargs3 dirop_arg;
  LOOKUP3res lookup_res;
  struct svc_req req;
  short exportid;
  exportlist_t *pexport;

  strncpy(str_path, i_spec_path, NFS2_MAXPATHLEN);
  curr = str_path;
  next_name = str_path;

  if(str_path[0] == '@')
    {

      rc = cmdnfs_fhandle3(CMDNFS_ENCODE, 1, &pstr_path, 0, NULL, (caddr_t) & hdl_param);

      if(rc != TRUE)
        {
          fprintf(output, "Invalid FileHandle: %s\n", str_path);
          return -1;
        }

      strncpy(io_global_path, str_path, size_global_path);

      set_shell_fh3(pnew_hdl, &hdl_param);

      cmdnfs_fhandle3(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & hdl_param);

      return 0;

    }
  else if(str_path[0] == '/')
    {
      /* absolute path, starting from "/", with a relative path */
      curr++;
      next_name++;
      hdl_lookup = p_thr_info->mounted_path_hdl;
      strncpy(tmp_path, "/", NFS2_MAXPATHLEN);

      /* the the directory  is /, return */
      if(str_path[1] == '\0')
        {
          strncpy(io_global_path, tmp_path, size_global_path);
          *pnew_hdl = hdl_lookup;
          return 0;
        }

    }
  else
    {
      hdl_lookup = *p_current_hdl;
      strncpy(tmp_path, io_global_path, NFS2_MAXPATHLEN);
    }

  /* Now, the path is a relative path, proceed a step by step lookup */
  do
    {
      /* tokenize to the next '/' */
      while((curr[0] != '\0') && (curr[0] != '/'))
        curr++;

      if(!curr[0])
        last = 1;               /* remembers if it was the last dir */

      curr[0] = '\0';

      /* build the arguments */

      set_nfs_fh3(&dirop_arg.dir, &hdl_lookup);
      dirop_arg.name = next_name;

      /* preparing request identifier */

      req.rq_prog = NFS_PROGRAM;
      req.rq_vers = NFS_V3;
      req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

      exportid = nfs3_FhandleToExportId(&dirop_arg.dir);
      if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
        {
          /* invalid handle */
          fprintf(output, "\tBad arguments: Invalid file handle.\n");
          return -1;
        }

      /* lookup this name */

      rc = nfs_Lookup((nfs_arg_t *) & dirop_arg,
                      pexport,
                      &(p_thr_info->context),
                      &(p_thr_info->client), ht, &req, (nfs_res_t *) & lookup_res);

      if(rc != 0)
        {
          fprintf(output, "Error %d in nfs_Lookup.\n", rc);
          return rc;
        }

      rc = lookup_res.status;
      if(rc != NFS3_OK)
        {
          nfs3_Lookup_Free((nfs_res_t *) & lookup_res);
          fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
          return rc;
        }

      /* updates current handle */
      set_shell_fh3(&hdl_lookup, &lookup_res.LOOKUP3res_u.resok.object);

      nfs3_Lookup_Free((nfs_res_t *) & lookup_res);

      /* adds /name at the end of the path */
      strncat(tmp_path, "/", FSAL_MAX_PATH_LEN);
      strncat(tmp_path, next_name, FSAL_MAX_PATH_LEN - strlen(tmp_path));

      /* updates cursors */
      if(!last)
        {
          curr++;
          next_name = curr;
          /* ignore successive slashes */
          while((curr[0] != '\0') && (curr[0] == '/'))
            {
              curr++;
              next_name = curr;
            }
          if(!curr[0])
            last = 1;           /* it is the last dir */
        }

    }
  while(!last);

  /* everything is OK, apply changes */
  clean_path(tmp_path, size_global_path);
  strncpy(io_global_path, tmp_path, size_global_path);

  *pnew_hdl = hdl_lookup;
  return 0;

}                               /* nfs_solvepath */

static int nfs_getattr(cmdnfs_thr_info_t * p_thr_info, shell_fh3_t * p_hdl,
                       fattr3 * attrs, FILE * output)
{
  GETATTR3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  nfs_fh3 nfshdl;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  set_nfs_fh3(&nfshdl, p_hdl);

  exportid = nfs3_FhandleToExportId(&nfshdl);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Getattr((nfs_arg_t *) & nfshdl,
                   pexport,
                   &(p_thr_info->context),
                   &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Getattr.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));

      nfs_Getattr_Free((nfs_res_t *) & res);
      return rc;
    }

  /* updates current handle */
  *attrs = res.GETATTR3res_u.resok.obj_attributes;

  nfs_Getattr_Free((nfs_res_t *) & res);

  return 0;

}

static int nfs_access(cmdnfs_thr_info_t * p_thr_info, shell_fh3_t * p_hdl, nfs3_uint32 * access_mask,   /* IN/OUT */
                      FILE * output)
{
  ACCESS3args arg;
  ACCESS3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing args */
  set_nfs_fh3(&arg.object, p_hdl);
  arg.access = *access_mask;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  exportid = nfs3_FhandleToExportId(&arg.object);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs3_Access((nfs_arg_t *) & arg,
                   pexport,
                   &(p_thr_info->context),
                   &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Access.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_Access_Free((nfs_res_t *) & res);
      return rc;
    }

  /* updates access mask */
  *access_mask = res.ACCESS3res_u.resok.access;

  nfs3_Access_Free((nfs_res_t *) & res);

  return 0;

}

static int nfs_readlink(cmdnfs_thr_info_t * p_thr_info, shell_fh3_t * p_hdl,
                        char *linkcontent, FILE * output)
{
  READLINK3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  nfs_fh3 nfshdl;

  set_nfs_fh3(&nfshdl, p_hdl);

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  exportid = nfs3_FhandleToExportId(&nfshdl);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Readlink((nfs_arg_t *) & nfshdl,
                    pexport,
                    &(p_thr_info->context),
                    &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Readlink.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_Readlink_Free((nfs_res_t *) & res);
      return rc;
    }

  /* copy link content */
  strcpy(linkcontent, res.READLINK3res_u.resok.data);

  nfs3_Readlink_Free((nfs_res_t *) & res);

  return 0;

}                               /* nfs_readlink */

static int nfs_readdirplus(cmdnfs_thr_info_t * p_thr_info, shell_fh3_t * p_dir_hdl, cookie3 cookie, cookieverf3 * p_cookieverf, /* IN/OUT */
                           dirlistplus3 * dirlist,
                           nfs_res_t ** to_be_freed, FILE * output)
{
  READDIRPLUS3args arg;
  READDIRPLUS3res *p_res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  *to_be_freed = NULL;

  /* args */
  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.cookie = cookie;
  memcpy(&arg.cookieverf, p_cookieverf, sizeof(cookieverf3));
  arg.dircount = 1024;
  arg.maxcount = 4096;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  exportid = nfs3_FhandleToExportId(&arg.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  p_res = (READDIRPLUS3res *) Mem_Alloc(sizeof(READDIRPLUS3res));

  rc = nfs3_Readdirplus((nfs_arg_t *) & arg,
                        pexport,
                        &(p_thr_info->context),
                        &(p_thr_info->client), ht, &req, (nfs_res_t *) p_res);

  if(rc != 0)
    {
      Mem_Free(p_res);
      fprintf(output, "Error %d in nfs3_Readdirplus.\n", rc);
      return rc;
    }

  rc = p_res->status;
  if(rc != NFS3_OK)
    {
      nfs3_Readdirplus_Free((nfs_res_t *) p_res);
      Mem_Free(p_res);
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      return rc;
    }

  memcpy(p_cookieverf, p_res->READDIRPLUS3res_u.resok.cookieverf, sizeof(cookieverf3));

  *dirlist = p_res->READDIRPLUS3res_u.resok.reply;
  *to_be_freed = (nfs_res_t *) p_res;

  return 0;

}                               /* nfs_readdirplus */

void nfs_readdirplus_free(nfs_res_t * to_free)
{
  if(to_free == NULL)
    return;

  nfs3_Readdirplus_Free((nfs_res_t *) to_free);
  Mem_Free(to_free);
}

static int nfs_create(cmdnfs_thr_info_t * p_thr_info,
                      shell_fh3_t * p_dir_hdl, char *obj_name,
                      mode_t posix_mode, shell_fh3_t * p_obj_hdl, FILE * output)
{
  CREATE3args arg;
  CREATE3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, p_dir_hdl);
  arg.where.name = obj_name;
  arg.how.mode = GUARDED;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  exportid = nfs3_FhandleToExportId(&arg.where.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  /* empty sattr3 list */
  if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL,
                   (caddr_t) & (arg.how.createhow3_u.obj_attributes)) == FALSE)
    {
      /* invalid handle */
      fprintf(output, "\tError encoding nfs arguments.\n");
      return -1;
    }

  /* only setting mode */
  arg.how.createhow3_u.obj_attributes.mode.set_it = TRUE;
  arg.how.createhow3_u.obj_attributes.mode.set_mode3_u.mode = posix_mode;

  rc = nfs_Create((nfs_arg_t *) & arg,
                  pexport,
                  &(p_thr_info->context),
                  &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Create.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Create_Free((nfs_res_t *) & res);
      return rc;
    }

  /* object handle */
  if(res.CREATE3res_u.resok.obj.handle_follows)
    set_shell_fh3(p_obj_hdl, &res.CREATE3res_u.resok.obj.post_op_fh3_u.handle);
  else
    fprintf(output, "Warning: nfs_Create did not return file handle.\n");

  nfs_Create_Free((nfs_res_t *) & res);

  return 0;

}

static int nfs_mkdir(cmdnfs_thr_info_t * p_thr_info,
                     shell_fh3_t * p_dir_hdl, char *obj_name,
                     mode_t posix_mode, shell_fh3_t * p_obj_hdl, FILE * output)
{
  MKDIR3args arg;
  MKDIR3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, p_dir_hdl);
  arg.where.name = obj_name;

  exportid = nfs3_FhandleToExportId(&arg.where.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  /* empty sattr3 list */
  if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL,
                   (caddr_t) & (arg.attributes)) == FALSE)
    {
      /* invalid handle */
      fprintf(output, "\tError encoding nfs arguments.\n");
      return -1;
    }

  /* only setting mode */
  arg.attributes.mode.set_it = TRUE;
  arg.attributes.mode.set_mode3_u.mode = posix_mode;

  rc = nfs_Mkdir((nfs_arg_t *) & arg,
                 pexport,
                 &(p_thr_info->context),
                 &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Mkdir.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Mkdir_Free((nfs_res_t *) & res);
      return rc;
    }

  /* object handle */
  if(res.MKDIR3res_u.resok.obj.handle_follows)
    set_shell_fh3(p_obj_hdl, &res.MKDIR3res_u.resok.obj.post_op_fh3_u.handle);
  else
    fprintf(output, "Warning: nfs_Mkdir did not return file handle.\n");

  nfs_Mkdir_Free((nfs_res_t *) & res);

  return 0;

}                               /*nfs_mkdir */

static int nfs_rmdir(cmdnfs_thr_info_t * p_thr_info,
                     shell_fh3_t * p_dir_hdl, char *obj_name, FILE * output)
{
  diropargs3 arg;
  RMDIR3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.name = obj_name;

  /* extract expoort id */
  exportid = nfs3_FhandleToExportId(&arg.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Rmdir((nfs_arg_t *) & arg,
                 pexport,
                 &(p_thr_info->context),
                 &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Rmdir.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Rmdir_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs_Rmdir_Free((nfs_res_t *) & res);
  return 0;

}                               /* nfs_rmdir */

static int nfs_remove(cmdnfs_thr_info_t * p_thr_info,
                      shell_fh3_t * p_dir_hdl, char *obj_name, FILE * output)
{
  diropargs3 arg;
  REMOVE3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.name = obj_name;

  /* extract export id */

  exportid = nfs3_FhandleToExportId(&arg.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Remove((nfs_arg_t *) & arg,
                  pexport,
                  &(p_thr_info->context),
                  &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Remove.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Remove_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs_Remove_Free((nfs_res_t *) & res);
  return 0;

}                               /* nfs_remove */

static int nfs_setattr(cmdnfs_thr_info_t * p_thr_info,
                       shell_fh3_t * p_obj_hdl, sattr3 * p_attributes, FILE * output)
{
  SETATTR3args arg;
  SETATTR3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.object, p_obj_hdl);
  arg.new_attributes = *p_attributes;
  arg.guard.check = FALSE;

  /* extract export id */
  exportid = nfs3_FhandleToExportId(&arg.object);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Setattr((nfs_arg_t *) & arg,
                   pexport,
                   &(p_thr_info->context),
                   &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Setattr.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Setattr_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs_Setattr_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_setattr */

static int nfs_rename(cmdnfs_thr_info_t * p_thr_info,
                      shell_fh3_t * p_src_dir_hdl, char *src_name,
                      shell_fh3_t * p_tgt_dir_hdl, char *tgt_name, FILE * output)
{
  RENAME3args arg;
  RENAME3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.from.dir, p_src_dir_hdl);
  arg.from.name = src_name;
  set_nfs_fh3(&arg.to.dir, p_tgt_dir_hdl);
  arg.to.name = tgt_name;

  /* extract export id */
  exportid = nfs3_FhandleToExportId(&arg.from.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Rename((nfs_arg_t *) & arg,
                  pexport,
                  &(p_thr_info->context),
                  &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Rename.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Rename_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs_Rename_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_rename */

static int nfs_link(cmdnfs_thr_info_t * p_thr_info,
                    shell_fh3_t * p_file_hdl,
                    shell_fh3_t * p_tgt_dir_hdl, char *tgt_name, FILE * output)
{
  LINK3args arg;
  LINK3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.file, p_file_hdl);
  set_nfs_fh3(&arg.link.dir, p_tgt_dir_hdl);
  arg.link.name = tgt_name;

  /* extract export id */
  exportid = nfs3_FhandleToExportId(&arg.file);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Link((nfs_arg_t *) & arg,
                pexport,
                &(p_thr_info->context),
                &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Link.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs_Link_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs_Link_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_link */

static int nfs_symlink(cmdnfs_thr_info_t * p_thr_info,
                       shell_fh3_t path_hdl, char *link_name,
                       char *link_content, sattr3 * p_setattr,
                       shell_fh3_t * p_link_hdl, FILE * output)
{
  SYMLINK3args arg;
  SYMLINK3res res;
  struct svc_req req;
  unsigned short exportid;
  exportlist_t *pexport;
  int rc;

  /* preparing request identifier */

  req.rq_prog = NFS_PROGRAM;
  req.rq_vers = NFS_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, &path_hdl);
  arg.where.name = link_name;
  arg.symlink.symlink_attributes = *p_setattr;
  arg.symlink.symlink_data = link_content;

  exportid = nfs3_FhandleToExportId(&arg.where.dir);
  if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
    {
      /* invalid handle */
      fprintf(output, "\tBad arguments: Invalid file handle.\n");
      return -1;
    }

  rc = nfs_Symlink((nfs_arg_t *) & arg,
                   pexport,
                   &(p_thr_info->context),
                   &(p_thr_info->client), ht, &req, (nfs_res_t *) & res);

  if(rc != 0)
    {
      fprintf(output, "Error %d in nfs_Symlink.\n", rc);
      return rc;
    }

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      /* free nfs call resources */
      nfs_Symlink_Free((nfs_res_t *) & res);
      return rc;
    }

  /* returned handle */
  if(res.SYMLINK3res_u.resok.obj.handle_follows)
    {
      set_shell_fh3(p_link_hdl, &res.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle);
    }
  else
    {
      fprintf(output, "Warning: nfs_Symlink did not return file handle.\n");
    }

  /* free nfs call resources */
  nfs_Symlink_Free((nfs_res_t *) & res);

  return 0;

}                               /*nfs_symlink */

/*------------------------------------------------------------
 *          High level, shell-like commands
 *-----------------------------------------------------------*/

/** mount a path to browse it. */
int fn_nfs_mount(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output) /* IN : output stream          */
{
  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;
  char buff[2 * NFS3_FHSIZE + 1];
  mountres3 *p_mountres = (mountres3 *) & nfs_res;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* We only need to init thread in mount command. */
  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  /* check if a path has already been mounted */

  if(p_thr_info->is_mounted_path != FALSE)
    {
      fprintf(output, "%s: a path is already mounted. Use \"umount\" command first.\n",
              argv[0]);
      return -1;
    }

  if(cmdnfs_dirpath(CMDNFS_ENCODE,
                    argc - 1, argv + 1, 0, NULL, (caddr_t) & nfs_arg) == FALSE)
    {
      fprintf(output, "%s: bad arguments.\n", argv[0]);
      fprintf(output, "Usage: mount <path>.\n");
      return -1;
    }

  /* preparing request identifier */
  req.rq_prog = MOUNTPROG;
  req.rq_vers = MOUNT_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* nfs call */

  rc = mnt_Mnt(&nfs_arg,
               pexportlist,
               &(p_thr_info->context), &(p_thr_info->client), ht, &req, &nfs_res);

  /* freeing args */

  cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

  if(rc != 0)
    {
      fprintf(output, "%s: Error %d in mnt_Mnt.\n", argv[0], rc);
      return rc;
    }

  rc = p_mountres->fhs_status;
  if(rc != MNT3_OK)
    {
      mnt3_Mnt_Free(&nfs_res);
      fprintf(output, "%s: Error %d in MNT3 protocol.\n", argv[0], rc);
      return rc;
    }

  set_shell_fh3(&p_thr_info->mounted_path_hdl,
                (nfs_fh3 *) & p_mountres->mountres3_u.mountinfo.fhandle);

  mnt3_Mnt_Free(&nfs_res);

  strcpy(p_thr_info->mounted_path, argv[1]);

  p_thr_info->current_path_hdl = p_thr_info->mounted_path_hdl;
  strcpy(p_thr_info->current_path, "/");

  p_thr_info->is_mounted_path = TRUE;

  fprintf(output, "Current directory is \"%s\" \n", p_thr_info->current_path);
  snprintmem(buff, 2 * NFS3_FHSIZE + 1,
             (caddr_t) p_thr_info->current_path_hdl.data_val,
             p_thr_info->current_path_hdl.data_len);
  fprintf(output, "Current File handle is \"@%s\" \n", buff);

  return 0;
}

/** umount a mounted path. */
int fn_nfs_umount(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output)        /* IN : output stream          */
{
  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  struct svc_req req;
  int rc;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* We only need to init thread in mount command. */
  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  /* check if a path has already been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  if(cmdnfs_dirpath(CMDNFS_ENCODE,
                    argc - 1, argv + 1, 0, NULL, (caddr_t) & nfs_arg) == FALSE)
    {
      fprintf(output, "%s: bad arguments.\n", argv[0]);
      fprintf(output, "Usage: umount <path>.\n");
      return -1;
    }

  if(strncmp(argv[1], p_thr_info->mounted_path, NFS2_MAXPATHLEN))
    {
      fprintf(output, "%s: this path is not mounted.\n", argv[0]);
      fprintf(output, "Current monted path : %s.\n", p_thr_info->mounted_path);
      return -1;
    }

  /* preparing request identifier */
  req.rq_prog = MOUNTPROG;
  req.rq_vers = MOUNT_V3;
  req.rq_clntcred = (caddr_t) & p_thr_info->authunix_struct;

  /* nfs call */

  rc = mnt_Umnt(&nfs_arg,
                pexportlist,
                &(p_thr_info->context), &(p_thr_info->client), ht, &req, &nfs_res);

  /* freeing args */

  cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

  if(rc != 0)
    {
      fprintf(output, "%s: Error %d in mnt_Umnt.\n", argv[0], rc);
      return rc;
    }

  mnt_Umnt_Free(&nfs_res);

  p_thr_info->is_mounted_path = FALSE;

  return 0;
}

/** prints current path */
int fn_nfs_pwd(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output)   /* IN : output stream          */
{
  char buff[2 * NFS3_FHSIZE + 1];
  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  fprintf(output, "Current directory is \"%s\" \n", p_thr_info->current_path);
  snprintmem(buff, 2 * NFS3_FHSIZE + 1,
             (caddr_t) p_thr_info->current_path_hdl.data_val,
             p_thr_info->current_path_hdl.data_len);
  fprintf(output, "Current File handle is \"@%s\" \n", buff);

  return 0;
}

/** proceed an ls command using NFS protocol NFS */
int fn_nfs_ls(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output)    /* IN : output stream          */
{
#define NFS_READDIR_SIZE 10

  char linkdata[NFS2_MAXPATHLEN];
  char item_path[NFS2_MAXPATHLEN];
  char *str_name = ".";
  shell_fh3_t handle_tmp;
  fattr3 attrs;
  cookie3 begin_cookie;
  bool_t eod_met;
  cookieverf3 cookieverf;
  dirlistplus3 dirlist;
  entryplus3 *p_entry;

  fattr3 *p_attrs;
  shell_fh3_t hdl;
  shell_fh3_t *p_hdl = NULL;

  nfs_res_t *to_free = NULL;

  int rc = 0;
  char glob_path[NFS2_MAXPATHLEN];

  static char format[] = "hvdlSHz";
  const char help_ls[] = "usage: ls [options] [name|path]\n"
      "options :\n"
      "\t-h print this help\n"
      "\t-v verbose mode\n"
      "\t-d print directory info instead of listing its content\n"
      "\t-l print standard UNIX attributes\n"
      "\t-S print all supported attributes\n"
      "\t-H print the NFS handle\n" "\t-z silent mode (print nothing)\n";

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int flag_d = 0;
  int flag_l = 0;
  int flag_S = 0;
  int flag_H = 0;
  int flag_z = 0;
  int err_flag = 0;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "ls: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "ls: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case 'd':
          if(flag_d)
            fprintf(output,
                    "ls: warning: option 'd' has been specified more than once.\n");
          else
            flag_d++;
          break;

        case 'l':
          if(flag_l)
            fprintf(output,
                    "ls: warning: option 'l' has been specified more than once.\n");
          else
            flag_l++;
          break;

        case 'S':
          if(flag_S)
            fprintf(output,
                    "ls: warning: option 'S' has been specified more than once.\n");
          else
            flag_S++;
          break;

        case 'z':
          if(flag_z)
            fprintf(output,
                    "ls: warning: option 'z' has been specified more than once.\n");
          else
            flag_z++;
          break;

        case 'H':
          if(flag_H)
            fprintf(output,
                    "ls: warning: option 'H' has been specified more than once.\n");
          else
            flag_H++;
          break;

        case '?':
          fprintf(output, "ls: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }                           /* while */

  if(flag_l + flag_S + flag_H > 1)
    {
      fprintf(output, "ls: conflict between options l,S,H\n");
      err_flag++;
    }

  if(flag_z + flag_v > 1)
    {
      fprintf(output, "ls: can't use -z and -v at the same time\n");
      err_flag++;
    }

  if(flag_h)
    {
      fprintf(output, help_ls);
      return 0;
    }

  if(err_flag)
    {
      fprintf(output, help_ls);
      return -1;
    }

  /* copy current global path */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* first, retrieve the argument (if any) */
  if(Optind == argc - 1)
    {
      str_name = argv[Optind];

      /* retrieving handle */
      if((rc = nfs_solvepath(p_thr_info,
                            glob_path,
                            NFS2_MAXPATHLEN,
                             str_name, &p_thr_info->current_path_hdl, &handle_tmp, output)))
        return rc;
    }
  else
    {
      str_name = ".";
      handle_tmp = p_thr_info->current_path_hdl;
    }

  if(flag_v)
    fprintf(output, "proceeding ls (using NFS protocol) on \"%s\"\n", glob_path);

  if((rc = nfs_getattr(p_thr_info, &handle_tmp, &attrs, output)))
    return rc;

  /*
   * if the object is a file or a directoy with the -d option specified,
   * we only show its info and exit.
   */
  if((attrs.type != NF3DIR) || flag_d)
    {
      if((attrs.type == NF3LNK) && flag_l)
        {
          if((rc = nfs_readlink(p_thr_info, &handle_tmp, linkdata, output)))
            return rc;
        }

      if(flag_l)
        {
          if(!flag_z)
            print_nfsitem_line(output, &attrs, str_name, linkdata);
        }
      else if(flag_S)
        {
          if(!flag_z)
            {
              fprintf(output, "%s :\n", str_name);
              print_nfs_attributes(&attrs, output);
            }
        }
      else if(flag_H)
        {
          if(!flag_z)
            {
              char buff[2 * NFS3_FHSIZE + 1];

              snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) handle_tmp.data_val,
                         handle_tmp.data_len);
              fprintf(output, "%s (@%s)\n", str_name, buff);
            }
        }
      else                      /* only prints the name */
        {
          if(!flag_z)
            fprintf(output, "%s\n", str_name);
        }

      return 0;
    }

  /* If this point is reached, then the current element is a directory */

  begin_cookie = 0LL;
  eod_met = FALSE;
  memset(&cookieverf, 0, sizeof(cookieverf3));

  while(!eod_met)
    {

      if(flag_v)
        fprintf(output, "-->nfs3_Readdirplus( path=%s, cookie=%llu )\n",
                glob_path, begin_cookie);

      if((rc = nfs_readdirplus(p_thr_info, &handle_tmp, begin_cookie, &cookieverf,       /* IN/OUT */
                               &dirlist, &to_free, output)))
        return rc;

      p_entry = dirlist.entries;

      while(p_entry)
        {
          if(!strcmp(str_name, "."))
            strncpy(item_path, p_entry->name, NFS2_MAXPATHLEN);
          else if(str_name[strlen(str_name) - 1] == '/')
            snprintf(item_path, NFS2_MAXPATHLEN, "%s%s", str_name, p_entry->name);
          else
            snprintf(item_path, NFS2_MAXPATHLEN, "%s/%s", str_name, p_entry->name);

          /* interpreting post-op attributes */

          if(p_entry->name_attributes.attributes_follow)
            p_attrs = &p_entry->name_attributes.post_op_attr_u.attributes;
          else
            p_attrs = NULL;

          /* interpreting post-op handle */

          if(p_entry->name_handle.handle_follows)
            {
              set_shell_fh3(&hdl, &p_entry->name_handle.post_op_fh3_u.handle);
              p_hdl = &hdl;
            }
          else
            p_hdl = NULL;

          if((p_attrs != NULL) && (p_hdl != NULL) && (p_attrs->type == NF3LNK))
            {
              if((rc = nfs_readlink(p_thr_info, p_hdl, linkdata, output)))
                return rc;
            }

          if((p_attrs != NULL) && flag_l)
            {
              print_nfsitem_line(output, p_attrs, item_path, linkdata);
            }
          else if((p_attrs != NULL) && flag_S)
            {
              fprintf(output, "%s :\n", item_path);
              if(!flag_z)
                print_nfs_attributes(p_attrs, output);
            }
          else if((p_hdl != NULL) && flag_H)
            {
              if(!flag_z)
                {
                  char buff[2 * NFS3_FHSIZE + 1];

                  snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) p_hdl->data_val,
                             p_hdl->data_len);
                  fprintf(output, "%s (@%s)\n", item_path, buff);
                }
            }
          else
            {
              if(!flag_z)
                fprintf(output, "%s\n", item_path);
            }

          begin_cookie = p_entry->cookie;
          p_entry = p_entry->nextentry;
        }

      /* Ready for next iteration */
      eod_met = dirlist.eof;

    }

  nfs_readdirplus_free(to_free);

  return 0;
}                               /* fn_nfs_ls */

/** change current path */
int fn_nfs_cd(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    )
{

  const char help_cd[] = "usage: cd <path>\n";

  char glob_path[NFS2_MAXPATHLEN];
  shell_fh3_t new_hdl;
  int rc;
  fattr3 attrs;
  nfs3_uint32 mask;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* Exactly one arg expected */
  if(argc != 2)
    {
      fprintf(output, help_cd);
      return -1;
    }

  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  if((rc =
     nfs_solvepath(p_thr_info, glob_path, NFS2_MAXPATHLEN,
                   argv[1], &p_thr_info->current_path_hdl, &new_hdl, output)))
    return rc;

  /* verify if the object is a directory */

  if((rc = nfs_getattr(p_thr_info, &new_hdl, &attrs, output)))
    return rc;

  if(attrs.type != NF3DIR)
    {
      fprintf(output, "Error: %s is not a directory\n", glob_path);
      return ENOTDIR;
    }

  /* verify lookup permission  */
  mask = ACCESS3_LOOKUP;
  if((rc = nfs_access(p_thr_info, &new_hdl, &mask, output)))
    return rc;

  if(!(mask & ACCESS3_LOOKUP))
    {
      fprintf(output, "Error: %s: permission denied.\n", glob_path);
      return EACCES;
    }

  /* if so, apply changes */
  strncpy(p_thr_info->current_path, glob_path, NFS2_MAXPATHLEN);
  p_thr_info->current_path_hdl = new_hdl;

  {
    char buff[2 * NFS3_FHSIZE + 1];
    fprintf(output, "Current directory is \"%s\" \n", p_thr_info->current_path);
    snprintmem(buff, 2 * NFS3_FHSIZE + 1,
               (caddr_t) p_thr_info->current_path_hdl.data_val,
               p_thr_info->current_path_hdl.data_len);
    fprintf(output, "Current File handle is \"@%s\" \n", buff);
  }

  return 0;

}

/** create a file */
int fn_nfs_create(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{
  static char format[] = "hv";

  const char help_create[] =
      "usage: create [-h][-v] <path> <mode>\n"
      "       path: path of the file to be created\n"
      "       mode: octal mode for the directory to be created (ex: 644)\n";

  char glob_path[NFS2_MAXPATHLEN];
  shell_fh3_t new_hdl;
  shell_fh3_t subdir_hdl;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode = 0644;

  char tmp_path[NFS2_MAXPATHLEN];
  char *path;
  char *file;
  char *strmode;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "create: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "create: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "create: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_create);
      return 0;
    }

  /* Exactly 2 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      strncpy(tmp_path, argv[Optind], NFS2_MAXPATHLEN);
      split_path(tmp_path, &path, &file);

      strmode = argv[Optind + 1];

      /* converting mode string to posix mode */
      mode = atomode(strmode);
      if(mode < 0)
        err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_create);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc = nfs_solvepath(p_thr_info, glob_path, NFS2_MAXPATHLEN,
                         path, &p_thr_info->current_path_hdl, &subdir_hdl, output)))
    return rc;

  if((rc = nfs_create(p_thr_info, &subdir_hdl, file, mode, &new_hdl, output)))
    return rc;

  if(flag_v)
    {
      char buff[2 * NFS3_FHSIZE + 1];
      snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) new_hdl.data_val, new_hdl.data_len);
      fprintf(output, "%s/%s successfully created.\n(handle=@%s)\n", glob_path, file,
              buff);
    }

  return 0;

}

/** create a directory */
int fn_nfs_mkdir(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{
  static char format[] = "hv";

  const char help_mkdir[] =
      "usage: mkdir [-h][-v] <path> <mode>\n"
      "       path: path of the directory to be created\n"
      "       mode: octal mode for the dir to be created (ex: 755)\n";

  char glob_path[NFS2_MAXPATHLEN];
  shell_fh3_t new_hdl;
  shell_fh3_t subdir_hdl;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode = 0755;

  char tmp_path[NFS2_MAXPATHLEN];
  char *path;
  char *file;
  char *strmode;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "mkdir: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "mkdir: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "mkdir: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_mkdir);
      return 0;
    }

  /* Exactly 2 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      strncpy(tmp_path, argv[Optind], NFS2_MAXPATHLEN);
      split_path(tmp_path, &path, &file);

      strmode = argv[Optind + 1];

      /* converting mode string to posix mode */
      mode = atomode(strmode);
      if(mode < 0)
        err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_mkdir);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc = nfs_solvepath(p_thr_info, glob_path, NFS2_MAXPATHLEN,
                         path, &p_thr_info->current_path_hdl, &subdir_hdl, output)))
    return rc;

  if((rc = nfs_mkdir(p_thr_info, &subdir_hdl, file, mode, &new_hdl, output)))
    return rc;

  if(flag_v)
    {
      char buff[2 * NFS3_FHSIZE + 1];
      snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) new_hdl.data_val, new_hdl.data_len);
      fprintf(output, "%s/%s successfully created.\n(handle=@%s)\n", glob_path, file,
              buff);
    }

  return 0;

}

/** unlink a file */
int fn_nfs_unlink(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{
  static char format[] = "hv";

  const char help_unlink[] =
      "usage: unlink [-h][-v] <path>\n"
      "       path: path of the directory to be unlinkd\n";

  char glob_path_parent[NFS2_MAXPATHLEN];
  char glob_path_object[NFS2_MAXPATHLEN];
  shell_fh3_t subdir_hdl;
  shell_fh3_t obj_hdl;
  fattr3 attrs;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char tmp_path[NFS2_MAXPATHLEN];
  char *path;
  char *file;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "unlink: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "unlink: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "unlink: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_unlink);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      strncpy(tmp_path, argv[Optind], NFS2_MAXPATHLEN);
      split_path(tmp_path, &path, &file);
    }

  /* copy current path. */
  strncpy(glob_path_parent, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves parent dir handle */
  if((rc = nfs_solvepath(p_thr_info, glob_path_parent, NFS2_MAXPATHLEN,
                         path, &p_thr_info->current_path_hdl, &subdir_hdl, output)))
    return rc;

  /* copy parent path */
  strncpy(glob_path_object, glob_path_parent, NFS2_MAXPATHLEN);

  /* lookup on child object */
  if((rc = nfs_solvepath(p_thr_info, glob_path_object, NFS2_MAXPATHLEN,
                         file, &subdir_hdl, &obj_hdl, output)))
    return rc;

  /* get attributes of child object */
  if(flag_v)
    fprintf(output, "Getting attributes for %s...\n", glob_path_object);

  if((rc = nfs_getattr(p_thr_info, &obj_hdl, &attrs, output)))
    return rc;

  if(attrs.type != NF3DIR)
    {
      if(flag_v)
        fprintf(output, "%s is not a directory: calling nfs3_remove...\n",
                glob_path_object);

      if((rc = nfs_remove(p_thr_info, &subdir_hdl, file, output)))
        return rc;
    }
  else
    {
      if(flag_v)
        fprintf(output, "%s is a directory: calling nfs3_rmdir...\n", glob_path_object);

      if((rc = nfs_rmdir(p_thr_info, &subdir_hdl, file, output)))
        return rc;
    }

  if(flag_v)
    fprintf(output, "%s successfully removed.\n", glob_path_object);

  return 0;

}

/** setattr */
int fn_nfs_setattr(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output /* IN : output stream          */ )
{

  static char format[] = "hv";

  const char help_setattr[] =
      "usage: setattr [-h][-v] <path> <attr>=<value>,<attr>=<value>,...\n"
      "       where <attr> can be :\n"
      "          mode(octal value),\n"
      "          uid, gid, (unsigned 32 bits integer)\n"
      "          size, (unsigned  64 bits integer)\n"
      "          atime, mtime (format: YYYYMMDDHHMMSS.nnnnnnnnn)\n";

  char glob_path[NFS2_MAXPATHLEN];      /* absolute path of the object */

  shell_fh3_t obj_hdl;          /* handle of the object    */
  sattr3 set_attrs;             /* attributes to be setted */
  char *attr_string;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char file[NFS2_MAXPATHLEN];   /* the relative path to the object */

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "setattr: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "setattr: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "setattr: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_setattr);
      return 0;
    }

  /* Exactly 2 args expected */

  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      strcpy(file, argv[Optind]);
      attr_string = argv[Optind + 1];
    }

  if(err_flag)
    {
      fprintf(output, help_setattr);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieve handle to the file whose attributes are to be changed */
  if((rc =
     nfs_solvepath(p_thr_info, glob_path, NFS2_MAXPATHLEN, file,
                   &p_thr_info->current_path_hdl, &obj_hdl, output)))
    return rc;

  /* Convert the peer (attr_name,attr_val) to an sattr3 structure. */
  if(cmdnfs_sattr3(CMDNFS_ENCODE,
                   1, &attr_string, 0, NULL, (caddr_t) & set_attrs) == FALSE)
    {
      fprintf(output, "Invalid nfs arguments.\n");
      fprintf(output, help_setattr);
      return -1;
    }

  /* executes set attrs */
  if((rc = nfs_setattr(p_thr_info, &obj_hdl, &set_attrs, output)))
    return rc;

  if(flag_v)
    fprintf(output, "Attributes of \"%s\" successfully changed.\n", glob_path);

  return 0;
}                               /* fn_nfs_setattr */

/** proceed a rename command. */
int fn_nfs_rename(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  static char format[] = "hv";

  const char help_rename[] = "usage: rename [-h][-v] <src> <dest>\n";

  char src_glob_path[NFS2_MAXPATHLEN];
  char tgt_glob_path[NFS2_MAXPATHLEN];

  shell_fh3_t src_path_handle, tgt_path_handle;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char tmp_path1[NFS2_MAXPATHLEN];
  char tmp_path2[NFS2_MAXPATHLEN];
  char *src_path;
  char *src_file;
  char *tgt_path;
  char *tgt_file;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "rename: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "rename: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "rename: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_rename);
      return 0;
    }

  /* Exactly 2 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {

      strncpy(tmp_path1, argv[Optind], NFS2_MAXPATHLEN);
      split_path(tmp_path1, &src_path, &src_file);

      strncpy(tmp_path2, argv[Optind + 1], NFS2_MAXPATHLEN);
      split_path(tmp_path2, &tgt_path, &tgt_file);

    }

  if(err_flag)
    {
      fprintf(output, help_rename);
      return -1;
    }

  if(flag_v)
    fprintf(output, "Renaming %s (dir %s) to %s (dir %s)\n",
            src_file, src_path, tgt_file, tgt_path);

  /* copy current path. */
  strncpy(src_glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);
  strncpy(tgt_glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves paths handles */
  if((rc =
     nfs_solvepath(p_thr_info, src_glob_path, NFS2_MAXPATHLEN,
                   src_path, &p_thr_info->current_path_hdl, &src_path_handle, output)))
    return rc;

  if((rc =
     nfs_solvepath(p_thr_info, tgt_glob_path, NFS2_MAXPATHLEN,
                   tgt_path, &p_thr_info->current_path_hdl, &tgt_path_handle, output)))
    return rc;

  /* Rename operation */

  if((rc = nfs_rename(p_thr_info, &src_path_handle,      /* IN */
                     src_file,  /* IN */
                     &tgt_path_handle,  /* IN */
                     tgt_file,  /* IN */
                      output)))
    return rc;

  if(flag_v)
    fprintf(output, "%s/%s successfully renamed to %s/%s\n",
            src_glob_path, src_file, tgt_glob_path, tgt_file);

  return 0;

}

/** proceed a hardlink command. */
int fn_nfs_hardlink(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  static char format[] = "hv";

  const char help_hardlink[] =
      "hardlink: create a hard link.\n"
      "usage: hardlink [-h][-v] <target> <new_path>\n"
      "       target: path of an existing file.\n"
      "       new_path: path of the hardlink to be created\n";

  char glob_path_target[NFS2_MAXPATHLEN];
  char glob_path_link[NFS2_MAXPATHLEN];

  shell_fh3_t target_hdl, dir_hdl;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *target = NULL;

  char tmp_path[NFS2_MAXPATHLEN];
  char *path;
  char *name;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "hardlink: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "hardlink: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "hardlink: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_hardlink);
      return 0;
    }

  /* 2 args expected */

  if(Optind == (argc - 2))
    {

      target = argv[Optind];

      strncpy(tmp_path, argv[Optind + 1], NFS2_MAXPATHLEN);
      split_path(tmp_path, &path, &name);

    }
  else
    {
      err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_hardlink);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path_target, p_thr_info->current_path, NFS2_MAXPATHLEN);
  strncpy(glob_path_link, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle for target */
  if((rc =
     nfs_solvepath(p_thr_info, glob_path_target, NFS2_MAXPATHLEN,
                   target, &p_thr_info->current_path_hdl, &target_hdl, output)))
    return rc;

  /* retrieves path handle for parent dir */
  if((rc =
     nfs_solvepath(p_thr_info, glob_path_link, NFS2_MAXPATHLEN,
                   path, &p_thr_info->current_path_hdl, &dir_hdl, output)))
    return rc;

  rc = nfs_link(p_thr_info, &target_hdl,        /* IN - target file */
                &dir_hdl,       /* IN - parent dir handle */
                name,           /* IN - link name */
                output);        /* OUT - new attributes */

  if(rc)
    return rc;

  if(flag_v)
    fprintf(output, "%s/%s <=> %s successfully created\n", path, name, glob_path_target);

  return 0;

}

/** proceed an ln command. */

int fn_nfs_ln(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output     /* IN : output stream          */
    )
{

  static char format[] = "hv";

  const char help_ln[] =
      "ln: create a symbolic link.\n"
      "usage: ln [-h][-v] <link_content> <link_path>\n"
      "       link_content: content of the symbolic link to be created\n"
      "       link_path: path of the symbolic link to be created\n";

  char glob_path[NFS2_MAXPATHLEN];
  shell_fh3_t path_hdl, link_hdl;
  sattr3 set_attrs;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *content = NULL;
  char tmp_path[NFS2_MAXPATHLEN];
  char *path;
  char *name;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "ln: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "ln: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "ln: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_ln);
      return 0;
    }

  /* 2 args expected */

  if(Optind == (argc - 2))
    {

      content = argv[Optind];

      strncpy(tmp_path, argv[Optind + 1], NFS2_MAXPATHLEN);
      split_path(tmp_path, &path, &name);

    }
  else
    {
      err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_ln);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc =
     nfs_solvepath(p_thr_info, glob_path, NFS2_MAXPATHLEN,
                   path, &p_thr_info->current_path_hdl, &path_hdl, output)))
    return rc;

  /* Prepare link attributes : empty sattr3 list */

  if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL, (caddr_t) & set_attrs) == FALSE)
    {
      /* invalid handle */
      fprintf(output, "\tError encoding nfs arguments.\n");
      return -1;
    }

  rc = nfs_symlink(p_thr_info, path_hdl,        /* IN - parent dir handle */
                   name,        /* IN - link name */
                   content,     /* IN - link content */
                   &set_attrs,  /* Link attributes */
                   &link_hdl,   /* OUT - link handle */
                   output);

  if(rc)
    return rc;

  if(flag_v)
    {
      char buff[2 * NFS3_FHSIZE + 1];
      snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) link_hdl.data_val,
                 link_hdl.data_len);

      fprintf(output, "%s/%s -> %s successfully created (@%s) \n", path, name, content,
              buff);
    }

  return 0;
}

/** proceed an ls command using NFS protocol NFS */
int fn_nfs_stat(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output)  /* IN : output stream          */
{

  shell_fh3_t handle_tmp;
  fattr3 attrs;

  int rc = 0;
  char glob_path[NFS2_MAXPATHLEN];

  static char format[] = "hvHz";
  const char help_stat[] = "usage: stat [options] <path>\n"
      "options :\n"
      "\t-h print this help\n"
      "\t-v verbose mode\n"
      "\t-H print the NFS handle\n" "\t-z silent mode (print nothing)\n";

  int option;
  char *str_name = NULL;
  int flag_v = 0;
  int flag_h = 0;
  int flag_H = 0;
  int flag_z = 0;
  int err_flag = 0;

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  /* check if a path has been mounted */

  if(p_thr_info->is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  /* analysing options */
  getopt_init();

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "stat: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "stat: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case 'z':
          if(flag_z)
            fprintf(output,
                    "stat: warning: option 'z' has been specified more than once.\n");
          else
            flag_z++;
          break;

        case 'H':
          if(flag_H)
            fprintf(output,
                    "stat: warning: option 'H' has been specified more than once.\n");
          else
            flag_H++;
          break;

        case '?':
          fprintf(output, "stat: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }                           /* while */

  if(flag_z + flag_v > 1)
    {
      fprintf(output, "stat: can't use -z and -v at the same time\n");
      err_flag++;
    }

  if(flag_h)
    {
      fprintf(output, help_stat);
      return 0;
    }

  if(Optind != argc - 1)
    {
      fprintf(output, "stat: Missing argument: <path>\n");
      err_flag++;
    }
  else
    {
      str_name = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_stat);
      return -1;
    }

  /* copy current global path */
  strncpy(glob_path, p_thr_info->current_path, NFS2_MAXPATHLEN);

  /* retrieving handle */
  if((rc = nfs_solvepath(p_thr_info,
                        glob_path,
                        NFS2_MAXPATHLEN,
                         str_name, &p_thr_info->current_path_hdl, &handle_tmp, output)))
    return rc;

  if(flag_v)
    fprintf(output, "proceeding stat (using NFS protocol) on \"%s\"\n", glob_path);

  if((rc = nfs_getattr(p_thr_info, &handle_tmp, &attrs, output)))
    return rc;

  if(flag_H)
    {
      if(!flag_z)
        {
          char buff[2 * NFS3_FHSIZE + 1];

          snprintmem(buff, 2 * NFS3_FHSIZE + 1, (caddr_t) handle_tmp.data_val,
                     handle_tmp.data_len);
          fprintf(output, "%s (@%s)\n", str_name, buff);
        }
    }
  else if(!flag_z)
    {
      /*fprintf(output,"%s :\n",str_name); */
      print_nfs_attributes(&attrs, output);
    }

  return 0;
}                               /* fn_nfs_stat */

/** change thread credentials. */
int fn_nfs_su(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output)    /* IN : output stream          */
{
  int rc, i;
  char *str_uid;
  uid_t uid;
  fsal_status_t st;
  struct passwd *pw_struct;

#define MAX_GRPS  128
  gid_t groups_tab[MAX_GRPS];
  int nb_grp;

  const char help_su[] = "usage: su <uid>\n";

  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  /* UID arg expected */
  if(argc != 2)
    {
      fprintf(output, help_su);
      return -1;
    }
  else
    {
      str_uid = argv[1];
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }

  if(isdigit(str_uid[0]))
    {
      if((uid = my_atoi(str_uid)) == (uid_t) - 1)
        {
          fprintf(output, "Error: invalid uid \"%s\"\n", str_uid);
          return -1;
        }
      pw_struct = getpwuid(uid);
    }
  else
    {
      pw_struct = getpwnam(str_uid);
    }

  if(pw_struct == NULL)
    {
      fprintf(output, "Unknown user %s\n", str_uid);
      return errno;
    }

  nb_grp = getugroups(MAX_GRPS, groups_tab, pw_struct->pw_name, pw_struct->pw_gid);

  fprintf(output, "Changing user to : %s ( uid = %d, gid = %d )\n",
          pw_struct->pw_name, pw_struct->pw_uid, pw_struct->pw_gid);

  if(nb_grp > 1)
    {
      fprintf(output, "altgroups = ");
      for(i = 1; i < nb_grp; i++)
        {
          if(i == 1)
            fprintf(output, "%d", groups_tab[i]);
          else
            fprintf(output, ", %d", groups_tab[i]);
        }
      fprintf(output, "\n");
    }

  st = FSAL_GetClientContext(&p_thr_info->context, &p_thr_info->exp_context,
                             pw_struct->pw_uid, pw_struct->pw_gid, groups_tab, nb_grp);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_GetUserCred:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  fprintf(output, "Done.\n");

  return 0;

}

int fn_nfs_id(int argc,         /* IN : number of args in argv */
              char **argv,      /* IN : arg list               */
              FILE * output)    /* IN : output stream          */
{
  int rc;
  cmdnfs_thr_info_t *p_thr_info = NULL;

  if(is_nfs_layer_initialized != TRUE)
    {
      fprintf(output, "\tNFS layer not initialized.\n");
      return -1;
    }

  p_thr_info = GetNFSClient();

  if(p_thr_info->is_thread_init != TRUE)
    {
      if((rc = InitNFSClient(p_thr_info)))
        {
          fprintf(output, "\t%s: Error %d during thread initialization.\n", argv[0], rc);
          return -1;
        }
    }
#ifdef _USE_POSIX
  {
    posixfsal_op_context_t * p_cred
      = (posixfsal_op_context_t *)&p_thr_info->context;
    fprintf(output, "Current user : uid = %d, gid = %d\n",
          p_cred->credential.user, p_cred->credential.group);
  }
#endif

  return 0;
}
