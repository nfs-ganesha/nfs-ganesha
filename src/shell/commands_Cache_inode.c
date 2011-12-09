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
 * \file    commands.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/17 13:37:44 $
 * \version $Revision: 1.74 $
 * \brief   Converts user's commands to Cache_inode commands.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "cache_inode.h"
#include "cache_content.h"
#include "LRU_List.h"
#include "err_fsal.h"
#include "err_cache_inode.h"
#include "err_cache_content.h"
#include "stuff_alloc.h"
#include "cmd_tools.h"
#include "commands.h"
#include "Getopt.h"

#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <pwd.h>
#include <ctype.h>

#define EXPORT_ID 1
#define ATTR_LEN  100

static char localmachine[256];
#ifdef OLD_LOGGING
static desc_log_stream_t voie_cache;
static log_t log_desc_cache = LOG_INITIALIZER;
#endif

/** Global variable: root pentry */
static cache_entry_t *pentry_root;

/** Global (exported) variable : The cache hash table */
hash_table_t *ht;

static int cache_init = FALSE;

/** Global variable : the garbagge policy to be used */
static cache_inode_gc_policy_t gcpol;

/** Global variable : the cache policy to be used */
//static cache_inode_policy_t cachepol = CACHE_INODE_POLICY_FULL_WRITE_THROUGH;
static cache_inode_policy_t cachepol = CACHE_INODE_POLICY_ATTRS_ONLY_WRITE_THROUGH ;

/** Global (exported) variable : init parameters for clients. */
cache_inode_client_parameter_t cache_client_param;
cache_content_client_parameter_t datacache_client_param;

typedef struct cmdCacheInode_thr_info__
{

  int is_thread_init;

  /* export context : on for each thread,
   * on order to make it possible for them
   * to access different filesets.
   */
  fsal_export_context_t exp_context;

  /** context for accessing the filesystem */
  fsal_op_context_t context;

  /** Thread specific variable: Last status from cache_inode layer */
  cache_inode_status_t cache_status;

  int is_client_init;

  /** Thread specific variable: current pentry */
  cache_entry_t *pentry;

  /** Thread specific variable : Current path */
  char current_path[FSAL_MAX_PATH_LEN]; /* current path */

  /** Thread specific variable : the client for the cache */
  cache_inode_client_t client;
  cache_content_client_t dc_client;

} cmdCacheInode_thr_info_t;

/* pthread key to manage thread specific configuration */

static pthread_key_t thread_key;
static pthread_once_t once_key = PTHREAD_ONCE_INIT;

/* init pthtread_key for current thread */

static void init_keys(void)
{
  if(pthread_key_create(&thread_key, NULL) == -1)
    printf("Error %d creating pthread key for thread %p : %s\n",
           errno, (caddr_t) pthread_self(), strerror(errno));

  return;
}                               /* init_keys */

/**
 * GetFSALCmdContext :
 * manages pthread_keys.
 */
static cmdCacheInode_thr_info_t *GetCacheInodeContext()
{
  cmdCacheInode_thr_info_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      printf("Error %d calling pthread_once for thread %p : %s\n",
             errno, (caddr_t) pthread_self(), strerror(errno));
      return NULL;
    }

  p_current_thread_vars = (cmdCacheInode_thr_info_t *) pthread_getspecific(thread_key);

  /* we allocate the thread context if this is the first time */
  if(p_current_thread_vars == NULL)
    {

      /* allocates thread structure */
      p_current_thread_vars =
          (cmdCacheInode_thr_info_t *) Mem_Alloc(sizeof(cmdCacheInode_thr_info_t));

      /* panic !!! */
      if(p_current_thread_vars == NULL)
        {
          printf("%p:commands_Cache_inode: Not enough memory\n",
                 (caddr_t) pthread_self());
          return NULL;
        }

      /* Clean thread context */

      memset(p_current_thread_vars, 0, sizeof(cmdCacheInode_thr_info_t));

      p_current_thread_vars->is_thread_init = FALSE;
      p_current_thread_vars->is_client_init = FALSE;
      strcpy(p_current_thread_vars->current_path, "");
      p_current_thread_vars->pentry = NULL;
      p_current_thread_vars->cache_status = CACHE_INODE_SUCCESS;

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);

    }

  return p_current_thread_vars;

}                               /* GetCacheInodeContext */

static int InitThread(cmdCacheInode_thr_info_t * thr_info)
{

  uid_t uid;
  fsal_status_t st;
  struct passwd *pw_struct;

  /* for the moment, create export context for root fileset */
  st = FSAL_BuildExportContext(&thr_info->exp_context, NULL, NULL);

  /* initialize FSAL credential for this thread */

  st = FSAL_InitClientContext(&thr_info->context);

  if(FSAL_IS_ERROR(st))
    {
      printf
          ("%p:commands_Cache_inode: Error %d initializing context for thread (FSAL_InitThreadCred)\n",
           (caddr_t) pthread_self(), st.major);
      return 1;
    }

  uid = getuid();

  pw_struct = getpwuid(uid);

  if(pw_struct == NULL)
    {
      printf("commands_Cache_inode: Unknown user %u\n", uid);
      return 1;
    }

  st = FSAL_GetClientContext(&thr_info->context, &thr_info->exp_context,
                             uid, pw_struct->pw_gid, NULL, 0);

  if(FSAL_IS_ERROR(st))
    {
      printf
          ("%p:commands_Cache_inode: Error %d getting contexte for uid %d (FSAL_GetUserCred)\n",
           (caddr_t) pthread_self(), st.major, uid);
      return 1;
    }

  thr_info->is_thread_init = TRUE;

  return 0;

}

static int InitClient(cmdCacheInode_thr_info_t * thr_info)
{
  thr_info->pentry = pentry_root;

  strcpy(thr_info->current_path, "/");

  /* Init the cache_inode client */
  if(cache_inode_client_init(&thr_info->client, cache_client_param, 0, NULL) != 0)
    return 1;

  /* Init the cache content client */
  if(cache_content_client_init(&thr_info->dc_client, datacache_client_param, "") != 0)
    return 1;

  thr_info->client.pcontent_client = (caddr_t) & thr_info->dc_client;

  thr_info->is_client_init = TRUE;

  return 0;

}

cmdCacheInode_thr_info_t *RetrieveInitializedContext()
{

  cmdCacheInode_thr_info_t *context;

  context = GetCacheInodeContext();

  if(context->is_thread_init != TRUE)
    if(InitThread(context))
      {
        printf("Error occured during thread initialization.\n");
        return NULL;
      }

  if(context->is_client_init != TRUE)
    if(InitClient(context))
      {
        printf("Error occured during client initialization.\n");
        return NULL;
      }

  return context;

}

void Cache_inode_layer_SetLogLevel(int log_lvl)
{
#ifdef OLD_LOGGING
  log_stream_t *curr;

  /* mutex pour proteger le descriptor de log */
  pthread_mutex_lock(&mutex_log);

  /* first time */
  if(log_level == -1)
    {
      log_level = log_lvl;
      voie_cache.fd = fileno(stderr);
      AddLogStreamJd(&log_desc_cache, V_FD, voie_cache, log_level, SUP);
    }
  else
    {
      log_level = log_lvl;
      /* changing log level */
      curr = log_desc_cache.liste_voies;
      while(curr)
        {
          curr->niveau = log_level;
          curr = curr->suivante;
        }
    }

  /* mutex pour proteger le descriptor de log */
  pthread_mutex_unlock(&mutex_log);
#endif
}

int lru_entry_to_str(LRU_data_t data, char *str)
{
  return sprintf(str, "%p (len=%llu)", data.pdata, (unsigned long long)data.len);
}                               /* lru_entry_to_str */

int lru_clean_entry(LRU_entry_t * entry, void *adddata)
{
  return 0;
}                               /* lru_clean_entry */

static void getopt_init()
{
  /* disables getopt error message */
  Opterr = 0;

  /* reinits getopt processing */
  Optind = 1;

}

/* solves a relative or aboslute path */
int cache_solvepath(char *io_global_path, int size_global_path, /* global path */
                    char *i_spec_path,  /* specified path */
                    cache_entry_t * current_pentry,     /* current directory handle */
                    cache_entry_t ** pnew_pentry, FILE * output)
{
  char str_path[FSAL_MAX_PATH_LEN];

  fsal_name_t name;
  fsal_status_t st;
  char tmp_path[FSAL_MAX_PATH_LEN];
  char *next_name;
  char *curr;
  int last = 0;

  cache_inode_fsal_data_t fsdata;

  cache_entry_t *pentry_lookup = NULL;
  cache_entry_t *pentry_tmp = NULL;
  fsal_attrib_list_t attrlookup;

  cmdCacheInode_thr_info_t *context;
  context = RetrieveInitializedContext();

  /* is it a relative or an absolute path ? */
  strncpy(str_path, i_spec_path, FSAL_MAX_PATH_LEN);
  str_path[FSAL_MAX_PATH_LEN - 1] = '\0';

  curr = str_path;
  next_name = str_path;

  if(str_path[0] == '@')
    {
      /* It is a file handle */
      int rc;

      rc = sscanHandle(&(fsdata.handle), str_path + 1);

      if(rc <= 0)
        {
          fprintf(output, "Invalid FileHandle: %s\n", str_path);
          return -1;
        }

      if(str_path[rc + 1] != '\0')
        {
          fprintf(output, "Invalid FileHandle: %s\n", str_path);
          return -1;
        }

      /* Get the corresponding pentry */
      fsdata.cookie = 0;
      if((pentry_tmp = cache_inode_get(&fsdata,
                                       cachepol,
                                       &attrlookup,
                                       ht,
                                       &context->client,
                                       &context->context,
                                       &context->cache_status)) == NULL)
        {
          log_fprintf(output, "Error executing cache_inode_get( \"%s\" ) : %J%r\n",
                      str_path, ERR_CACHE_INODE, context->cache_status);

          return context->cache_status;
        }

      strncpy(io_global_path, str_path, size_global_path);
      io_global_path[size_global_path - 1] = '\0';
      *pnew_pentry = pentry_tmp;

      return 0;

    }
  else if(str_path[0] == '/')
    {
      /* absolute path, starting from "/", with a relative path */
      curr++;
      next_name++;
      pentry_lookup = pentry_root;
      strncpy(tmp_path, "/", FSAL_MAX_PATH_LEN);

      /* the the directory  is /, return */
      if(str_path[1] == '\0')
        {
          strncpy(io_global_path, tmp_path, size_global_path);
          *pnew_pentry = pentry_lookup;
          return 0;
        }

    }
  else
    {
      pentry_lookup = current_pentry;
      strncpy(tmp_path, io_global_path, FSAL_MAX_PATH_LEN);
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

      /* build the name */
      if(FSAL_IS_ERROR(st = FSAL_str2name(next_name, FSAL_MAX_PATH_LEN, &name)))
        {
          fprintf(output, "Error executing FSAL_str2name:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;
        }

      /* lookup this name */

      if((pentry_tmp = cache_inode_lookup(pentry_lookup,
                                          &name,
                                          cachepol,
                                          &attrlookup,
                                          ht,
                                          &context->client,
                                          &context->context,
                                          &context->cache_status)) == NULL)
        {
          log_fprintf(output,
                      "Error executing cache_inode_lookup( \"%s\", \"%s\" ) : %J%r\n",
                      tmp_path, name.name, ERR_CACHE_INODE, context->cache_status);

          return context->cache_status;
        }

      /* updates current handle */
      pentry_lookup = pentry_tmp;

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

  *pnew_pentry = pentry_lookup;
  return 0;

}

int cacheinode_init(char *filename, int flag_v, FILE * output)
{
  int rc;

  fsal_status_t status;
  fsal_path_t __attribute__ ((__unused__)) pathroot;
  fsal_handle_t root_handle;
  fsal_attrib_list_t attrs;

  cache_inode_fsal_data_t fsdata;
  cache_inode_parameter_t cache_param;
  config_file_t config_file;

  cmdCacheInode_thr_info_t *context;

  /* geting the hostname */
  if(gethostname(localmachine, sizeof(localmachine)) != 0)
    {
      fprintf(stderr, "Error in gethostname is %s", strerror(errno));
      exit(1);
    }
  else
    SetNameHost(localmachine);

  /* Parse config file */
  if((config_file = config_ParseFile(filename)) == NULL)
    {
      fprintf(output, "init_cache: Error parsing %s: %s\n", filename,
              config_GetErrorMsg());
      return -1;
    }

  /* creating log */
  AddFamilyError(ERR_CACHE_INODE, "Cache_inode related Errors",
                 tab_errstatus_cache_inode);

  /* creates thread context */

  context = GetCacheInodeContext();

  if(context->is_thread_init != TRUE)
    if(InitThread(context))
      {
        fprintf(output, "Error ossured during thread initialization.\n");
        return 1;
      }

  /* Reading the hash parameter */
  rc = cache_inode_read_conf_hash_parameter(config_file, &cache_param);
  if(rc != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_read_conf_hash_parameter : %J%r\n",
                  ERR_CACHE_INODE, rc);

      return 1;
    }

  cache_param.hparam.hash_func_key = cache_inode_fsal_hash_func;
  cache_param.hparam.hash_func_rbt = cache_inode_fsal_rbt_func;
  cache_param.hparam.hash_func_both = NULL ; /* BUGAZOMEU */
  cache_param.hparam.compare_key = cache_inode_compare_key_fsal;
  cache_param.hparam.key_to_str = NULL;
  cache_param.hparam.val_to_str = NULL;

  if(flag_v)
    cache_inode_print_conf_hash_parameter(output, cache_param);

  if((ht = cache_inode_init(cache_param, &context->cache_status)) == NULL)
    {
      fprintf(output, "Error %d while init hash\n ", context->cache_status);
      return 1;
    }
  else if(flag_v)
    fprintf(output, "\tHash Table address = %p\n", ht);

  /* Get the gc policy */
  rc = cache_inode_read_conf_gc_policy(config_file, &gcpol);
  if(rc != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_read_conf_gc_policy : %J%r\n",
                  ERR_CACHE_INODE, rc);
      return 1;
    }

  if(flag_v)
    cache_inode_print_conf_gc_policy(output, gcpol);

  /* retrieve lower layer info */

  /* Getting the root of the FS */
#if defined( _USE_PROXY )
  /*if( FSAL_IS_ERROR( status = FSAL_str2path( "/users/thomas/./", FSAL_MAX_PATH_LEN, &pathroot ) ) ) */
  if(FSAL_IS_ERROR(status = FSAL_str2path("/", FSAL_MAX_PATH_LEN, &pathroot)))
    {
      char buffer[LOG_MAX_STRLEN];

      MakeLogError(buffer, ERR_FSAL, status.major, status.minor, __LINE__);
      fprintf(output, "%s\n", buffer);
      return 1;
    }

  if(FSAL_IS_ERROR
     (status = FSAL_lookupPath(&pathroot, &context->context, &root_handle, NULL)))
    {
      char buffer[LOG_MAX_STRLEN];

      MakeLogError(buffer, ERR_FSAL, status.major, status.minor, __LINE__);
      fprintf(output, "%s\n", buffer);
      return 1;
    }
#elif defined( _USE_VFS )
if(FSAL_IS_ERROR(status = FSAL_str2path("/tmp", FSAL_MAX_PATH_LEN, &pathroot)))
    {
      char buffer[LOG_MAX_STRLEN];

      MakeLogError(buffer, ERR_FSAL, status.major, status.minor, __LINE__);
      fprintf(output, "%s\n", buffer);
      return 1;
    }

  if(FSAL_IS_ERROR
     (status = FSAL_lookupPath(&pathroot, &context->context, &root_handle, NULL)))
    {
      char buffer[LOG_MAX_STRLEN];

      MakeLogError(buffer, ERR_FSAL, status.major, status.minor, __LINE__);
      fprintf(output, "%s\n", buffer);
      return 1;
    }
#else
  if(FSAL_IS_ERROR
     (status = FSAL_lookup(NULL, NULL, &context->context, &root_handle, NULL)))
    {
      char buffer[LOG_MAX_STRLEN];

      MakeLogError(buffer, ERR_FSAL, status.major, status.minor, __LINE__);
      fprintf(output, "%s\n", buffer);
      return 1;
    }
#endif

  /* retrieve supported attributes */

  FSAL_CLEAR_MASK(attrs.asked_attributes);
  FSAL_SET_MASK(attrs.asked_attributes, FSAL_ATTR_SUPPATTR);
  if(FSAL_IS_ERROR(status = FSAL_getattrs(&root_handle, &context->context, &attrs)))
    {
      fprintf(output, "Error executing FSAL_getattrs:");
      print_fsal_status(output, status);
      fprintf(output, "\n");
      return status.major;
    }

#ifdef OLD_LOGGING
  cache_client_param.log_outputs = log_desc_cache;
#endif
  cache_client_param.attrmask = attrs.supported_attributes;

  cache_client_param.lru_param.entry_to_str = lru_entry_to_str;
  cache_client_param.lru_param.clean_entry = lru_clean_entry;

  /* We need a cache_client to acces the cache */
  rc = cache_inode_read_conf_client_parameter(config_file, &cache_client_param);
  if(rc != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output,
                  "Error executing cache_inode_read_conf_client_parameter : %J%r\n",
                  ERR_CACHE_INODE, rc);
      return 1;
    }

  /* We need a cache_client to acces the datacache */
  rc = cache_content_read_conf_client_parameter(config_file, &datacache_client_param);
  if(rc != CACHE_CONTENT_SUCCESS)
    {
      log_fprintf(output,
                  "Error executing cache_content_read_conf_client_parameter : %J%r\n",
                  ERR_CACHE_INODE, rc);
      return 1;
    }
#ifdef OLD_LOGGING
  datacache_client_param.log_outputs = log_desc_cache;
#endif
/*
  DEPRECATED :
  datacache_client_param.lru_param.entry_to_str = lru_entry_to_str ;
  datacache_client_param.lru_param.clean_entry = lru_clean_entry ;
*/
  if(flag_v)
    {

      cache_inode_print_conf_client_parameter(output, cache_client_param);
      cache_content_print_conf_client_parameter(output, datacache_client_param);
    }

  /* Reading the datacache core parameter */
  if(rc != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output,
                  "Error executing cache_content_read_conf_core_parameter : %J%r\n",
                  ERR_CACHE_INODE, rc);
      return 1;
    }

  /* Init the cache_inode client */
  if(cache_inode_client_init(&context->client, cache_client_param, 0, NULL) != 0)
    return 1;

#ifdef _USE_ASYNC_CACHE_INODE
  /* Start the TAD and synclets for writeback cache inode */
  cache_inode_async_init(cache_client_param);

  if(cache_inode_async_precreate_object
     (&context->client, DIRECTORY, &context->exp_context) == -1)
    {
      fprintf(stderr, "NFS INIT: /!\\ Impossible to pre-create asynchronous direcory pool");
      exit(1);
    }

  if(cache_inode_async_precreate_object
     (&context->client, REGULAR_FILE, &context->exp_context) == -1)
    {
      fprintf(stderr, "NFS INIT: /!\\ Impossible to pre-create asynchronous file pool");
      exit(1);
    }
#endif

  /* Init the cache content client */
  if(cache_content_client_init(&context->dc_client, datacache_client_param, "") != 0)
    return 1;

  context->client.pcontent_client = (caddr_t) & context->dc_client;

  fsdata.cookie = 0;
  fsdata.handle = root_handle;

  if((context->pentry = cache_inode_make_root(&fsdata,
                                              cachepol,
                                              ht, 
                                              &context->client,
                                              &context->context,
                                              &context->cache_status)) == NULL)
    {
      fprintf(output, "Error: can't init fs's root");
      return 1;
    }

  if(cache_content_init_dir(datacache_client_param, EXPORT_ID) != 0)
    {
      fprintf(output, "Error: can't init datacache directory");
      return 1;
    }

  strcpy(context->current_path, "/");

  context->is_client_init = TRUE;

  pentry_root = context->pentry;

  if(flag_v)
    fprintf(output, "\tCache_inode successfully initialized.\n");

  cache_init = TRUE;

  /* Free config struct */
  config_Free(config_file);

  return 0;
}

/** proceed an init_fs command. */
int fn_Cache_inode_cache_init(int argc, /* IN : number of args in argv */
                              char **argv,      /* IN : arg list               */
                              FILE * output)    /* IN : output stream          */
{
  int rc;

  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int option;
  char *filename = NULL;

  char format[] = "hv";

  const char help_init[] =
      "usage: init_cache [options] <ganesha_config_file>\n"
      "options :\n" "\t-h print this help\n" "\t-v verbose mode\n";

  if(ht != NULL)
    {
      fprintf(output, "\tCache_inode is already initialized\n");
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
                    "init_cache: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "init_cache: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case '?':
          fprintf(output, "init_fs: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }                       /* switch */
    }                           /* while */

  if(flag_h)
    {
      fprintf(output, help_init);
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
      fprintf(output, help_init);
      return -1;
    }

  rc = cacheinode_init(filename, flag_v, output);

  return rc;
}

/** prints current path */
int fn_Cache_inode_pwd(int argc,        /* IN : number of args in argv */
                       char **argv,     /* IN : arg list               */
                       FILE * output)   /* IN : output stream          */
{
  fsal_handle_t *pfsal_handle = NULL;
  char buff[128];

  cmdCacheInode_thr_info_t *context;
  context = RetrieveInitializedContext();

  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  if((pfsal_handle =
      cache_inode_get_fsal_handle(context->pentry, &context->cache_status)) == NULL)
    {
      return 1;
    }

  fprintf(output, "Current directory is \"%s\" \n", context->current_path);
  snprintmem(buff, 128, (caddr_t) pfsal_handle, sizeof(fsal_handle_t));
  fprintf(output, "Current File handle is \"@%s\" \n", buff);

  return 0;

}

/** change current path */
int fn_Cache_inode_cd(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output)    /* IN : output stream          */
{
  char glob_path[FSAL_MAX_PATH_LEN];

  cache_entry_t *new_pentry;
  int rc;

  cmdCacheInode_thr_info_t *context;

  const char help_cd[] = "usage: cd <path>\n";

  if(!cache_init)
    {
      fprintf(output, "\tCache is not initialized\n");
      return -1;
    }

  /* Exactly one arg expected */
  if(argc != 2)
    {
      fprintf(output, help_cd);
      return -1;
    }

  context = RetrieveInitializedContext();

  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  if((rc =
      cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, argv[1], context->pentry,
                      &new_pentry, output)) != 0)
    return rc;

  if(new_pentry->internal_md.type != DIRECTORY)
    {
      fprintf(output, "Error: %s is not a directory\n", glob_path);
      return ENOTDIR;
    }

  if((context->cache_status = cache_inode_access(new_pentry,
                                                 FSAL_X_OK,
                                                 ht,
                                                 &context->client,
                                                 &context->context,
                                                 &context->cache_status)) !=
     CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_access : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  /* if so, apply changes */
  strncpy(context->current_path, glob_path, FSAL_MAX_PATH_LEN);
  context->pentry = new_pentry;

  fprintf(output, "Current directory is \"%s\"\n", context->current_path);

  return 0;
}

/** proceed a stat command. */
int fn_Cache_inode_stat(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output)  /* IN : output stream          */
{
  char format[] = "hv";

  const char help_stat[] = "usage: stat [-h][-v] <file>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *pentry_stat = NULL;
  fsal_attrib_list_t attrs;

  cmdCacheInode_thr_info_t *context;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  char *file = NULL;

  if(!cache_init)
    {
      fprintf(output, "\tCache_inode is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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
        case '?':
          fprintf(output, "stat: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  /* Exactly one arg expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_stat);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves object handle */
  if((rc = cache_solvepath(glob_path,
                           FSAL_MAX_PATH_LEN, file, context->pentry, &pentry_stat, output)))
    return rc;

  /* Get the attributes */
  if(cache_inode_getattr(pentry_stat,
                         &attrs,
                         ht,
                         &context->client,
                         &context->context,
                         &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_getattr( \"%s\" ) : %J%r\n",
                  file, ERR_CACHE_INODE, context->cache_status);

      return context->cache_status;
    }

  /* print file attributes */
  print_fsal_attributes(attrs, output);

  return 0;
}

/** proceed to a call to the garbagge collector. */
int fn_Cache_inode_gc(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output)    /* IN : output stream          */
{
  char format[] = "hv";

  const char help_gc[] = "usage: gc \n"
      "options :\n"
      "\t-h print this help\n"
      "   The gc policy used is defined in the configuration file\n";

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

        case '?':
          fprintf(output, "ls: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }                           /* while */

  if(flag_h)
    {
      fprintf(output, help_gc);
      return 0;
    }

  if(err_flag)
    {
      fprintf(output, help_gc);
      return -1;
    }

  cache_inode_set_gc_policy(gcpol);

  if(cache_inode_gc(ht, &context->client, &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_gc : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  return 0;
}                               /* fn_Cache_inode_gc */

/** proceed an ls command. */
int fn_Cache_inode_ls(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output)    /* IN : output stream          */
{
#define CACHE_INODE_SHELL_READDIR_SIZE 10
  uint64_t begin_cookie = 0;
  uint64_t end_cookie = 0;
  unsigned int nbfound;
  cache_inode_dir_entry_t * dirent_array[CACHE_INODE_SHELL_READDIR_SIZE] ;
  cache_inode_endofdir_t eod_met;
  unsigned int i;
  fsal_path_t symlink_path;
  fsal_attrib_list_t attrs;
  char *str_name = ".";
  char item_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *pentry_tmp = NULL;
  int dir_pentry_unlock = FALSE;

  int rc = 0;
  char glob_path[FSAL_MAX_PATH_LEN];
  fsal_handle_t *pfsal_handle = NULL;

  char format[] = "hvdlLSHz";
  const char help_ls[] = "usage: ls [options]\n"
      "options :\n"
      "\t-h print this help\n"
      "\t-v verbose mode\n"
      "\t-d print directory info instead of listing its content\n"
      "\t-l print standard UNIX attributes\n"
      "\t-L print the cache_inode entry addresses\n"
      "\t-S print all supported attributes\n"
      "\t-H print the fsal handle\n" "\t-z silent mode (print nothing)\n";

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int flag_d = 0;
  int flag_l = 0;
  int flag_S = 0;
  int flag_L = 0;
  int flag_H = 0;
  int flag_z = 0;
  int err_flag = 0;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

        case 'L':
          if(flag_L)
            fprintf(output,
                    "ls: warning: option 'L' has been specified more than once.\n");
          else
            flag_L++;
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

  if(flag_l + flag_S + flag_L + flag_H > 1)
    {
      fprintf(output, "ls: conflict between options l,S,L\n");
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

  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* first, retrieve the argument (if any) */
  if(Optind == argc - 1)
    {
      str_name = argv[Optind];

      /* retrieving handle */
      if((rc = cache_solvepath(glob_path,
                              FSAL_MAX_PATH_LEN,
                               str_name, context->pentry, &pentry_tmp, output)))
        return rc;

    }
  else
    {
      str_name = ".";
      pentry_tmp = context->pentry;
    }

  if(flag_v)
    fprintf(output, "proceeding ls (via Cache_inode) on \"%s\"\n", glob_path);

  /*
   * if the object is a file or a directoy with the -d option specified,
   * we only show its info and exit.
   */
  if((pentry_tmp->internal_md.type != DIRECTORY) || flag_d)
    {
      if(pentry_tmp->internal_md.type == SYMBOLIC_LINK)
        {
          if(cache_inode_readlink(pentry_tmp,
                                  &symlink_path,
                                  ht,
                                  &context->client,
                                  &context->context,
                                  &context->cache_status) != CACHE_INODE_SUCCESS)
            {
              if(!flag_z)
                log_fprintf(output, "Error executing cache_inode_readlink : %J%r\n",
                            ERR_CACHE_INODE, context->cache_status);

              return context->cache_status;
            }
        }

      if(cache_inode_getattr(pentry_tmp,
                             &attrs,
                             ht,
                             &context->client,
                             &context->context,
                             &context->cache_status) != CACHE_INODE_SUCCESS)
        {
          if(!flag_z)
            log_fprintf(output, "Error executing cache_inode_getattr : %J%r\n",
                        ERR_CACHE_INODE, context->cache_status);

          return context->cache_status;
        }

      if(flag_l)
        {
          if(!flag_z)
            print_item_line(output, &attrs, str_name, symlink_path.path);
        }
      else if(flag_S)
        {
          if(!flag_z)
            {
              fprintf(output, "%s :\n", str_name);
              print_fsal_attributes(attrs, output);
            }
        }
      else if(flag_H)
        {
          if(!flag_z)
            {
              char buff[128];

              if((pfsal_handle =
                  cache_inode_get_fsal_handle(pentry_tmp,
                                              &context->cache_status)) == NULL)
                {
                  return 1;
                }
              snprintmem(buff, 128, (caddr_t) pfsal_handle, sizeof(fsal_handle_t));
              fprintf(output, "%s (@%s)\n", str_name, buff);
            }
        }
      else if(flag_L)
        {
          if(!flag_z)
            {
              if(context->pentry->internal_md.type != REGULAR_FILE)
                fprintf(output, "%p N/A  \t\t%s\n", pentry_tmp, str_name);
              else
                {
                  if(context->pentry->object.file.pentry_content == NULL)
                    fprintf(output, "%p (not cached) \t%s\n", context->pentry, str_name);
                  else
                    fprintf(output, "%p %p \t%s\n",
                            context->pentry,
                            context->pentry->object.file.pentry_content, str_name);
                }
            }
        }
      else                      /* only prints the name */
        {
          if(!flag_z)
            fprintf(output, "%s\n", str_name);
        }

      return 0;
    }

  /* If this point is reached, then the pentry is a directory */

  begin_cookie = 0;
  eod_met = UNASSIGNED_EOD;

  while(eod_met != END_OF_DIR)
    {

      if(flag_v)
        fprintf(output, "-->cache_inode_readdir(path=%s,cookie=%"PRIu64")\n",
                glob_path, begin_cookie);

      if(cache_inode_readdir(pentry_tmp,
                             cachepol,
                             begin_cookie,
                             CACHE_INODE_SHELL_READDIR_SIZE,
                             &nbfound,
                             &end_cookie,
                             &eod_met,
                             dirent_array,
                             ht,
                             &dir_pentry_unlock,
                             &context->client,
                             &context->context,
                             &context->cache_status) != CACHE_INODE_SUCCESS)
        {
          fprintf(output, "Error %d in cache_inode_readdir\n",
                  context->cache_status);
          /* after successful cache_inode_readdir, pentry_tmp may be
           * read locked */
          if (dir_pentry_unlock)
              V_r(&pentry_tmp->lock);
          return context->cache_status;
        }

      if (dir_pentry_unlock)
        V_r(&pentry_tmp->lock);

      for(i = 0; i < nbfound; i++)
        {
          if(!strcmp(str_name, "."))
            strncpy(item_path, dirent_array[i]->name.name, FSAL_MAX_PATH_LEN);
          else if(str_name[strlen(str_name) - 1] == '/')
            snprintf(item_path, FSAL_MAX_PATH_LEN, "%s%s", str_name,
                     dirent_array[i]->name.name);
          else
            snprintf(item_path, FSAL_MAX_PATH_LEN, "%s/%s", str_name,
                     dirent_array[i]->name.name);

          if(dirent_array[i]->pentry->internal_md.type == SYMBOLIC_LINK)
            {
              if(cache_inode_readlink(dirent_array[i]->pentry,
                                      &symlink_path,
                                      ht,
                                      &context->client,
                                      &context->context,
                                      &context->cache_status) != CACHE_INODE_SUCCESS)
                {
                  log_fprintf(output, "Error executing cache_inode_readlink : %J%r\n",
                              ERR_CACHE_INODE, context->cache_status);
                  /* after successful cache_inode_readdir, pentry_tmp may be
                   * read locked */
                  return context->cache_status;
                }
            }

          if(flag_l)
            {
              if(cache_inode_getattr(dirent_array[i]->pentry,
                                     &attrs,
                                     ht,
                                     &context->client,
                                     &context->context,
                                     &context->cache_status) != CACHE_INODE_SUCCESS)
                {
                  log_fprintf(output, "Error executing cache_inode_getattr : %J%r\n",
                              ERR_CACHE_INODE, context->cache_status);
                  /* after successful cache_inode_readdir, pentry_tmp may be
                   * read locked */
                  return context->cache_status;
                }

              print_item_line(output, &attrs, item_path, symlink_path.path);
            }
          else if(flag_S)
            {
              fprintf(output, "%s :\n", item_path);
              if(cache_inode_getattr(dirent_array[i]->pentry,
                                     &attrs,
                                     ht,
                                     &context->client,
                                     &context->context,
                                     &context->cache_status) != CACHE_INODE_SUCCESS)
                {
                  log_fprintf(output, "Error executing cache_inode_getattr : %J%r\n",
                              ERR_CACHE_INODE, context->cache_status);
                  /* after successful cache_inode_readdir, pentry_tmp may be
                   * read locked */
                  return context->cache_status;
                }
              if(!flag_z)
                print_fsal_attributes(attrs, output);
            }
          else if(flag_L)
            {
              if(!flag_z)
                {
                  if(dirent_array[i]->pentry->internal_md.type != REGULAR_FILE)
                    fprintf(output, "%p N/A \t\t%s\n",
                            dirent_array[i]->pentry, item_path);
                  else
                    {
                      if(dirent_array[i]->pentry->object.file.pentry_content == NULL)
                        fprintf(output, "%p (not cached) \t%s\n",
                                dirent_array[i]->pentry,
                                item_path);
                      else
                        fprintf(output, "%p %p \t%s\n",
                                dirent_array[i]->pentry,
                                dirent_array[i]->pentry->object.file.pentry_content,
                                item_path);
                    }
                }
            }
          else if(flag_H)
            {
              if(!flag_z)
                {
                  char buff[128];

                  if((pfsal_handle =
                      cache_inode_get_fsal_handle(dirent_array[i]->pentry,
                                                  &context->cache_status)) == NULL)
                    {
                        /* after successful cache_inode_readdir, pentry_tmp may be
                         * read locked */
                      return 1;
                    }
                  snprintmem(buff, 128, (caddr_t) pfsal_handle, sizeof(fsal_handle_t));
                  fprintf(output, "%s (@%s)\n", item_path, buff);
                }
            }
          else
            {
              if(!flag_z)
                fprintf(output, "%s\n", item_path);
            }
        }

      /* Ready for next iteration */
      LogFullDebug(COMPONENT_CACHE_INODE,
                   "--------------> begin_cookie = %"PRIu64", nbfound=%d, "
		   "last cookie=%"PRIu64", end_cookie=%"PRIu64", "
		   "begin_cookie + nbfound =%"PRIu64"\n",
                   begin_cookie, nbfound, dirent_array[nbfound - 1]->cookie,
                   end_cookie, begin_cookie + nbfound);
      begin_cookie = end_cookie;
    }

  /* after successful cache_inode_readdir, pentry_tmp may be
   * read locked */

  return 0;
}                               /* fn_Cache_inode_ls */

/** display statistics about FSAL calls. */
int fn_Cache_inode_callstat(int argc,   /* IN : number of args in argv */
                            char **argv,        /* IN : arg list               */
                            FILE * output)      /* IN : output stream          */
{
  int i;
  hash_stat_t hstat;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* displaying stats */
  /* header: */
  fprintf(output,
          "Function             | Nb_Calls    | Success     | Retryable   | Unrecoverable\n");
  /* content */
  for(i = 0; i < CACHE_INODE_NB_COMMAND; i++)
    fprintf(output, "%-20s | %11u | %11u | %11u | %11u\n",
            cache_inode_function_names[i],
            context->client.stat.func_stats.nb_call[i],
            context->client.stat.func_stats.nb_success[i],
            context->client.stat.func_stats.nb_err_retryable[i],
            context->client.stat.func_stats.nb_err_unrecover[i]);
  fprintf(output,
          "------------------------------------------------------------------------------\n");

  /* Statistics for the HashTable */
  HashTable_GetStats(ht, &hstat);
  fprintf(output, "Operation            |     ok      |    err      |   notfound  \n");
  fprintf(output, "Set                  | %11u | %11u | %11u \n",
          hstat.dynamic.ok.nb_set, hstat.dynamic.err.nb_set,
          hstat.dynamic.notfound.nb_set);
  fprintf(output, "Test                 | %11u | %11u | %11u \n",
          hstat.dynamic.ok.nb_test, hstat.dynamic.err.nb_test,
          hstat.dynamic.notfound.nb_test);
  fprintf(output, "Get                  | %11u | %11u | %11u \n", hstat.dynamic.ok.nb_get,
          hstat.dynamic.err.nb_get, hstat.dynamic.notfound.nb_get);
  fprintf(output, "Del                  | %11u | %11u | %11u \n", hstat.dynamic.ok.nb_del,
          hstat.dynamic.err.nb_del, hstat.dynamic.notfound.nb_del);
  fprintf(output,
          "------------------------------------------------------------------------------\n");
  fprintf(output, "There are %d entries in the Cache inode HashTable\n",
          hstat.dynamic.nb_entries);
  fprintf(output,
          "index_size=%d  min_rbt_num_node=%d  max_rbt_num_node=%d average_rbt_num_node=%d\n",
          ht->parameter.index_size, hstat.computed.min_rbt_num_node,
          hstat.computed.max_rbt_num_node, hstat.computed.average_rbt_num_node);
  fprintf(output,
          "------------------------------------------------------------------------------\n");
  fprintf(output,
          "Client LRU_GC: nb_entry=%d, nb_invalid=%d, nb_call_gc=%d, param.nb_call_gc_invalid=%d\n",
          context->client.lru_gc->nb_entry, context->client.lru_gc->nb_invalid,
          context->client.lru_gc->nb_call_gc,
          context->client.lru_gc->parameter.nb_call_gc_invalid);
  fprintf(output,
          "------------------------------------------------------------------------------\n");

  return 0;
}

/** proceed an mkdir command. */
int fn_Cache_inode_mkdir(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_mkdir[] =
      "usage: mkdir [-h][-v] <path> [mode]\n"
      "       path: parent directory where the directory is to be created\n"
      "       name: name of the directory is to be created\n"
      "       mode: octal mode for the directory is to be created (ex: 755)\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *new_hdl;
  cache_entry_t *subdir_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode;
  fsal_accessmode_t fsalmode = 0755;
  fsal_attrib_list_t attrmkdir;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;
  char *strmode;

  fsal_name_t objname;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

  /* Exactly 1 or 2 args expected */

  if(Optind != (argc - 2) && Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

      if(Optind == (argc - 1))
        {
          mode = 0755;
        }
      else
        {
          strmode = argv[Optind + 1];
          /* converting mode string to FSAL mode string */
          mode = atomode(strmode);
        }

      if(mode < 0)
        err_flag++;
      else
        {
          fsalmode = 0;

          if(mode & S_ISUID)
            fsalmode |= FSAL_MODE_SUID;
          if(mode & S_ISGID)
            fsalmode |= FSAL_MODE_SGID;

          if(mode & S_IRUSR)
            fsalmode |= FSAL_MODE_RUSR;
          if(mode & S_IWUSR)
            fsalmode |= FSAL_MODE_WUSR;
          if(mode & S_IXUSR)
            fsalmode |= FSAL_MODE_XUSR;

          if(mode & S_IRGRP)
            fsalmode |= FSAL_MODE_RGRP;
          if(mode & S_IWGRP)
            fsalmode |= FSAL_MODE_WGRP;
          if(mode & S_IXGRP)
            fsalmode |= FSAL_MODE_XGRP;

          if(mode & S_IROTH)
            fsalmode |= FSAL_MODE_ROTH;
          if(mode & S_IWOTH)
            fsalmode |= FSAL_MODE_WOTH;
          if(mode & S_IXOTH)
            fsalmode |= FSAL_MODE_XOTH;

        }
    }

  if(err_flag)
    {
      fprintf(output, help_mkdir);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, path, context->pentry, &new_hdl,
                     output)))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(file, 256, &objname);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  subdir_hdl = cache_inode_create(new_hdl,
                                  &objname,
                                  DIRECTORY,
                                  cachepol,
                                  fsalmode,
                                  NULL,
                                  &attrmkdir,
                                  ht,
                                  &context->client,
                                  &context->context, &context->cache_status);

  if((subdir_hdl == NULL) || (context->cache_status != 0))
    {
      log_fprintf(output, "Error executing cache_inode_create(DIRECTORY) : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);

      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "%s/%s successfully created (handle=%p) \n", glob_path, file,
              subdir_hdl);
    }

  return 0;
}

/** proceed an create command. */
int fn_Cache_inode_link(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_hardlink[] =
      "hardlink: create a hard link.\n"
      "usage: hardlink [-h][-v] <target> <new_path>\n"
      "       target: path of an existing file.\n"
      "       new_path: path of the hardlink to be created\n";

  char glob_path_target[FSAL_MAX_PATH_LEN];
  char glob_path_link[FSAL_MAX_PATH_LEN];

  cache_entry_t *target_hdl;
  cache_entry_t *dir_hdl;
  fsal_name_t link_name;

  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  fsal_attrib_list_t attrlink;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *target = NULL;
  char *path;
  char *name;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

  /* There should be exactly 2 arguments */
  if(Optind == (argc - 2))
    {
      target = argv[Optind];

      strncpy(tmp_path, argv[Optind + 1], FSAL_MAX_PATH_LEN);
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
  strncpy(glob_path_target, context->current_path, FSAL_MAX_PATH_LEN);
  strncpy(glob_path_link, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if((rc =
     cache_solvepath(glob_path_target, FSAL_MAX_PATH_LEN, target, context->pentry,
                     &target_hdl, output)))
    return rc;

  if((rc =
     cache_solvepath(glob_path_link, FSAL_MAX_PATH_LEN, path, context->pentry, &dir_hdl,
                     output)))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(name, 256, &link_name);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(cache_inode_link(target_hdl,
                      dir_hdl,
                      &link_name,
                      cachepol,
                      &attrlink,
                      ht,
                      &context->client,
                      &context->context, &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_link : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "hardlink successfully created \n");
    }

  return 0;
}

/** proceed an ln (symlink) command. */
int fn_Cache_inode_ln(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_ln[] =
      "usage: ln [-h][-v] <link_content> <link_path>\n"
      "       link_content: content of the symbolic link to be created\n"
      "       link_path: path of the symbolic link to be created\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *new_hdl;
  cache_entry_t *subdir_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  fsal_accessmode_t fsalmode = 0777;
  fsal_attrib_list_t attrsymlink;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;
  char *content = NULL;

  fsal_name_t objname;

  cache_inode_create_arg_t create_arg;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

  /* Exactly 2 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      content = argv[Optind];
      strncpy(tmp_path, argv[Optind + 1], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

    }

  if(err_flag)
    {
      fprintf(output, help_ln);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, path, context->pentry, &new_hdl,
                     output)))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(file, 256, &objname);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* create fsal_path_t */
  st = FSAL_str2path(content, 256, &create_arg.link_content);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2path:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if((subdir_hdl = cache_inode_create(new_hdl,
                                      &objname,
                                      SYMBOLIC_LINK,
                                      cachepol,
                                      fsalmode,
                                      &create_arg,
                                      &attrsymlink,
                                      ht,
                                      &context->client,
                                      &context->context, &context->cache_status)) == NULL)
    {
      log_fprintf(output, "Error executing cache_inode_create(SYMBOLIC_LINK) : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);

      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "%s/%s successfully created (handle=%p) \n", glob_path, file,
              subdir_hdl);
    }

  return 0;
}

/** proceed an create command. */
int fn_Cache_inode_create(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_create[] =
      "usage: create [-h][-v] <path> [mode]\n"
      "       path: path of the file to be created\n"
      "       mode: octal mode for the directory to be created (ex: 644)\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *new_hdl;
  cache_entry_t *subdir_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode;
  fsal_accessmode_t fsalmode = 0644;
  fsal_attrib_list_t attrcreate;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;
  char *strmode;

  fsal_name_t objname;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

  /* 1 or 2 args expected */
  if(Optind != (argc - 2) && Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {

      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

      if(Optind == (argc - 1))
        {
          mode = 0755;
        }
      else
        {
          strmode = argv[Optind + 1];
          /* converting mode string to FSAL mode string */
          mode = atomode(strmode);
        }

      if(mode < 0)
        err_flag++;
      else
        {

          fsalmode = 0;

          if(mode & S_ISUID)
            fsalmode |= FSAL_MODE_SUID;
          if(mode & S_ISGID)
            fsalmode |= FSAL_MODE_SGID;

          if(mode & S_IRUSR)
            fsalmode |= FSAL_MODE_RUSR;
          if(mode & S_IWUSR)
            fsalmode |= FSAL_MODE_WUSR;
          if(mode & S_IXUSR)
            fsalmode |= FSAL_MODE_XUSR;

          if(mode & S_IRGRP)
            fsalmode |= FSAL_MODE_RGRP;
          if(mode & S_IWGRP)
            fsalmode |= FSAL_MODE_WGRP;
          if(mode & S_IXGRP)
            fsalmode |= FSAL_MODE_XGRP;

          if(mode & S_IROTH)
            fsalmode |= FSAL_MODE_ROTH;
          if(mode & S_IWOTH)
            fsalmode |= FSAL_MODE_WOTH;
          if(mode & S_IXOTH)
            fsalmode |= FSAL_MODE_XOTH;

        }

    }

  if(err_flag)
    {
      fprintf(output, help_create);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, path, context->pentry, &new_hdl,
                     output)))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(file, 256, &objname);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if((subdir_hdl = cache_inode_create(new_hdl,
                                      &objname,
                                      REGULAR_FILE,
                                      cachepol,
                                      fsalmode,
                                      NULL,
                                      &attrcreate,
                                      ht,
                                      &context->client,
                                      &context->context, &context->cache_status)) == NULL)
    {
      log_fprintf(output, "Error executing cache_inode_create(DIRECTORY) : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);

      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "%s/%s successfully created (handle=%p) \n", glob_path, file,
              subdir_hdl);
    }

  return 0;
}

/** proceed a rename command. */
int fn_Cache_inode_rename(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_rename[] = "usage: rename [-h][-v] <src> <dest>\n";

  char src_glob_path[FSAL_MAX_PATH_LEN];
  char tgt_glob_path[FSAL_MAX_PATH_LEN];

  cache_entry_t *src_path_pentry;
  cache_entry_t *tgt_path_pentry;
  fsal_name_t src_name;
  fsal_name_t tgt_name;

  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char tmp_path1[FSAL_MAX_PATH_LEN];
  char tmp_path2[FSAL_MAX_PATH_LEN];
  char *src_path;
  char *src_file;
  char *tgt_path;
  char *tgt_file;

  fsal_attrib_list_t attrsrc;
  fsal_attrib_list_t attrdest;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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
      strncpy(tmp_path1, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path1, &src_path, &src_file);

      strncpy(tmp_path2, argv[Optind + 1], FSAL_MAX_PATH_LEN);
      split_path(tmp_path2, &tgt_path, &tgt_file);
    }

  if(err_flag)
    {
      fprintf(output, help_rename);
      return -1;
    }

  if(flag_v)
    fprintf(output, "Renaming %s (dir %s) to %s (dir %s)\n", src_file, src_path, tgt_file,
            tgt_path);

  /* copy current path. */
  strncpy(src_glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  strncpy(tgt_glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves paths handles */
  if((rc = cache_solvepath(src_glob_path,
                          FSAL_MAX_PATH_LEN,
                           src_path, context->pentry, &src_path_pentry, output)))
    return rc;

  if((rc = cache_solvepath(tgt_glob_path,
                          FSAL_MAX_PATH_LEN,
                          tgt_path, context->pentry, &tgt_path_pentry, output)))
    return rc;

  /* create fsal_name_t */

  st = FSAL_str2name(src_file, 256, &src_name);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  st = FSAL_str2name(tgt_file, 256, &tgt_name);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* Rename operation */
  if(cache_inode_rename(src_path_pentry,
                        &src_name,
                        tgt_path_pentry,
                        &tgt_name,
                        &attrsrc,
                        &attrdest,
                        ht,
                        &context->client,
                        &context->context, &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_rename : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);

      return context->cache_status;
    }

  if(flag_v)
    fprintf(output, "%s/%s successfully renamed to %s/%s\n",
            src_glob_path, src_file, tgt_glob_path, tgt_file);

  return 0;
}

/** proceed an unlink command. */
int fn_Cache_inode_unlink(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_ln[] = "usage: ln [-h][-v] <path>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  cache_entry_t *new_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;

  fsal_name_t objname;
  fsal_attrib_list_t attrparent;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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
      fprintf(output, help_ln);
      return 0;
    }

  /* Exactly 2 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

    }

  if(err_flag)
    {
      fprintf(output, help_ln);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, path, context->pentry, &new_hdl,
                     output)))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(file, 256, &objname);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2path:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  cache_inode_remove(new_hdl,
                     &objname,
                     &attrparent,
                     ht, &context->client, &context->context, &context->cache_status);
  if(context->cache_status != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_remove : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "%s/%s successfully unlinked\n", glob_path, file);
    }

  return 0;
}

/** setattr
 *
 * syntax of command line:
 * setattr file_path  attribute_name  attribute_value
 *
 */
int fn_Cache_inode_setattr(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_setattr[] =
      "usage: setattr [-h][-v] <path> <attr>=<value>,<attr>=<value>,...\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */

  cache_entry_t *obj_hdl;       /* handle of the object    */
  fsal_attrib_list_t set_attrs; /* attributes to be setted */
  cache_inode_status_t cache_status;    /* FSAL return status      */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char file[FSAL_MAX_NAME_LEN]; /* the relative path to the object */
  char *attr_list = NULL;       /* attribute name */

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

      shell_attribute_t *curr_attr;

      /* print usage */
      fprintf(output, help_setattr);

      fprintf(output, "\n<attr> can be one of the following values:\n");

      /* print attribute list */

      for(curr_attr = shell_attr_list; curr_attr->attr_type != ATTR_NONE; curr_attr++)
        {
          switch (curr_attr->attr_type)
            {
            case ATTR_32:
              fprintf(output, "\t %s \t:\t 32 bits integer\n", curr_attr->attr_name);
              break;

            case ATTR_64:
              fprintf(output, "\t %s \t:\t 64 bits integer\n", curr_attr->attr_name);
              break;

            case ATTR_OCTAL:
              fprintf(output, "\t %s \t:\t octal\n", curr_attr->attr_name);
              break;

            case ATTR_TIME:
              fprintf(output, "\t %s \t:\t time (format: YYYYMMDDhhmmss)\n",
                      curr_attr->attr_name);
              break;
            default:
              break;
            }
        }

      return 0;
    }

  /* Exactly 2 args expected (path and attributes list) */

  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      strcpy(file, argv[Optind]);
      attr_list = argv[Optind + 1];
    }

  if(err_flag)
    {
      fprintf(output, help_setattr);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose attributes are to be changed */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  /* Convert the peer (attr_name,attr_val) to an FSAL attribute structure. */
  rc = MkFSALSetAttrStruct(attr_list, &set_attrs);

  /* interprets output code */
  switch (rc)
    {
    case 0:
      /* OK */
      break;

    case EFAULT:
      fprintf(output, "setattr: Internal error.\n");
      return rc;

    case ENOENT:
      fprintf(output, "setattr: Unknown attribute in list %s\n", attr_list);
      return rc;

    case EINVAL:
      fprintf(output, "setattr: Invalid value for attribute in list %s\n", attr_list);
      return rc;

    default:
      fprintf(output, "setattr: Error %d converting attributes.\n", rc);
      return rc;
    }

  /* if verbose mode is on, we print the attributes to be set */
  if(flag_v)
    {
      print_fsal_attributes(set_attrs, output);
    }

  /* executes set attrs */
  if((cache_status = cache_inode_setattr(obj_hdl,
                                         &set_attrs,
                                         ht,
                                         &context->client,
                                         &context->context,
                                         &context->cache_status)) != CACHE_INODE_SUCCESS)

    {
      log_fprintf(output, "Error executing cache_inode_setattr : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  return 0;
}

/**
 * perform an access command.
 * syntax: access [F][R][W][X] <file>
 * example: access toto FRX
 */

int fn_Cache_inode_access(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
                          FILE * output /* IN : output stream          */ )
{

  char format[] = "hv";

  const char help_access[] =
      "usage: access [-h][-v] <rights> <path>\n"
      "\n"
      "   -h : print this help\n"
      "   -v : verbose mode\n"
      "   -A : test access from attributes\n"
      "        ( call to getattr + test_access instead of access )\n"
      "\n"
      " <rights> : a set of the following characters:\n"
      "    F: test file existence\n"
      "    R: test read permission\n"
      "    W: test write permission\n"
      "    X: test execute permission\n"
      "\n"
      "Example: access -A RX my_dir\n"
      "test read and exec rights for directory \"my_dir\"\n"
      "by doing a getattr and a test_access call.\n\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */
  fsal_accessflags_t test_perms;        /* permissions to be tested    */

  int rc, option;
  unsigned int i;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */
  char *str_perms = NULL;       /* string that represents the permissions to be tested */

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_access);
      return 0;
    }

  /* Exactly 2 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      str_perms = argv[Optind];
      file = argv[Optind + 1];
    }

  if(err_flag)
    {
      fprintf(output, help_access);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  /* Convert the permission string to an fsal access test. */
  test_perms = 0;

  for(i = 0; i < strlen(str_perms); i++)
    {
      switch (str_perms[i])
        {
        case 'F':
          if(flag_v)
            fprintf(output, "F_OK flag\n");
          test_perms |= FSAL_F_OK;
          break;

        case 'R':
          if(flag_v)
            fprintf(output, "R_OK flag\n");
          test_perms |= FSAL_R_OK;
          break;

        case 'W':
          if(flag_v)
            fprintf(output, "W_OK flag\n");
          test_perms |= FSAL_W_OK;
          break;

        case 'X':
          if(flag_v)
            fprintf(output, "X_OK flag\n");
          test_perms |= FSAL_X_OK;
          break;

        default:
          fprintf(output, "**** Invalid test: %c ****\n", str_perms[i]);
          fprintf(output, help_access);
          return -1;
        }
    }

  /* Call to FSAL */

  if((context->cache_status = cache_inode_access(obj_hdl,
                                                 test_perms,
                                                 ht,
                                                 &context->client,
                                                 &context->context,
                                                 &context->cache_status)) !=
     CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_access : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }
  else
    {
      fprintf(output, "access: Access granted.\n");
      return 0;
    }
}

/** cache en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_data_cache(int argc, /* IN : number of args in argv */
                              char **argv,      /* IN : arg list               */
                              FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_data_cache[] =
      "usage: data_cache [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

#ifdef _USE_PROXY
  fsal_name_t name;
#endif

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_data_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_data_cache);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

#ifdef _USE_PROXY
  if(FSAL_IS_ERROR(FSAL_str2name(file, MAXPATHLEN, &name)))
    {
      context->cache_status = CACHE_INODE_FSAL_ERROR;
      log_fprintf(output, "Error opening file during cache_inode_add_cache : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;

    }

  if(cache_inode_open_by_name(context->pentry,
                              &name,
                              obj_hdl,
                              &context->client,
                              FSAL_O_RDWR,
                              &context->context,
                              &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error opening file during cache_inode_add_cache : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }
#endif

  if(flag_v)
    printf("---> data_cache using pentry_inode = %p\n", obj_hdl);

  if(cache_inode_add_data_cache(obj_hdl, ht, &context->client, &context->context,
                                &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_add_cache : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "Entry %p is now boud to datacache entry %p\n", obj_hdl,
              obj_hdl->object.file.pentry_content);
    }

  return 0;
}                               /* fn_Cache_inode_data_cache */

/** cache en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_release_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_release_cache[] =
      "usage: release_cache [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_release_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_release_cache);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  if(cache_inode_release_data_cache
     (obj_hdl, ht, &context->client, &context->context,
      &context->cache_status) != CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_release_cache : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  if(flag_v)
    {
      fprintf(output, "Entry %p is no more bounded to datacache\n", obj_hdl);
    }

  return 0;
}                               /* fn_Cache_inode_release_cache */

/** recover the data cache */
int fn_Cache_inode_recover_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_recover_cache[] =
      "usage: recover_cache [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  cache_content_status_t cache_content_status;

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_recover_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != argc)
    {
      err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_recover_cache);
      return -1;
    }

  if(cache_content_crash_recover(EXPORT_ID,
                                 0,
                                 1,
                                 (cache_content_client_t *) context->client.
                                 pcontent_client, &context->client, ht, &context->context,
                                 &cache_content_status) != CACHE_CONTENT_SUCCESS)
    {
      fprintf(output, "Error executing cache_content_crash_recover: %d\n",
              cache_content_status);
      return cache_content_status;
    }

  if(flag_v)
    {
      fprintf(output, "Data cache has been recovered\n");
    }

  return 0;
}                               /* fn_Cache_inode_recover_cache */

/** refresh en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_refresh_cache(int argc,      /* IN : number of args in argv */
                                 char **argv,   /* IN : arg list               */
                                 FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_refresh_cache[] =
      "usage: refresh_cache [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */
  cache_content_status_t cache_content_status;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_refresh_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_refresh_cache);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  if(obj_hdl->object.file.pentry_content == NULL)
    {
      fprintf(output, "Error: this entry is not data cached\n");
      return 1;
    }

  if(cache_content_refresh(obj_hdl->object.file.pentry_content,
                           (cache_content_client_t *) context->client.pcontent_client,
                           &context->context,
                           FORCE_FROM_FSAL,
                           &cache_content_status) != CACHE_CONTENT_SUCCESS)
    {
      fprintf(output, "Error executing cache_content_refresh: %d\n",
              cache_content_status);
      return cache_content_status;
    }

  if(flag_v)
    {
      fprintf(output, "Entry %p has been refreshed\n", obj_hdl);
    }

  return 0;
}                               /* fn_Cache_inode_refresh_cache */

/** flush en entry (REGULAR_FILE) in the data cache */
int fn_Cache_inode_flush_cache(int argc,        /* IN : number of args in argv */
                               char **argv,     /* IN : arg list               */
                               FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_flush_cache[] =
      "usage: flush_cache [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */
  cache_content_status_t cache_content_status;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_flush_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_flush_cache);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  if(obj_hdl->object.file.pentry_content == NULL)
    {
      fprintf(output, "Error: this entry is not data cached\n");
      return 1;
    }

  if(cache_content_flush(obj_hdl->object.file.pentry_content,
                         CACHE_CONTENT_FLUSH_AND_DELETE,
                         (cache_content_client_t *) context->client.pcontent_client,
                         &context->context,
                         &cache_content_status) != CACHE_CONTENT_SUCCESS)
    {
      fprintf(output, "Error executing cache_content_flush: %d\n", cache_content_status);
      return cache_content_status;
    }

  if(flag_v)
    {
      fprintf(output, "Entry %p has been flushed\n", obj_hdl);
    }

  return 0;
}                               /* fn_Cache_inode_flush_cache */

/** Reads the content of a cached regular file */
int fn_Cache_inode_read(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output /* IN : output stream          */ )
{
  char format[] = "hvAXB:s:";
  int rc, option;

  int err_flag = 0;
  int flag_v = 0;
  int flag_h = 0;
  int flag_s = 0;
  int flag_A = 0;
  int flag_X = 0;
  int flag_B = 0;

  fsal_attrib_list_t fsal_attr;

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  char str_seek_buff[256];
  char *str_seek_type = NULL;
  char *str_seek_offset = NULL;
  char *str_total_bytes = NULL;
  char *str_block_size = NULL;

  fsal_size_t block_size = 1024;        /* default: 1ko */
  fsal_size_t total_bytes = 0;  /* 0 == read all */
  fsal_seek_t seek_desc = { FSAL_SEEK_SET, 0 }; /* default: start of the file */

  /* fsal arguments */

  fsal_boolean_t is_eof = 0;
  fsal_size_t total_nb_read = 0;
  fsal_size_t once_nb_read = 0;
  fsal_size_t nb_block_read = 0;

  char *p_read_buff;

  char *file = NULL;            /* the relative path to the object */

  struct timeval timer_start;
  struct timeval timer_stop;
  struct timeval timer_diff;

  const char help_read[] =
      "Usage:\n"
      "  read [-h][-v][-A][-X] [-B <block_size> ] [ -s <seek_type>,<offset> ]  { <total_bytes> | all } filename\n"
      "Options:\n"
      "  -h: print this help\n"
      "  -v: verbose mode\n"
      "  -A: display read data in ascii\n"
      "  -X: display read data in hexa\n"
      "  -B <blocksize>: block size used for reading, in bytes (default 1k).\n"
      "  -s <seek_type>,<offset>: specify the position of the first byte to be read.\n"
      "        <seek_type> can take the values SET, CUR or END.\n"
      "        <offset> is a signed integer.\n"
      "  <total_bytes>: indicates the total number of bytes to be read\n"
      "      ('all' indicates that data are read until the end of the file).\n"
      "Example:\n"
      "  For reading the last 2kB of the opened file, using 1k block size:\n"
      "        read -B 1024 -s END,-2048 all  filename\n";

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */
  getopt_init();

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "read: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "read: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case 'A':
          if(flag_A)
            fprintf(output,
                    "read: warning: option 'A' has been specified more than once.\n");
          else if(flag_X)
            {
              fprintf(output, "read: option 'A' conflicts with option 'X'.\n");
              err_flag++;
            }
          else
            flag_A++;
          break;

        case 'X':
          if(flag_X)
            fprintf(output,
                    "read: warning: option 'X' has been specified more than once.\n");
          else if(flag_A)
            {
              fprintf(output, "read: option 'X' conflicts with option 'A'.\n");
              err_flag++;
            }
          else
            flag_X++;
          break;

        case 'B':
          if(flag_B)
            fprintf(output,
                    "read: warning: option 'B' has been specified more than once.\n");
          else
            {
              flag_B++;
              str_block_size = Optarg;
            }
          break;

        case 's':
          if(flag_s)
            fprintf(output,
                    "read: warning: option 's' has been specified more than once.\n");
          else
            {
              flag_s++;
              strncpy(str_seek_buff, Optarg, 256);
              str_seek_type = str_seek_buff;
            }
          break;

        case '?':
          fprintf(output, "read: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_read);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      str_total_bytes = argv[Optind];
      file = argv[Optind + 1];
    }

  if(err_flag)
    {
      fprintf(output, help_read);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  /* Sanity check */
  if(obj_hdl->internal_md.type != REGULAR_FILE)
    {
      fprintf(output, "Error: This entry is no REGULAR_FILE\n");
      return 1;
    }

  /* Is this entry cached ? */
  if(obj_hdl->object.file.pentry_content == NULL)
    {
      if(flag_v)
        fprintf(output, "Warning: This entry is not data cached\n");
    }

  /* check argument types */

  if(flag_B)
    {
      /* Try to convert the str_block_size to fsal_size_t */

      rc = ato64(str_block_size, &block_size);

      if(rc == -1)
        {
          fprintf(output, "read: error: invalid block size \"%s\"\n", str_block_size);
          err_flag++;
        }

    }

  if(flag_s)
    {
      /* Try to parse the argument */

      str_seek_offset = strchr(str_seek_type, ',');

      if(str_seek_offset == NULL)
        {
          fprintf(output,
                  "read: error: invalid seek specifier \"%s\". <seek_type>,<offset> expected.\n",
                  str_seek_type);
          err_flag++;
        }

      if(!err_flag)
        {
          int sign = 1;

          *str_seek_offset = '\0';
          str_seek_offset++;    /* the first char after the "," */

          /* Check seek type */

          if(!strncmp(str_seek_type, "CUR", 256))
            seek_desc.whence = FSAL_SEEK_CUR;
          else if(!strncmp(str_seek_type, "SET", 256))
            seek_desc.whence = FSAL_SEEK_SET;
          else if(!strncmp(str_seek_type, "END", 256))
            seek_desc.whence = FSAL_SEEK_END;
          else
            {
              fprintf(output,
                      "read: error: invalid seek type \"%s\". CUR, SET or END expected.\n",
                      str_seek_type);
              err_flag++;
            }

          /* Try to convert str_seek_offset to fsal_off_t */

          switch (str_seek_offset[0])
            {
            case '+':
              sign = 1;
              str_seek_offset++;
              break;

            case '-':
              sign = -1;
              str_seek_offset++;
              break;
            }

          rc = ato64(str_seek_offset, (unsigned long long *)&seek_desc.offset);

          if(rc == -1)
            {
              fprintf(output, "read: error: invalid offset \"%s\".\n", str_seek_offset);
              err_flag++;
            }
          else if(sign < 0)
            seek_desc.offset = -seek_desc.offset;

        }

    }
  /* else default seeking : SET,0 */

  if(!strcasecmp(str_total_bytes, "all"))
    {
      total_bytes = 0;
    }
  else
    {
      rc = ato64(str_total_bytes, &total_bytes);

      if(rc == -1)
        {
          fprintf(output,
                  "read: error: invalid read size \"%s\". \"all\" or <nb_bytes> expected.\n",
                  str_total_bytes);
          err_flag++;
        }
    }

  if(err_flag)
    {
      fprintf(output, help_read);
      return -1;
    }

  if(flag_v)
    {
      /* print a sum-up of read parameters */
      fprintf(output,
              "Read options: Block size: %llu Bytes, Seek: %s%+lld, Read limit: %llu Bytes\n",
              block_size,
              (seek_desc.whence == FSAL_SEEK_SET ? "SET" : seek_desc.whence ==
               FSAL_SEEK_CUR ? "CUR" : "END"), (long long)seek_desc.offset, total_bytes);
    }

  /* Now all arguments have been parsed, let's act ! */

  /* alloc a buffer */
  p_read_buff = Mem_Alloc(block_size);

  if(p_read_buff == NULL)
    {
      fprintf(output,
              "read: error: Not enough memory to allocate read buffer (%llu Bytes).\n",
              block_size);
      return ENOMEM;
    }

  gettimeofday(&timer_start, NULL);

  /* while EOF is not reached, and read<asked (when total_bytes!=0) */
  while(!is_eof && !((total_bytes != 0) && (total_nb_read >= total_bytes)))
    {
      if(cache_inode_rdwr(obj_hdl,
                          CACHE_INODE_READ,
                          &seek_desc,
                          block_size,
                          &once_nb_read,
                          &fsal_attr,
                          (caddr_t) p_read_buff,
                          &is_eof,
                          ht,
                          &context->client,
                          &context->context,
                          TRUE, &context->cache_status) != CACHE_INODE_SUCCESS)
        {
          log_fprintf(output, "Error executing cache_inode_read : %J%r\n",
                      ERR_CACHE_INODE, context->cache_status);

          return context->cache_status;
        }

      if(isFullDebug(COMPONENT_CACHE_INODE))
        {
          fprintf(output,
                  "shell: block_size=%llu, once_nb_read=%llu, total_bytes=%llu, total_nb_read=%llu, eof=%d, seek=%d.%"PRIu64,
                  block_size, once_nb_read, total_bytes, total_nb_read, is_eof,
                  seek_desc.whence, seek_desc.offset);
        }

      /* print what was read. */
      if(flag_A)
        {
          fsal_size_t index;
          for(index = 0; index < once_nb_read; index++)
            fprintf(output, "%c.", p_read_buff[index]);
        }
      else if(flag_X)
        {
          fsal_size_t index;
          for(index = 0; index < once_nb_read; index++)
            fprintf(output, "%.2X ", p_read_buff[index]);
        }
      else
        fprintf(output, ".");

      /* update stats */

      if(once_nb_read > 0)
        nb_block_read++;

      total_nb_read += once_nb_read;

      /* flush */
      if(nb_block_read % 10)
        fflush(output);

      /* Update the seek descriptor */
      seek_desc.whence = FSAL_SEEK_SET;
      seek_desc.offset += once_nb_read;

    }

  gettimeofday(&timer_stop, NULL);

  /* newline after read blocks */
  fprintf(output, "\n");

  if(flag_v)
    {
      double bandwidth;

      /* print stats */
      fprintf(output, "Nb blocks read: %llu\n", nb_block_read);
      fprintf(output, "Total: %llu Bytes\n", total_nb_read);

      fprintf(output, "Time enlapsed: ");
      timer_diff = time_diff(timer_start, timer_stop);
      print_timeval(output, timer_diff);

      bandwidth =
          total_nb_read / (1024 * 1024 *
                           (timer_diff.tv_sec + 0.000001 * timer_diff.tv_usec));

      fprintf(output, "Bandwidth: %f MB/s\n", bandwidth);

    }
  Mem_Free(p_read_buff);

  return 0;
}                               /* fn_Cache_inode_read */

/** Reads the content of a cached regular file */
int fn_Cache_inode_write(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ )
{
  char format[] = "hvs:N:A:X:";

  int rc, option;
  int err_flag = 0;

  int flag_v = 0;
  int flag_h = 0;
  int flag_N = 0;
  int flag_s = 0;
  int flag_A = 0;
  int flag_X = 0;

  char *str_times = NULL;
  char str_seek_buff[256];
  char *str_seek_type = NULL;
  char *str_seek_offset = NULL;

  char *str_hexa = NULL;
  char *str_ascii = NULL;

  size_t datasize = 0;
  char *databuff = NULL;

  unsigned long long nb_times = 1;      /* default = 1 */
  fsal_u64_t nb_block_written = 0;
  fsal_size_t size_written = 0;
  fsal_size_t size_written_once = 0;

  fsal_size_t block_size = 1024;        /* default: 1ko */
  fsal_seek_t seek_desc = { FSAL_SEEK_SET, 0 }; /* default: start of the file */

  fsal_attrib_list_t fsal_attr;

  fsal_boolean_t fsal_eof;

  struct timeval timer_start;
  struct timeval timer_stop;
  struct timeval timer_diff;

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  char *file = NULL;            /* the relative path to the object */

  const char help_write[] =
      "Usage:\n"
      "  write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -A <ascii_string> filename\n"
      "  write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -X <hexa_data> filename\n"
      "Where:\n"
      "  <seek_type> can be: SET, CUR, END\n"
      "  <offset> is a signed number of bytes.\n"
      "  <nb_times> is the number of times we write the expression into the file.\n"
      "\n"
      "  <ascii_string> is a string to be written to file.\n"
      "      Note that the null terminating character of is also written\n"
      "      to file.\n"
      "or\n"
      "  <hexa_data> is a data represented in hexadecimal format,\n"
      "      that is to be written to file.\n"
      "\n"
      "Examples:\n"
      "\n"
      "  For writing 10 times the null terminated string \"hello world\"\n"
      "  at the end of the file:\n"
      "        write -s END,0 -N 10 -A \"hello world\" filename\n"
      "\n"
      "  For overwriting the beginning of the file with\n"
      "  the pattern 0xA1267AEF31254ADE repeated twice:\n"
      "        write -s SET,0 -N 2 -X \"A1267AEF31254ADE\" filename\n";

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */
  getopt_init();

  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "write: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "write: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        case 'N':
          if(flag_N)
            fprintf(output,
                    "write: warning: option 'N' has been specified more than once.\n");
          else
            {
              flag_N++;
              str_times = Optarg;
            }
          break;

        case 's':
          if(flag_s)
            fprintf(output,
                    "write: warning: option 's' has been specified more than once.\n");
          else
            {
              flag_s++;
              strncpy(str_seek_buff, Optarg, 256);
              str_seek_buff[255] = '\0';
              str_seek_type = str_seek_buff;
            }
          break;

        case 'A':
          if(flag_A)
            fprintf(output,
                    "write: warning: option 'A' has been specified more than once.\n");
          else if(flag_X)
            {
              fprintf(output, "write: option 'A' conflicts with option 'X'.\n");
              err_flag++;
            }
          else
            {
              flag_A++;
              str_ascii = Optarg;
            }
          break;

        case 'X':
          if(flag_X)
            fprintf(output,
                    "write: warning: option 'X' has been specified more than once.\n");
          else if(flag_A)
            {
              fprintf(output, "write: option 'X' conflicts with option 'A'.\n");
              err_flag++;
            }
          else
            {
              flag_X++;
              str_hexa = Optarg;
            }
          break;

        case '?':
          fprintf(output, "write: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_write);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_write);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  glob_path[FSAL_MAX_PATH_LEN - 1] = '\0';

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  /* Sanity check */
  if(obj_hdl->internal_md.type != REGULAR_FILE)
    {
      fprintf(output, "Error: This entry is no REGULAR_FILE\n");
      return 1;
    }

  /* Is this entry cached ? */
  if(obj_hdl->object.file.pentry_content == NULL)
    {
      if(flag_v)
        fprintf(output, "Warning: This entry is not data cached\n");
    }

  /* check argument types */

  if(flag_N)
    {
      /* Try to convert the str_times to nb_times */

      rc = ato64(str_times, &nb_times);

      if(rc == -1)
        {
          fprintf(output, "write: error: invalid number \"%s\"\n", str_times);
          return EINVAL;
        }

    }

  if(flag_s)
    {
      int sign = 1;

      /* Try to parse the argument */

      str_seek_offset = strchr(str_seek_type, ',');

      if(str_seek_offset == NULL)
        {
          fprintf(output,
                  "write: error: invalid seek specifier \"%s\". <seek_type>,<offset> expected.\n",
                  str_seek_type);
          return EINVAL;
        }

      *str_seek_offset = '\0';
      str_seek_offset++;        /* the first char after the "," */

      /* Check seek type */

      if(!strncmp(str_seek_type, "CUR", 256))
        seek_desc.whence = FSAL_SEEK_CUR;
      else if(!strncmp(str_seek_type, "SET", 256))
        seek_desc.whence = FSAL_SEEK_SET;
      else if(!strncmp(str_seek_type, "END", 256))
        seek_desc.whence = FSAL_SEEK_END;
      else
        {
          fprintf(output,
                  "write: error: invalid seek type \"%s\". CUR, SET or END expected.\n",
                  str_seek_type);
          return EINVAL;
        }

      /* Try to convert str_seek_offset to fsal_off_t */

      switch (str_seek_offset[0])
        {
        case '+':
          sign = 1;
          str_seek_offset++;
          break;

        case '-':
          sign = -1;
          str_seek_offset++;
          break;
        }

      rc = ato64(str_seek_offset, (unsigned long long *)&seek_desc.offset);

      if(rc == -1)
        {
          fprintf(output, "write: error: invalid offset \"%s\".\n", str_seek_offset);
          return EINVAL;
        }
      else if(sign < 0)
        seek_desc.offset = -seek_desc.offset;

    }
  /* else default seeking : SET,0 */

  if(flag_A)
    {
      datasize = strlen(str_ascii) + 1; /* Include null termination char. */
      databuff = str_ascii;
    }

  if(flag_X)
    {
      size_t length = strlen(str_hexa);

      datasize = (length >> 1);

      if(length % 2)
        {

          /* if it is not odd: error */
          fprintf(output,
                  "write: error: in \"%s\", data length is not a multiple of 8 bits.\n",
                  str_hexa);

          return EINVAL;
        }

      databuff = Mem_Alloc(datasize + 1);

      if(databuff == NULL)
        {
          fprintf(output, "write: error: Not enough memory to allocate %llu Bytes.\n",
                  (unsigned long long)datasize);
          return ENOMEM;
        }

      memset(databuff, 0, datasize + 1);

      /* try to convert the string to hexa */
      rc = sscanmem(databuff, datasize, str_hexa);

      if(rc != (int)(2 * datasize))
        {
          /* if it is not odd: error */
          fprintf(output, "write: error: \"%s\" in not a valid hexa format.\n", str_hexa);

          Mem_Free(str_hexa);

          return EINVAL;
        }

    }

  if(flag_v)
    {
      /* print a sum-up of write parameters */
      fprintf(output, "Write options: Data length: %llu x %llu Bytes, Seek: %s%+lld\n",
              (unsigned long long)nb_times,
              (unsigned long long)datasize,
              (seek_desc.whence == FSAL_SEEK_SET ? "SET" :
               seek_desc.whence == FSAL_SEEK_CUR ? "CUR" :
               "END"), (long long)seek_desc.offset);
    }

  /* variables initialisation */

  block_size = (fsal_size_t) datasize;
  nb_block_written = 0;
  size_written = 0;
  size_written_once = 0;

  gettimeofday(&timer_start, NULL);

  /* write loop */

  while(nb_block_written < nb_times)
    {
      if(cache_inode_rdwr(obj_hdl,
                          CACHE_INODE_WRITE,
                          &seek_desc,
                          block_size,
                          &size_written_once,
                          &fsal_attr,
                          (caddr_t) databuff,
                          &fsal_eof,
                          ht,
                          &context->client,
                          &context->context,
                          TRUE, &context->cache_status) != CACHE_INODE_SUCCESS)
        {
          log_fprintf(output, "Error executing cache_inode_write : %J%r\n",
                      ERR_CACHE_INODE, context->cache_status);

          return context->cache_status;
        }

      fprintf(output, ".");

      /* update stats */

      if(size_written_once > 0)
        nb_block_written++;

      size_written += size_written_once;

      /* flush */
      if(nb_block_written % 10)
        fflush(output);

      /* Update the seek descriptor */
      seek_desc.whence = FSAL_SEEK_SET;
      seek_desc.offset += size_written_once;
    }

  gettimeofday(&timer_stop, NULL);

  /* newline after written blocks */
  fprintf(output, "\n");

  if(flag_v)
    {
      double bandwidth;

      /* print stats */
      fprintf(output, "Nb blocks written: %llu\n", nb_block_written);
      fprintf(output, "Total volume: %llu Bytes\n", size_written);

      fprintf(output, "Time enlapsed: ");
      timer_diff = time_diff(timer_start, timer_stop);
      print_timeval(output, timer_diff);

      bandwidth =
          size_written / (1024 * 1024 *
                          (timer_diff.tv_sec + 0.000001 * timer_diff.tv_usec));

      fprintf(output, "Bandwidth: %f MB/s\n", bandwidth);

    }

  if(flag_X)
    Mem_Free(databuff);

  return 0;
}                               /* fn_Cache_inode_write */

/** change thread contexte. */
int fn_Cache_inode_su(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output)    /* IN : output stream          */
{

  char *str_uid;
  uid_t uid;
  fsal_status_t st;
  struct passwd *pw_struct;
  int i;

# define MAX_GRPS  128
  gid_t groups_tab[MAX_GRPS];
  int nb_grp;

  const char help_su[] = "usage: su <uid>\n";

  cmdCacheInode_thr_info_t *context;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

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

  st = FSAL_GetClientContext(&context->context, &context->exp_context,
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

/** change current path */
int fn_Cache_inode_open_by_name(int argc,       /* IN : number of args in argv */
                                char **argv,    /* IN : arg list               */
                                FILE * output)  /* IN : output stream          */
{
  char glob_path[FSAL_MAX_PATH_LEN];

  cache_entry_t *pentry_file;
  fsal_attrib_list_t file_attr;
  fsal_status_t st;
  fsal_name_t filename;

  cmdCacheInode_thr_info_t *context;

  const char help_cd[] = "usage: open_by_name <path> \n";

  if(!cache_init)
    {
      fprintf(output, "\tCache is not initialized\n");
      return -1;
    }

  /* Exactly two arg expected */
  if(argc != 2)
    {
      fprintf(output, help_cd);
      return -1;
    }

  /* Convert filename to fsal_name_t */
  if(FSAL_IS_ERROR(st = FSAL_str2name(argv[1], FSAL_MAX_PATH_LEN, &filename)))
    {
      fprintf(output, "Error executing FSAL_str2name:\n");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;

      return -1;
    }

  context = RetrieveInitializedContext();

  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  glob_path[FSAL_MAX_PATH_LEN - 1] = '\0';

  if((pentry_file = cache_inode_lookup(context->pentry,
                                       &filename,
                                       cachepol,
                                       &file_attr,
                                       ht,
                                       &context->client,
                                       &context->context,
                                       &context->cache_status)) == NULL)
    {
      fprintf(output, "Error: cannot lookup %s in %s : %u\n", argv[1], glob_path,
              context->cache_status);
      return -1;
    }

  if((context->cache_status = cache_inode_open_by_name(context->pentry,
                                                       &filename,
                                                       pentry_file,
                                                       &context->client,
                                                       FSAL_O_RDWR,
                                                       &context->context,
                                                       &context->cache_status)) !=
     CACHE_INODE_SUCCESS)
    {
      log_fprintf(output, "Error executing cache_inode_open_by_name : %J%r\n",
                  ERR_CACHE_INODE, context->cache_status);
      return context->cache_status;
    }

  return 0;
}

/** Close an opened entry */
int fn_Cache_inode_close(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_flush_cache[] =
      "usage: flush_close [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

  cmdCacheInode_thr_info_t *context;


  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_flush_cache);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_flush_cache);
      return -1;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  glob_path[FSAL_MAX_PATH_LEN - 1] = '\0';

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  if(obj_hdl->object.file.pentry_content == NULL)
    {
      fprintf(output, "Error: this entry is not data cached\n");
      return 1;
    }
#ifdef _TOTO
  if(cache_content_flush(obj_hdl->object.file.pentry_content,
                         CACHE_CONTENT_FLUSH_AND_DELETE,
                         (cache_content_client_t *) context->client.pcontent_client,
                         &context->context,
                         &cache_content_status) != CACHE_CONTENT_SUCCESS)
    {
      fprintf(output, "Error executing cache_content_flush: %d\n", cache_content_status);
      return cache_content_status;
    }
#endif
  fprintf(output, "not implemented yet\n");

  if(flag_v)
    {
      fprintf(output, "Entry %p has been flushed\n", obj_hdl);
    }

  return 0;
}                               /* fn_Cache_inode_close */

/** Close an opened entry */
int fn_Cache_inode_invalidate(int argc,      /* IN : number of args in argv */
                              char **argv,   /* IN : arg list               */
                              FILE * output /* IN : output stream          */ )
{
  char format[] = "hv";

  const char help_invalidate[] =
      "usage: invalidate [-h][-v]  <path>\n"
      "\n" "   -h : print this help\n" "   -v : verbose mode\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  cache_entry_t *obj_hdl;       /* handle of the object        */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */

  cmdCacheInode_thr_info_t *context;

  fsal_attrib_list_t attr ;
  fsal_handle_t * pfsal_handle = NULL ;

  /* is the fs initialized ? */
  if(!cache_init)
    {
      fprintf(output, "Error: Cache is not initialized\n");
      return -1;
    }

  context = RetrieveInitializedContext();

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "access: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;

        case 'h':
          if(flag_h)
            fprintf(output,
                    "access: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;

        default:
        case '?':
          fprintf(output, "access: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_invalidate);
      return 0;
    }

  /* Exactly 1 args expected */
  if(Optind != (argc - 1))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_invalidate);
      return -1;
    }

  if(flag_h)
    {
      /* print usage */
      fprintf(output, help_invalidate);
      return 0;
    }

  /* copy current absolute path to a local variable. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  glob_path[FSAL_MAX_PATH_LEN - 1] = '\0';

  /* retrieve handle to the file whose permissions are to be tested */
  if((rc =
     cache_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->pentry, &obj_hdl,
                     output)))
    return rc;

  switch( obj_hdl->internal_md.type )
    {
    case REGULAR_FILE:
      pfsal_handle = &obj_hdl->object.file.handle;
      break;

    case SYMBOLIC_LINK:
      pfsal_handle = &obj_hdl->object.symlink->handle;
      break;

    case FS_JUNCTION:
    case DIRECTORY:
      pfsal_handle = &obj_hdl->object.dir.handle;
      break;

    case CHARACTER_FILE:
    case BLOCK_FILE:
    case SOCKET_FILE:
    case FIFO_FILE:
      pfsal_handle = &obj_hdl->object.special_obj.handle;
      break;

    case UNASSIGNED:
    case RECYCLED:
      fprintf(output, "invalidate: unknown pentry type : %u\n",  obj_hdl->internal_md.type );
      return -1 ;
      break;
    }

  if( ( context->cache_status = cache_inode_invalidate( pfsal_handle,
                                                        &attr,
                                                        ht,
                                                        &context->client,
                                                        &context->cache_status) ) != CACHE_INODE_SUCCESS )
    {
      fprintf(output, "Error executing cache_inode_invalidate: %d\n", context->cache_status);
      return -1 ;
    }

  if(flag_v)
    {
      fprintf(output, "Entry %p has been invalidated\n", obj_hdl);
    }

  return 0;
}                               /* fn_Cache_inode_invalidate */
