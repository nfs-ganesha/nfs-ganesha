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
 * \date    $Date: 2006/02/17 13:37:44 $
 * \version $Revision: 1.54 $
 * \brief   Converts user's commands to MFSL commands.
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <pwd.h>
#include "fsal.h"
#include "mfsl.h"
#include "log_macros.h"
#include "err_ghost_fs.h"
#include "config_parsing.h"
#include "cmd_tools.h"
#include "commands.h"
#include "Getopt.h"
#include "stuff_alloc.h"

static pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;

static int is_loaded = FALSE;   /* filsystem initialization status */

typedef struct cmdmfsl_thr_info__
{
  int is_thread_ok;             /* per thread initialization status */
  mfsl_object_t current_dir;    /* current directory handle */
  char current_path[FSAL_MAX_PATH_LEN]; /* current path */

  /* thread's context */
  fsal_op_context_t context;
  mfsl_context_t mcontext;

  /* export context : on for each thread,
   * on order to make it possible for them
   * to access different filesets.
   */
  fsal_export_context_t exp_context;
  int opened;                   /* is file opened ? */
  mfsl_file_t current_fd;       /* current file descriptor */

} cmdmfsl_thr_info_t;

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
 * GetMFSLCmdContext :
 * manages pthread_keys.
 */
cmdmfsl_thr_info_t *GetMFSLCmdContext()
{

  cmdmfsl_thr_info_t *p_current_thread_vars;

  /* first, we init the keys if this is the first time */
  if(pthread_once(&once_key, init_keys) != 0)
    {
      printf("Error %d calling pthread_once for thread %p : %s\n",
             errno, (caddr_t) pthread_self(), strerror(errno));
      return NULL;
    }

  p_current_thread_vars = (cmdmfsl_thr_info_t *) pthread_getspecific(thread_key);

  /* we allocate the thread context if this is the first time */
  if(p_current_thread_vars == NULL)
    {

      /* allocates thread structure */
      p_current_thread_vars =
          (cmdmfsl_thr_info_t *) Mem_Alloc(sizeof(cmdmfsl_thr_info_t));

      /* panic !!! */
      if(p_current_thread_vars == NULL)
        {
          printf("%p:commands_FSAL: Not enough memory\n", (caddr_t) pthread_self());
          return NULL;
        }

      /* Clean thread context */

      memset(p_current_thread_vars, 0, sizeof(cmdmfsl_thr_info_t));

      p_current_thread_vars->is_thread_ok = FALSE;
      strcpy(p_current_thread_vars->current_path, "");
      p_current_thread_vars->opened = FALSE;

      /* set the specific value */
      pthread_setspecific(thread_key, (void *)p_current_thread_vars);

    }

  return p_current_thread_vars;

}                               /* GetMFSLCmdContext */

/**
 *  Initialize thread specific FSAL environment.
 */
int Init_Thread_MFSL(FILE * output, cmdmfsl_thr_info_t * context, int flag_v)
{

  uid_t uid;
  fsal_status_t st;
  mfsl_object_t hdl_dir;
  char buff[2 * sizeof(fsal_handle_t) + 1];
  struct passwd *pw_struct;

  /* for the moment, create export context for root fileset */
  st = FSAL_BuildExportContext(&context->exp_context, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_BuildExportContext:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* get user's credentials */

  st = FSAL_InitClientContext(&context->context);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_InitClientContext:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  uid = getuid();
  pw_struct = getpwuid(uid);

  if(pw_struct == NULL)
    {
      fprintf(output, "Unknown uid %u\n", uid);
      return errno;
    }

  st = FSAL_GetClientContext(&context->context, &context->exp_context,
                             uid, pw_struct->pw_gid, NULL, 0);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_GetUserCred:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* get MFSL_Context */
  st = MFSL_GetContext(&context->mcontext, &context->context);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing MFSL_GetContext:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* get root file handle */

  /* lookup */

  st = MFSL_lookup(NULL, NULL, &context->context, &context->mcontext, &hdl_dir, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {

      fprintf(output, "Error executing MFSL_lookup:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;

    }

  /* save root handle */

  context->current_dir = hdl_dir;
  context->is_thread_ok = TRUE;

  strcpy(context->current_path, "/");

  snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &context->current_dir.handle);
  if(flag_v)
    fprintf(output, "Current directory is \"%s\" (@%s)\n", context->current_path, buff);

  return 0;

}

void mfsl_layer_SetLogLevel(int log_lvl)
{
  /* mutex pour proteger le descriptor de log */
  pthread_mutex_lock(&mutex_log);

  SetComponentLogLevel(COMPONENT_MFSL, log_lvl);

  pthread_mutex_unlock(&mutex_log);
}

static void getopt_init()
{
  Opterr = 0;                   /* disables Getopt error message */
  /* reinits Getopt processing */
  Optind = 1;
}

int mfsl_init(char *filename, int flag_v, FILE * output)
{
  config_file_t config_file;

  /* FSAL init parameters */
  mfsl_parameter_t init_param;
  fsal_status_t st;

  /* thread context */
  cmdmfsl_thr_info_t *context;

  /* Initializes the FSAL */

  /* use FSAL error family. */

  AddFamilyError(ERR_MFSL, "MFSL related Errors", tab_errstatus_MFSL);
  AddFamilyError(ERR_POSIX, "POSIX Errors", tab_systeme_status);

  /* set configuration defaults */
  MFSL_SetDefault_parameter(&init_param);

  /* Parse config file */

  config_file = config_ParseFile(filename);

  if(!config_file)
    {
      fprintf(output, "init_fs: Error parsing %s: %s\n", filename, config_GetErrorMsg());
      return -1;
    }

  /* Load FSAL configuration from file configuration */

  st = MFSL_load_parameter_from_conf(config_file, &init_param);

  if(FSAL_IS_ERROR(st))
    {
      if(st.major == ERR_FSAL_NOENT)
        {
#if defined( _USE_MFSL_ASYNC )
          fprintf(output, "Missing MFSL_Async stanza in config file\n");
#elif defined( _USE_MFSL_PROXY_RPCSECGSS )
          fprintf(output, "Missing MFSL_Proxy_RrpSeGss  stanza in config file\n");
#else
          fprintf(output, "Missing MFSL stanza in config file\n");
#endif
        }
      else
        {
          fprintf(output, "Error executing MFSL_load_FSAL_parameter_from_conf:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;
        }
    }

  /* Free config struct */
  config_Free(config_file);

  /* Initialization */

  if(flag_v)
    fprintf(output, "MFSL initialization...\n");

  st = MFSL_Init(&init_param);

  if(FSAL_IS_ERROR(st))
    {

      fprintf(output, "Error executing MFSL_Init:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;

    }

  is_loaded = TRUE;

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, flag_v);
      if(rc != 0)
        return rc;
    }

  return 0;
}

/** proceed an init_fs command. */
int fn_mfsl_init(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output) /* IN : output stream          */
{
  int rc;

  static char format[] = "hv";

  static char help_init[] =
      "usage: init_fs [options] <ganesha_config_file>\n"
      "options :\n" "\t-h print this help\n" "\t-v verbose mode\n";

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  char *filename = NULL;

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'v':
          if(flag_v)
            fprintf(output,
                    "init_fs: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "init_fs: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "init_fs: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

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
    {
      filename = argv[Optind];
    }

  if(err_flag)
    {
      fprintf(output, help_init);
      return -1;
    }

  rc = mfsl_init(filename, flag_v, output);

  return rc;
}

/** prints current path */

int fn_mfsl_pwd(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    )
{

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  {
    char buff[2 * sizeof(fsal_handle_t) + 1];
    snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &context->current_dir.handle);

    fprintf(output, "Current directory is \"%s\" (@%s)\n", context->current_path, buff);

  }

  return 0;

}

/* solves a relative or absolute path */

int msfl_solvepath(char *io_global_path, int size_global_path,  /* [IN-OUT] global path     */
                   char *i_spec_path,   /* [IN] user specified path */
                   mfsl_object_t i_current_handle,      /* [IN] current directory handle   */
                   mfsl_object_t * new_handle,  /* [OUT] target object handle      */
                   FILE * output)
{

  char str_path[FSAL_MAX_PATH_LEN];
  cmdmfsl_thr_info_t *context;

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* local copy */
  memset( str_path, 0, FSAL_MAX_PATH_LEN ) ;
  strncpy(str_path, i_spec_path, FSAL_MAX_PATH_LEN);

  if(str_path[0] == '@')
    /* It is a file handle */
    {
      int rc;

      rc = sscanHandle(new_handle, str_path + 1);

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

      strncpy(io_global_path, str_path, size_global_path);

      return 0;

    }
  else if(str_path[0] == '/')
    /* absolute path, proceed a lookupPath */
    {
      fsal_path_t path;
      fsal_status_t st;
      mfsl_object_t tmp_hdl;

      if(FSAL_IS_ERROR(st = FSAL_str2path(str_path, FSAL_MAX_PATH_LEN, &path)))
        {
          fprintf(output, "Error executing FSAL_str2path:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;
        }

      if(FSAL_IS_ERROR(st = MFSL_lookupPath(&path,
                                            &context->context,
                                            &context->mcontext, &tmp_hdl, NULL)))
        {
          fprintf(output, "Error executing MFSL_lookupPath:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;
        }

      /* cleans path */
      clean_path(str_path, FSAL_MAX_PATH_LEN);

      strncpy(io_global_path, str_path, size_global_path);
      *new_handle = tmp_hdl;

      return 0;

    }
  else
    /* relative path, proceed a step by step lookup */
    {
      fsal_name_t name;
      fsal_status_t st;
      mfsl_object_t old_hdl = i_current_handle;
      mfsl_object_t tmp_hdl;
      char tmp_path[FSAL_MAX_PATH_LEN];
      char *next_name = str_path;
      char *curr = str_path;
      int last = 0;

      tmp_path[0] = '\0';       /* empty string */

      do
        {

          /* tokenize to the next '/' */
          while((*curr != '\0') && (*curr != '/'))
            curr++;

          if(!(*curr))
            last = 1;           /* remembers if it was the last dir */
          *curr = '\0';

          /* build the name */
          if(FSAL_IS_ERROR(st = FSAL_str2name(next_name, FSAL_MAX_PATH_LEN, &name)))
            {
              fprintf(output, "Error executing FSAL_str2name:");
              print_fsal_status(output, st);
              fprintf(output, "\n");
              return st.major;
            }

          /* lookup this name */
          if(FSAL_IS_ERROR(st = MFSL_lookup(&old_hdl,
                                            &name,
                                            &context->context,
                                            &context->mcontext, &tmp_hdl, NULL, NULL)))
            {
              fprintf(output, "Error executing MFSL_lookup:");
              print_fsal_status(output, st);
              fprintf(output, "\n");
              return st.major;
            }

          /* if handles are the same, we are at fileset root,
           * so, don't modify the path.
           * Else, we contatenate them.
           */
          if(FSAL_handlecmp(&old_hdl.handle, &tmp_hdl.handle, &st) != 0)
            {
              /* updates current handle */
              old_hdl = tmp_hdl;

              /* adds /name at the end of the path */
              strncat(tmp_path, "/", FSAL_MAX_PATH_LEN);
              strncat(tmp_path, next_name, FSAL_MAX_PATH_LEN);
            }

          /* updates cursors */
          if(!last)
            {
              curr++;
              next_name = curr;
              /* ignore successive slashes */
              while((*curr != '\0') && (*curr == '/'))
                {
                  curr++;
                  next_name = curr;
                }
              if(!(*curr))
                last = 1;       /* it is the last dir */
            }

        }
      while(!last);

      /* everything is OK, apply changes */

      strncat(io_global_path, tmp_path, size_global_path);
      clean_path(io_global_path, size_global_path);

      *new_handle = old_hdl;

      return 0;

    }

}

/** change current path */
int fn_mfsl_cd(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  static char help_cd[] = "usage: cd <path>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t new_hdl;
  fsal_attrib_list_t attrs;
  fsal_status_t st;
  int rc;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* Exactly one arg expected */
  if(argc != 2)
    {
      fprintf(output, help_cd);
      return -1;
    }

  /* is it a relative or absolute path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    argv[1], context->current_dir, &new_hdl, output))
    return rc;

  /* verify if the object is a directory */
  FSAL_CLEAR_MASK(attrs.asked_attributes);
  FSAL_SET_MASK(attrs.asked_attributes,
                FSAL_ATTR_TYPE | FSAL_ATTR_MODE | FSAL_ATTR_GROUP | FSAL_ATTR_OWNER);

  if(FSAL_IS_ERROR(st = MFSL_getattrs(&new_hdl,
                                      &context->context, &context->mcontext, &attrs, NULL)))
    {
      fprintf(output, "Error executing MFSL_getattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(attrs.type != FSAL_TYPE_DIR)
    {
      fprintf(output, "Error: %s is not a directory\n", glob_path);
      return ENOTDIR;
    }

  if(FSAL_IS_ERROR(st = FSAL_test_access(&context->context, FSAL_X_OK, &attrs)))
    {
      fprintf(output, "Error: %s: permission denied.\n", glob_path);
      return st.major;
    }

/*  if (FSAL_IS_ERROR(st = FSAL_access(&new_hdl,&contexte,FSAL_X_OK,&attrs))){
    fprintf(output,"Error: %s: permission denied.\n",);
    return st.major;
  }*/

  /* if so, apply changes */
  strncpy(context->current_path, glob_path, FSAL_MAX_PATH_LEN);
  context->current_dir = new_hdl;

  {
    char buff[2 * sizeof(fsal_handle_t) + 1];
    snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &context->current_dir.handle);

    fprintf(output, "Current directory is \"%s\" (@%s)\n", context->current_path, buff);
  }

  return 0;

}

/** proceed a stat command. */
int fn_mfsl_stat(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_stat[] = "usage: stat [-h][-v] <file>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t new_hdl;
  fsal_attrib_list_t attrs;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  char *file = NULL;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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
        case '?':
          fprintf(output, "stat: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_stat);
      return 0;
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
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    file, context->current_dir, &new_hdl, output))
    return rc;

  /* retrieve supported attributes */
  FSAL_CLEAR_MASK(attrs.asked_attributes);
  FSAL_SET_MASK(attrs.asked_attributes, FSAL_ATTR_SUPPATTR);

  if(FSAL_IS_ERROR(st = MFSL_getattrs(&new_hdl,
                                      &context->context, &context->mcontext, &attrs, NULL)))
    {
      fprintf(output, "Error executing MFSL_getattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* print supported attributes if verbose flag is set */
  if(flag_v)
    {
      fprintf(output, "Supported attributes :\n");
      print_fsal_attrib_mask(attrs.supported_attributes, output);
      fprintf(output, "\nAttributes :\n");
    }

  /* getting all supported attributes */
  attrs.asked_attributes = attrs.supported_attributes;

  if(FSAL_IS_ERROR(st = MFSL_getattrs(&new_hdl,
                                      &context->context, &context->mcontext, &attrs, NULL)))
    {
      fprintf(output, "Error executing MFSL_getattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* print file attributes */
  print_fsal_attributes(attrs, output);

  return 0;

}

/** proceed an ls command. */
int fn_mfsl_ls(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output)   /* IN : output stream          */
{

  static char format[] = "hvdlS";

  static char help_ls[] =
      "usage: ls [options] [name|path]\n"
      "options :\n"
      "\t-h print this help\n"
      "\t-v verbose mode\n"
      "\t-d print directory info instead of listing its content\n"
      "\t-l print standard UNIX attributes\n" "\t-S print all supported attributes\n";

  int option;
  int flag_v = 0;
  int flag_h = 0;
  int flag_d = 0;
  int flag_l = 0;
  int flag_S = 0;
  int err_flag = 0;
  char *str_name;

#define READDIR_SIZE FSAL_READDIR_SIZE

  fsal_attrib_mask_t mask_needed;
  mfsl_object_t obj_hdl;
  fsal_dir_t dir;
  char glob_path[FSAL_MAX_PATH_LEN];
  fsal_attrib_list_t attrs;
  fsal_status_t st;
  fsal_cookie_t from, to;
  fsal_dirent_t entries[READDIR_SIZE];
  fsal_count_t number;
  fsal_boolean_t eod = FALSE;
  fsal_path_t symlink_path;
  int error = FALSE;
  int rc;

  cmdmfsl_thr_info_t *context;

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
        case '?':
          fprintf(output, "ls: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }

    }

  if(flag_l + flag_S > 1)
    {
      fprintf(output, "ls: conflict between options l,S\n");
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

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* prepare needed attributes mask */
  FSAL_CLEAR_MASK(mask_needed);
  FSAL_SET_MASK(mask_needed, FSAL_ATTRS_MANDATORY);

  if(flag_l)
    FSAL_SET_MASK(mask_needed, FSAL_ATTRS_POSIX);
  else if(flag_S)
    mask_needed = 0xFFFFFFFFFFFFFFFFLL;

  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* first, retrieve the argument (if any) */
  if(Optind == (argc - 1))
    {
      str_name = argv[Optind];

      /* retrieving handle */
      if(rc =
         msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                        str_name, context->current_dir, &obj_hdl, output))
        return rc;

    }
  else
    {
      str_name = ".";
      obj_hdl = context->current_dir;
    }

  if(flag_v)
    fprintf(output, "proceeding ls on \"%s\"\n", glob_path);

  FSAL_CLEAR_MASK(attrs.asked_attributes);
  FSAL_SET_MASK(attrs.asked_attributes, FSAL_ATTR_SUPPATTR);

  if(FSAL_IS_ERROR(st = MFSL_getattrs(&obj_hdl,
                                      &context->context, &context->mcontext, &attrs, NULL)))
    {
      fprintf(output, "Error executing MFSL_getattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* getting all needed attributes */
  attrs.asked_attributes = (attrs.supported_attributes & mask_needed);

  if(FSAL_IS_ERROR(st = MFSL_getattrs(&obj_hdl,
                                      &context->context, &context->mcontext, &attrs, NULL)))
    {
      fprintf(output, "Error executing MFSL_getattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /*
   * if the object is a file or a directoy with the -d option specified,
   * we only show its info and exit.
   */
  if((attrs.type != FSAL_TYPE_DIR) || flag_d)
    {

      if((attrs.type == FSAL_TYPE_LNK) && (flag_l))
        {

          if(FSAL_IS_ERROR
             (st =
              MFSL_readlink(&obj_hdl, &context->context, &context->mcontext,
                            &symlink_path, NULL, NULL)))
            {
              fprintf(output, "Error executing FSAL_readlink:");
              print_fsal_status(output, st);
              fprintf(output, "\n");
              return st.major;
            }

        }

      if(flag_l)
        print_item_line(output, &attrs, str_name, symlink_path.path);

      else if(flag_S)
        {

          char tracebuff[2 * sizeof(fsal_handle_t) + 1];
          snprintHandle(tracebuff, 2 * sizeof(fsal_handle_t) + 1, &obj_hdl.handle);
          fprintf(output, "%s (@%s):\n", str_name, tracebuff);
          print_fsal_attributes(attrs, output);

        }
      else                      /* only prints the name */
        fprintf(output, "%s\n", str_name);

      return 0;
    }

  /*
   * the current object is a directory, we have to list its element
   */
  if(FSAL_IS_ERROR(st = MFSL_opendir(&obj_hdl,
                                     &context->context, &context->mcontext, &dir, NULL, NULL)))
    {
      fprintf(output, "Error executing MFSL_opendir:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  FSAL_SET_COOKIE_BEGINNING(from);

  while(!error && !eod)
    {
      fsal_dirent_t *curr;
      char item_path[FSAL_MAX_PATH_LEN];

      if(FSAL_IS_ERROR(st = MFSL_readdir(&dir,
                                         from, attrs.supported_attributes & mask_needed,
                                         READDIR_SIZE * sizeof(fsal_dirent_t),
                                         entries,
                                         &to, &number, &eod, &context->mcontext, NULL)))
        {
          fprintf(output, "Error executing MFSL_readdir:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          error = st.major;
          number = 0;
        }

      if(flag_v)
        fprintf(output, "MFSL_readdir returned %u entries\n", (unsigned int)number);

      if(number > 0)
        {
          curr = entries;
          do
            {
              int len;
              len = strlen(str_name);
              if(!strcmp(str_name, "."))
                strncpy(item_path, curr->name.name, FSAL_MAX_PATH_LEN);
              else if(str_name[len - 1] == '/')
                snprintf(item_path, FSAL_MAX_PATH_LEN, "%s%s", str_name, curr->name.name);
              else
                snprintf(item_path, FSAL_MAX_PATH_LEN, "%s/%s", str_name,
                         curr->name.name);

              if((curr->attributes.type == FSAL_TYPE_LNK) && (flag_l))
                {

                  mfsl_object_t tmp_mfsl;

                  tmp_mfsl.handle = curr->handle;

                  if(FSAL_IS_ERROR
                     (st =
                      MFSL_readlink(&tmp_mfsl, &context->context, &context->mcontext,
                                    &symlink_path, NULL, NULL)))
                    {
                      fprintf(output, "Error executing FSAL_readlink:");
                      print_fsal_status(output, st);
                      fprintf(output, "\n");
                      return st.major;
                    }

                }

              if(flag_l)
                print_item_line(output, &(curr->attributes), item_path,
                                symlink_path.path);

              else if(flag_S)
                {

                  char tracebuff[2 * sizeof(fsal_handle_t) + 1];
                  snprintHandle(tracebuff, 2 * sizeof(fsal_handle_t) + 1, &curr->handle);

                  fprintf(output, "%s (@%s):\n", item_path, tracebuff);
                  print_fsal_attributes(curr->attributes, output);

                }
              else              /* only prints the name */
                fprintf(output, "%s\n", item_path);

            }
          while(curr = curr->nextentry);
        }
      /* preparing next call */
      from = to;

    }

  FSAL_closedir(&dir);

  return error;

}                               /* fn_mfsl_ls */

/** change thread contexte. */
int fn_mfsl_su(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  char *str_uid;
  fsal_uid_t uid;
  fsal_status_t st;
  struct passwd *pw_struct;
  int i;

# define MAX_GRPS  128
  gid_t groups_tab[MAX_GRPS];
  int nb_grp;

  static char help_stats[] = "usage: su <uid>\n";

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* UID arg expected */
  if(argc != 2)
    {
      fprintf(output, help_stats);
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

/** proceed an unlink command. */
int fn_mfsl_unlink(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_unlink[] = "usage: unlink [-h][-v] <path>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t new_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;

  fsal_name_t objname;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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
      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);
    }

  if(err_flag)
    {
      fprintf(output, help_unlink);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    path, context->current_dir, &new_hdl, output))
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

  if(FSAL_IS_ERROR(st = MFSL_unlink(&new_hdl, &objname, NULL,   /* Bad idea, will probably segfault */
                                    &context->context, &context->mcontext, NULL, NULL)))
    {
      fprintf(output, "Error executing MFSL_unlink:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    fprintf(output, "%s/%s successfully unlinked\n", glob_path, file);

  return 0;

}

/** proceed an mkdir command. */
int fn_mfsl_mkdir(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_mkdir[] =
      "usage: mkdir [-h][-v] <path> <mode>\n"
      "       path: path of the directory to be created\n"
      "       mode: octal mode for the directory is to be created (ex: 755)\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t new_hdl, subdir_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode;
  fsal_accessmode_t fsalmode = 0755;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;
  char *strmode;

  fsal_name_t objname;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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

      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

      strmode = argv[Optind + 1];

      /* converting mode string to FSAL mode string */
      mode = atomode(strmode);
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
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    path, context->current_dir, &new_hdl, output))
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

  if(FSAL_IS_ERROR(st = MFSL_mkdir(&new_hdl,
                                   &objname,
                                   &context->context,
                                   &context->mcontext,
                                   fsalmode, &subdir_hdl, NULL, NULL, NULL)))
    {                           /* Will probably segfault */
      fprintf(output, "Error executing MFSL_mkdir:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    {
      char buff[2 * sizeof(fsal_handle_t) + 1];
      snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &subdir_hdl.handle);

      fprintf(output, "%s/%s successfully created (@%s) \n", glob_path, file, buff);

    }

  return 0;

}

/** proceed a rename command. */
int fn_mfsl_rename(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_rename[] = "usage: rename [-h][-v] <src> <dest>\n";

  char src_glob_path[FSAL_MAX_PATH_LEN];
  char tgt_glob_path[FSAL_MAX_PATH_LEN];

  mfsl_object_t src_path_handle, tgt_path_handle;
  fsal_name_t src_name, tgt_name;

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

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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
    fprintf(output, "Renaming %s (dir %s) to %s (dir %s)\n",
            src_file, src_path, tgt_file, tgt_path);

  /* copy current path. */
  strncpy(src_glob_path, context->current_path, FSAL_MAX_PATH_LEN);
  strncpy(tgt_glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves paths handles */
  if(rc =
     msfl_solvepath(src_glob_path, FSAL_MAX_PATH_LEN,
                    src_path, context->current_dir, &src_path_handle, output))
    return rc;

  if(rc =
     msfl_solvepath(tgt_glob_path, FSAL_MAX_PATH_LEN,
                    tgt_path, context->current_dir, &tgt_path_handle, output))
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

  st = MFSL_rename(&src_path_handle,    /* IN */
                   &src_name,   /* IN */
                   &tgt_path_handle,    /* IN */
                   &tgt_name,   /* IN */
                   &context->context,   /* IN */
                   &context->mcontext,  /* IN */
                   NULL, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {

      fprintf(output, "Error executing FSAL_rename:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;

    }

  if(flag_v)
    fprintf(output, "%s/%s successfully renamed to %s/%s\n",
            src_glob_path, src_file, tgt_glob_path, tgt_file);

  return 0;

}

/** proceed an ln command. */
int fn_mfsl_ln(int argc,        /* IN : number of args in argv */
               char **argv,     /* IN : arg list               */
               FILE * output    /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_ln[] =
      "ln: create a symbolic link.\n"
      "usage: ln [-h][-v] <link_content> <link_path>\n"
      "       link_content: content of the symbolic link to be created\n"
      "       link_path: path of the symbolic link to be created\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t path_hdl, link_hdl;
  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *content = NULL;
  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *name;

  fsal_name_t objname;
  fsal_path_t objcontent;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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

      strncpy(tmp_path, argv[Optind + 1], FSAL_MAX_PATH_LEN);
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
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    path, context->current_dir, &path_hdl, output))
    return rc;

  /* create fsal_name_t */
  st = FSAL_str2name(name, 256, &objname);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2name:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* create fsal_path_t */
  st = FSAL_str2path(content, 256, &objcontent);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_str2path:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  st = MFSL_symlink(&path_hdl,  /* IN - parent dir handle */
                    &objname,   /* IN - link name */
                    &objcontent,        /* IN - link content */
                    &context->context,  /* IN - user contexte */
                    &context->mcontext, /* IN - user contexte */
                    0777,       /* IN (ignored) */
                    &link_hdl,  /* OUT - link handle */
                    NULL, NULL);      /* OUT - link attributes */

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_symlink:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    {
      char buff[2 * sizeof(fsal_handle_t) + 1];
      snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &link_hdl.handle);

      fprintf(output, "%s/%s -> %s successfully created (@%s) \n", path, name, content,
              buff);

    }

  return 0;

}

/** proceed a hardlink command. */
int fn_mfsl_hardlink(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_hardlink[] =
      "hardlink: create a hard link.\n"
      "usage: hardlink [-h][-v] <target> <new_path>\n"
      "       target: path of an existing file.\n"
      "       new_path: path of the hardlink to be created\n";

  char glob_path_target[FSAL_MAX_PATH_LEN];
  char glob_path_link[FSAL_MAX_PATH_LEN];

  mfsl_object_t target_hdl, dir_hdl;
  fsal_name_t link_name;

  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *target = NULL;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *name;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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

  /* retrieves path handle for target */
  if(rc =
     msfl_solvepath(glob_path_target, FSAL_MAX_PATH_LEN,
                    target, context->current_dir, &target_hdl, output))
    return rc;

  /* retrieves path handle for parent dir */
  if(rc =
     msfl_solvepath(glob_path_link, FSAL_MAX_PATH_LEN,
                    path, context->current_dir, &dir_hdl, output))
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

  st = MFSL_link(&target_hdl,   /* IN - target file */
                 &dir_hdl,      /* IN - parent dir handle */
                 &link_name,    /* IN - link name */
                 &context->context,     /* IN - user contexte */
                 &context->mcontext,    /* IN - user contexte */
                 NULL, NULL);   /* OUT - new attributes */

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_link:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    {
      fprintf(output, "%s/%s <=> %s successfully created\n", path, name,
              glob_path_target);

    }

  return 0;

}

/** proceed an create command. */
int fn_mfsl_create(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_create[] =
      "usage: create [-h][-v] <path> <mode>\n"
      "       path: path of the file to be created\n"
      "       mode: octal access mode for the file to be created (ex: 644)\n";

  char glob_path_dir[FSAL_MAX_PATH_LEN];
  mfsl_object_t dir_hdl, file_hdl;

  fsal_status_t st;
  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;
  int mode;
  fsal_accessmode_t fsalmode = 0644;

  char tmp_path[FSAL_MAX_PATH_LEN];
  char *path;
  char *file;
  char *strmode;

  fsal_name_t objname;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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

      strncpy(tmp_path, argv[Optind], FSAL_MAX_PATH_LEN);
      split_path(tmp_path, &path, &file);

      strmode = argv[Optind + 1];

      /* converting mode string to FSAL mode string */
      mode = atomode(strmode);
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
  strncpy(glob_path_dir, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves path handle */
  if(rc =
     msfl_solvepath(glob_path_dir, FSAL_MAX_PATH_LEN,
                    path, context->current_dir, &dir_hdl, output))
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

  st = MFSL_create(&dir_hdl,    /* IN */
                   &objname,    /* IN */
                   &context->context,   /* IN */
                   &context->mcontext,  /* IN */
                   fsalmode,    /* IN */
                   &file_hdl,   /* OUT */
                   /*&context->current_fd, *//* OUT */
                   NULL /* [ IN/OUT ] */ ,
                   NULL, NULL         /* will probably segfault */
      );

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_create:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    {
      char buff[2 * sizeof(fsal_handle_t) + 1];
      snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &file_hdl.handle);

      fprintf(output, "%s/%s successfully created (@%s) \n", glob_path_dir, file, buff);

    }

  return 0;

}

/* setattr
 *
 * syntax of command line:
 * setattr file_path  attribute_name  attribute_value
 *
 */
int fn_mfsl_setattr(int argc,   /* IN : number of args in argv */
                    char **argv,        /* IN : arg list               */
                    FILE * output       /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_setattr[] =
      "usage: setattr [-h][-v] <path> <attr>=<value>,<attr>=<value>,...\n";

  char glob_path[FSAL_MAX_PATH_LEN];    /* absolute path of the object */
  mfsl_object_t obj_hdl;        /* handle of the object */
  fsal_attrib_list_t set_attrs; /* attributes to be setted */
  fsal_status_t st;             /* FSAL return status */

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */
  char *attr_list = NULL;       /* attribute list */

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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
            }
        }

      return 0;
    }

  /* Exactly 2 args expected (path and attributes) */

  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
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

  rc = msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->current_dir,
                      &obj_hdl, output);
  if(rc)
    return rc;

  /* Convert the peers (attr_name,attr_val) to an FSAL attribute structure. */
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

  st = MFSL_setattrs(&obj_hdl, &context->context, &context->mcontext, &set_attrs, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_setattrs:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  return 0;

}

/**
 * perform an access command.
 * syntax: access [-A] [F][R][W][X] <file>
 * example: access toto FRX
 */

int fn_mfsl_access(int argc,    /* IN : number of args in argv */
                   char **argv, /* IN : arg list               */
                   FILE * output        /* IN : output stream          */
    )
{

  static char format[] = "hvA";

  static char help_access[] =
      "usage: access [-h][-v][-A] <rights> <path>\n"
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
  mfsl_object_t obj_hdl;        /* handle of the object */
  fsal_accessflags_t test_perms;        /* permissions to be tested */
  fsal_status_t st;             /* FSAL return status */

  int rc, option;
  unsigned int i;
  int flag_v = 0;
  int flag_h = 0;
  int flag_A = 0;
  int err_flag = 0;

  char *file = NULL;            /* the relative path to the object */
  char *str_perms = NULL;       /* string that represents the permissions to be tested */

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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

        case 'A':
          if(flag_A)
            fprintf(output,
                    "access: warning: option 'A' has been specified more than once.\n");
          else
            flag_A++;
          break;

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

  rc = msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN, file, context->current_dir, &obj_hdl,
                      output);
  if(rc)
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

  if(flag_A)
    {
      fsal_attrib_list_t attributes;

      /* 1st method: get attr and test_access */

      FSAL_CLEAR_MASK(attributes.asked_attributes);
      FSAL_SET_MASK(attributes.asked_attributes,
                    FSAL_ATTR_MODE | FSAL_ATTR_OWNER | FSAL_ATTR_GROUP | FSAL_ATTR_ACL);

      if(flag_v)
        fprintf(output, "Getting file attributes...\n");

      st = MFSL_getattrs(&obj_hdl, &context->context, &context->mcontext, &attributes, NULL);

      if(FSAL_IS_ERROR(st))
        {
          fprintf(output, "Error executing MFSL_getattrs:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;
        }

      if(flag_v)
        {
          print_fsal_attributes(attributes, output);
        }

      if(flag_v)
        fprintf(output, "Testing access rights...\n");

      st = FSAL_test_access(&context->context, test_perms, &attributes);

      if(FSAL_IS_ERROR(st))
        {

          fprintf(output, "Error executing FSAL_test_access:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;

        }
      else
        {

          fprintf(output, "access: Access granted.\n");
          return 0;

        }

    }
  else
    {
      /* 2nd method: simply calling access */

      if(flag_v)
        fprintf(output, "Calling access\n");

      st = MFSL_access(&obj_hdl, &context->context, &context->mcontext, test_perms, NULL, NULL);

      if(FSAL_IS_ERROR(st))
        {

          fprintf(output, "Error executing FSAL_access:");
          print_fsal_status(output, st);
          fprintf(output, "\n");
          return st.major;

        }
      else
        {

          fprintf(output, "access: Access granted.\n");
          return 0;
        }

    }

}

/** proceed a truncate command. */
int fn_mfsl_truncate(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_truncate[] = "usage: truncate [-h][-v] <file> <size>\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t filehdl;

  fsal_status_t st;
  fsal_size_t trunc_size;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  char *file = NULL;
  char *str_size = NULL;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
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
                    "truncate: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "truncate: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "truncate: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_truncate);
      return 0;
    }

  /* Exactly two arg expected */
  if(Optind != (argc - 2))
    {
      err_flag++;
    }
  else
    {
      file = argv[Optind];
      str_size = argv[Optind + 1];

      rc = ato64(str_size, &trunc_size);
      if(rc == -1)
        {
          fprintf(output, "truncate: error: invalid trunc size \"%s\"\n", str_size);
          err_flag++;
        }

    }

  if(err_flag)
    {
      fprintf(output, help_truncate);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves object handle */
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    file, context->current_dir, &filehdl, output))
    return rc;

  if(flag_v)
    fprintf(output, "Truncating \"%s\" to %llu bytes.\n", glob_path, trunc_size);

  st = MFSL_truncate(&filehdl, &context->context, &context->mcontext, trunc_size, NULL, /* Will fail with FSAL_PROXY */
                     NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_truncate:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  if(flag_v)
    fprintf(output, "Truncate operation completed sucessfully.\n");

  return 0;

}

/**
 *  Command that opens a file using specific flags.
 *  open <path> [ rwat ]
 */
int fn_mfsl_open(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{

  static char format[] = "hv";

  static char help_open[] =
      "usage: open [-h][-v] <path> [<oflags>]\n"
      "   where <oflags> is a set of the following values:\n"
      "   'r': read, 'w': write, 'a': append, 't': truncate.\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t filehdl;

  fsal_status_t st;

  int rc, option;
  int flag_v = 0;
  int flag_h = 0;
  int err_flag = 0;

  fsal_openflags_t o_flags;

  int flag_r = 0;
  int flag_w = 0;
  int flag_a = 0;
  int flag_t = 0;

  char *file = NULL;
  char *opt_str;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* is a file already opened ? */
  if(context->opened)
    {
      fprintf(output, "Error: a file is already opened. Use 'close' command first.\n");
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
                    "open: warning: option 'v' has been specified more than once.\n");
          else
            flag_v++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "open: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "open: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_open);
      return 0;
    }

  /* one or two args expected */
  if(Optind > (argc - 1))
    err_flag++;
  else
    {
      file = argv[Optind];

      Optind++;

      /* optional flags */
      while(Optind < argc)
        {
          /* test flags */
          opt_str = argv[Optind];

          while(*opt_str)
            {
              switch (*opt_str)
                {
                case 'r':
                case 'R':
                  flag_r++;
                  break;

                case 'w':
                case 'W':
                  flag_w++;
                  break;

                case 'a':
                case 'A':
                  flag_a++;
                  break;

                case 't':
                case 'T':
                  flag_t++;
                  break;

                default:
                  fprintf(output, "open: unknown open flag : '%c'\n", *opt_str);
                  err_flag++;
                }
              opt_str++;
            }

          Optind++;
        }

    }

  if(err_flag)
    {
      fprintf(output, help_open);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves object handle */
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    file, context->current_dir, &filehdl, output))
    return rc;

  /* make open flags */

  o_flags = 0;

  if(flag_r && flag_w)
    o_flags |= FSAL_O_RDWR;
  else if(flag_r)
    o_flags |= FSAL_O_RDONLY;
  else if(flag_w)
    o_flags |= FSAL_O_WRONLY;

  if(flag_a)
    o_flags |= FSAL_O_APPEND;
  if(flag_t)
    o_flags |= FSAL_O_TRUNC;

  if(flag_v)
    fprintf(output, "Open operation on %s with flags %#X.\n", glob_path, o_flags);

  st = MFSL_open(&filehdl,
                 &context->context,
                 &context->mcontext, o_flags, &context->current_fd, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_open:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* note that a file is opened. */
  context->opened = TRUE;

  if(flag_v)
    fprintf(output, "Open operation completed sucessfully : fd = %d.\n",
            FSAL_FILENO(&(context->current_fd.fsal_file)));

  return 0;

}

/**
 *  Command that reads data from an opened file.
 */
int fn_mfsl_read(int argc,      /* IN : number of args in argv */
                 char **argv,   /* IN : arg list               */
                 FILE * output  /* IN : output stream          */
    )
{

  static char format[] = "hvAXB:s:";

  static char help_read[] =
      "Usage:\n"
      "  read [-h][-v][-A][-X] [-B <block_size> ] [ -s <seek_type>,<offset> ]  { <total_bytes> | all }\n"
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
      "        read -B 1024 -s END,-2048 all   \n";

  fsal_status_t st;

  int rc, option;

  int flag_v = 0;
  int flag_h = 0;
  int flag_A = 0;
  int flag_X = 0;
  int flag_B = 0;
  int flag_s = 0;

  int err_flag = 0;

  char *str_block_size = NULL;

  char str_seek_buff[256];

  char *str_seek_type = NULL;
  char *str_seek_offset = NULL;
  char *str_total_bytes = NULL;

  fsal_size_t block_size = 1024;        /* default: 1ko */
  fsal_size_t total_bytes = 0;  /* 0 == read all */
  fsal_seek_t seek_desc = { FSAL_SEEK_CUR, 0 }; /* default: read current position */

  fsal_seek_t *p_seek_desc = NULL;

  /* fsal arguments */

  fsal_boolean_t is_eof = 0;
  fsal_size_t total_nb_read = 0;
  fsal_size_t once_nb_read = 0;
  fsal_size_t nb_block_read = 0;

  char *p_read_buff;

  struct timeval timer_start;
  struct timeval timer_stop;
  struct timeval timer_diff;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* is a file opened ? */
  if(!context->opened)
    {
      fprintf(output, "Error: no opened file. Use 'open' command first.\n");
      return -1;
    }

  /* option analysis. */

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
      fprintf(output, help_read);
      return 0;
    }

  /* Exactly one arg expected */

  if(Optind != (argc - 1))
    err_flag++;
  else
    str_total_bytes = argv[Optind];

  if(err_flag)
    {
      fprintf(output, help_read);
      return -1;
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

      p_seek_desc = &seek_desc;

    }
  else
    {
      p_seek_desc = NULL;       /* default seeking */
    }

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
              (p_seek_desc
               ? (seek_desc.whence == FSAL_SEEK_SET ? "SET" : seek_desc.whence ==
                  FSAL_SEEK_CUR ? "CUR" : "END") : "DEFAULT"),
              (p_seek_desc ? seek_desc.offset : 0LL), total_bytes);
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

  gettimeofday(&timer_start, NULL) ;

  /* while EOF is not reached, and read<asked (when total_bytes!=0) */
  while(!is_eof && !((total_bytes != 0) && (total_nb_read >= total_bytes)))
    {

      st = MFSL_read(&context->current_fd,
                     p_seek_desc,
                     block_size,
                     (caddr_t) p_read_buff, &once_nb_read, &is_eof, &context->mcontext, NULL);

      if(FSAL_IS_ERROR(st))
        {
          fprintf(output, "Error executing FSAL_read:");
          print_fsal_status(output, st);
          fprintf(output, "\n");

          /* exit only if it is not retryable */
          if(fsal_is_retryable(st))
            {
              sleep(1);
              continue;
            }
          else
            {
              Mem_Free(p_read_buff);
              return st.major;
            }
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

      /* what ever seek type was, we continue reading from current position */
      p_seek_desc = NULL;

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
}

/**
 *  Command that writes data to an opened file.
 *
 *  Usage:
 *    write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -A <ascii_string>
 *    write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -X <hexa_data>
 *  Where:
 *    <seek_type> can be: SET, CUR, END
 *    <offset> is a signed number of bytes.
 *    <nb_times> is the number of times we write the expression into the file.
 *
 *    <ascii_string> is a string to be written to file.
 *        Note that the null terminating character of is also written
 *        to file.
 *  or
 *    <hexa_data> is a data represented in hexadecimal format,
 *        that is to be written to file.
 *
 *  Examples:
 *
 *    For writing 10 times the null terminated string "hello world"
 *    at the end of the file:
 *          write -s END,0 -N 10 -A "hello world"
 *
 *    For overwriting the beginning of the file with
 *    the pattern 0xA1267AEF31254ADE repeated twice:
 *          write -s SET,0 -N 2 -X "A1267AEF31254ADE"
 */

int fn_mfsl_write(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  static char format[] = "hvs:N:A:X:";

  static char help_write[] =
      "Usage:\n"
      "  write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -A <ascii_string>\n"
      "  write [-h][-v] [ -s <seek_type>,<offset> ]  [-N <nb_times>] -X <hexa_data>\n"
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
      "  For writting 10 times the null terminated string \"hello world\"\n"
      "  at the end of the file:\n"
      "        write -s END,0 -N 10 -A \"hello world\"\n"
      "\n"
      "  For overwritting the beginning of the file with\n"
      "  the pattern 0xA1267AEF31254ADE repeated twice:\n"
      "        write -s SET,0 -N 2 -X \"A1267AEF31254ADE\"\n";

  fsal_status_t st;

  int rc, option;

  int flag_v = 0;
  int flag_h = 0;
  int flag_N = 0;
  int flag_s = 0;
  int flag_A = 0;
  int flag_X = 0;

  int err_flag = 0;

  char *str_times = NULL;
  char str_seek_buff[256];
  char *str_seek_type = NULL;
  char *str_seek_offset = NULL;

  char *str_hexa = NULL;
  char *str_ascii = NULL;

  size_t datasize = 0;
  char *databuff = NULL;

  unsigned long long nb_times = 1;      /* default = 1 */

  fsal_size_t block_size;       /* the length of the data block to be written */

  fsal_u64_t nb_block_written = 0;
  fsal_size_t size_written = 0;
  fsal_size_t size_written_once = 0;

  fsal_seek_t seek_desc = { FSAL_SEEK_CUR, 0 }; /* default: write to current position */

  fsal_seek_t *p_seek_desc = NULL;

  struct timeval timer_start;
  struct timeval timer_stop;
  struct timeval timer_diff;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* is a file opened ? */
  if(!context->opened)
    {
      fprintf(output, "Error: no opened file. Use 'open' command first.\n");
      return -1;
    }

  /* option analysis. */

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
      fprintf(output, help_write);
      return 0;
    }

  /* No extra arg expected */

  if(Optind != argc)
    err_flag++;

  if(!flag_A && !flag_X)
    {
      fprintf(output, "write: error: -A or -X option is mandatory.\n");
      err_flag++;
    }

  if(err_flag)
    {
      fprintf(output, help_write);
      return -1;
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

      p_seek_desc = &seek_desc;

    }
  else
    {
      p_seek_desc = NULL;       /* default seeking */
    }

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
              (p_seek_desc ? (seek_desc.whence == FSAL_SEEK_SET ? "SET" :
                              seek_desc.whence == FSAL_SEEK_CUR ? "CUR" :
                              "END") : "DEFAULT"),
              (p_seek_desc ? seek_desc.offset : 0LL));
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

      st = MFSL_write(&context->current_fd,
                      p_seek_desc,
                      block_size,
                      (caddr_t) databuff, &size_written_once, &context->mcontext, NULL );

      if(FSAL_IS_ERROR(st))
        {
          fprintf(output, "Error executing FSAL_write:");
          print_fsal_status(output, st);
          fprintf(output, "\n");

          /* exit only if it is not retryable */
          if(fsal_is_retryable(st))
            {
              sleep(1);
              continue;
            }
          else
            {
              if(flag_X)
                Mem_Free(databuff);
              return st.major;
            }
        }

      fprintf(output, ".");

      /* update stats */

      if(size_written_once > 0)
        nb_block_written++;

      size_written += size_written_once;

      /* flush */
      if(nb_block_written % 10)
        fflush(output);

      /* what ever seek type was, we continue writting to the current position */
      p_seek_desc = NULL;

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

}

/**
 *  Command that closes a file.
 *  close
 */
int fn_mfsl_close(int argc,     /* IN : number of args in argv */
                  char **argv,  /* IN : arg list               */
                  FILE * output /* IN : output stream          */
    )
{

  static char help_close[] = "usage: close\n";

  fsal_status_t st;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* is a file already opened ? */
  if(!context->opened)
    {
      fprintf(output, "Error: this is no file currently opened.\n");
      return -1;
    }

  if(argc != 1)
    {
      fprintf(output, help_close);
      return -1;
    }

  st = MFSL_close(&context->current_fd, &context->mcontext, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_close:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* note that a file is closed. */
  context->opened = FALSE;

  return 0;

}

/**
 *  Command that prints a file to screen.
 *  cat [-f] <path>
 */
int fn_mfsl_cat(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    )
{

  static char format[] = "hf";

  static char help_cat[] =
      "usage: cat [-h][-f] <path>\n"
      "   -h: print this help\n"
      "   -f: by default, cat doesn't print more that 1MB.\n"
      "       this option force it to print the whole file.\n";

  char glob_path[FSAL_MAX_PATH_LEN];
  mfsl_object_t filehdl;

  fsal_status_t st;

  int rc, option;
  int flag_h = 0;
  int flag_f = 0;
  int err_flag = 0;

  fsal_openflags_t o_flags;
  mfsl_file_t cat_fd;

#define MAX_CAT_SIZE  (1024*1024)
  fsal_size_t nb_read = 0;
  fsal_size_t buffsize = 1024;
  char readbuff[1024];
  int is_eof = 0;

  char *file = NULL;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* analysing options */

  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'f':
          if(flag_f)
            fprintf(output,
                    "cat: warning: option 'f' has been specified more than once.\n");
          else
            flag_f++;
          break;
        case 'h':
          if(flag_h)
            fprintf(output,
                    "cat: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "cat: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }
    }

  if(flag_h)
    {
      fprintf(output, help_cat);
      return 0;
    }

  /* one arg expected */
  if(Optind != (argc - 1))
    err_flag++;
  else
    file = argv[Optind];

  if(err_flag)
    {
      fprintf(output, help_cat);
      return -1;
    }

  /* copy current path. */
  strncpy(glob_path, context->current_path, FSAL_MAX_PATH_LEN);

  /* retrieves object handle */
  if(rc =
     msfl_solvepath(glob_path, FSAL_MAX_PATH_LEN,
                    file, context->current_dir, &filehdl, output))
    return rc;

  /* make open flags */

  o_flags = FSAL_O_RDONLY;

  st = MFSL_open(&filehdl, &context->context, &context->mcontext, o_flags, &cat_fd, NULL, NULL);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_open:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  /* read operations */

  while(!is_eof && (flag_f || (nb_read < MAX_CAT_SIZE)))
    {
      fsal_size_t nb_read_once;

      st = MFSL_read(&cat_fd, NULL, buffsize, (caddr_t) readbuff,
                     &nb_read_once, &is_eof, &context->mcontext, NULL);

      if(FSAL_IS_ERROR(st))
        {
          fprintf(output, "Error executing FSAL_read:");
          print_fsal_status(output, st);
          fprintf(output, "\n");

          /* exit only if it is not retryable */
          if(fsal_is_retryable(st))
            {
              sleep(1);
              continue;
            }
          else
            return st.major;
        }

      fwrite((caddr_t) readbuff, (size_t) nb_read_once, 1, output);

      /* update stats */
      nb_read += nb_read_once;

    }

  MFSL_close(&cat_fd, &context->mcontext, NULL);

  if(!is_eof)
    {
      fprintf(output,
              "\n----------------- File is larger than 1MB (use -f option to display all) -----------------\n");
      return EPERM;
    }

  return 0;

}

/** compare 2 handles. */
int fn_mfsl_handlecmp(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list   */
                      FILE * output     /* IN : output stream */
    )
{
  static char help_handlecmp[] = "usage: handlecmp <obj1> <obj2>\n";

  char glob_path1[FSAL_MAX_PATH_LEN];
  char glob_path2[FSAL_MAX_PATH_LEN];
  char buff[2 * sizeof(fsal_handle_t) + 1];

  mfsl_object_t hdl1, hdl2;
  fsal_status_t st;
  int rc;

  cmdmfsl_thr_info_t *context;

  /* is the fs initialized ? */
  if(!is_loaded)
    {
      fprintf(output, "Error: filesystem not initialized\n");
      return -1;
    }

  /* initialize current thread */

  context = GetMFSLCmdContext();

  if(context->is_thread_ok != TRUE)
    {
      int rc;
      rc = Init_Thread_MFSL(output, context, 0);
      if(rc != 0)
        return rc;
    }

  /* Exactly 2 args expected */
  if(argc != 3)
    {
      fprintf(output, help_handlecmp);
      return -1;
    }

  strncpy(glob_path1, context->current_path, FSAL_MAX_PATH_LEN);
  strncpy(glob_path2, context->current_path, FSAL_MAX_PATH_LEN);

  if(rc =
     msfl_solvepath(glob_path1, FSAL_MAX_PATH_LEN,
                    argv[1], context->current_dir, &hdl1, output))
    return rc;

  if(rc =
     msfl_solvepath(glob_path2, FSAL_MAX_PATH_LEN,
                    argv[2], context->current_dir, &hdl2, output))
    return rc;

  /* it should return :
   *  - 0 if handle are the same
   *    - A non null value else.
   */
  rc = FSAL_handlecmp(&hdl1.handle, &hdl2.handle, &st);

  if(FSAL_IS_ERROR(st))
    {
      fprintf(output, "Error executing FSAL_handlecmp:");
      print_fsal_status(output, st);
      fprintf(output, "\n");
      return st.major;
    }

  snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &hdl1.handle);
  fprintf(output, "%s: handle = @%s\n", argv[1], buff);

  snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &hdl2.handle);
  fprintf(output, "%s: handle = @%s\n", argv[2], buff);

  if(rc == 0)
    {
      fprintf(output, "Handles are identical.\n");
      return rc;
    }
  else
    {
      fprintf(output, "Handles are different.\n");
      return rc;
    }

  return 0;
}
