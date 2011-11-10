/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "stuff_alloc.h"
#include <string.h>
#include <unistd.h>
#include <libgen.h>             /* basename */
#include <sys/types.h>
#include <sys/stat.h>

#define OP_TESTCONN   1
#define OP_EMPTYDB    2
#define OP_FIND       3
#define OP_POPULATE   4

/* functions related to OP_FIND */
void find(fsal_posixdb_conn * p_conn);
void display_directory(fsal_posixdb_conn * p_conn, posixfsal_handle_t * p_handle_parent,
                       char *basedir);

/* functions related to OP_EMPTYDB */
void emptydb(fsal_posixdb_conn * p_conn);

/* functions related to OP_POPULATE */
void populatedb(fsal_posixdb_conn * p_conn, char *path);
void add_dir(fsal_posixdb_conn * p_conn, char *path, posixfsal_handle_t * dir_handle);

/* ---------------------------------------------- */

void populatedb(fsal_posixdb_conn * p_conn, char *path)
{
  int rc;
  fsal_posixdb_fileinfo_t info;
  fsal_name_t fsalname;
  posixfsal_handle_t handle, handle_parent;
  struct stat buffstat;
  char *begin, *end, backup;

  if(path[0] != '/')
    {
      fputs("Error : you should provide a complete path", stderr);
      return;
    }

  if(path[strlen(path) - 1] != '/')
    strcat(path, "/");

  /* add the path (given in arguments) to the database */
  rc = lstat("/", &buffstat);
  fsal_internal_posix2posixdb_fileinfo(&buffstat, &info);
  fsal_internal_posixdb_add_entry(p_conn, NULL, &info, NULL, &handle_parent);

  begin = end = path;
  while(*end != '\0')
    {
      while(*begin == '/')
        begin++;
      if(*begin == '\0')
        break;
      end = begin + 1;
      while(*end != '/' && *end != '\0')
        end++;
      backup = *end;
      *end = '\0';

      rc = lstat(path, &buffstat);
      fsal_internal_posix2posixdb_fileinfo(&buffstat, &info);
      FSAL_str2name(begin, FSAL_MAX_NAME_LEN, &fsalname);
      fsal_internal_posixdb_add_entry(p_conn, &fsalname, &info, &handle_parent, &handle);
      memcpy(&handle_parent, &handle, sizeof(posixfsal_handle_t));

      *end = backup;
      begin = end;
    }

  /* add files */
  printf("Adding entries in %s... ", path);
  fflush(stdout);
  add_dir(p_conn, path, &handle_parent);
  puts("done");
}

void add_dir(fsal_posixdb_conn * p_conn, char *path, posixfsal_handle_t * p_dir_handle)
{
  DIR *dirp;
  struct dirent *dp;
  struct dirent dpe;
  posixfsal_handle_t new_handle;
  struct stat buffstat;
  char path_temp[FSAL_MAX_PATH_LEN];
  fsal_status_t st;
  fsal_posixdb_fileinfo_t info;
  fsal_name_t fsalname;

  if((dirp = opendir(path)))
    {
      while(!readdir_r(dirp, &dpe, &dp) && dp)
        {
          if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
            continue;
          if(!strcmp(dp->d_name, ".snapshot"))
            {
              fputs("(ignoring .snapshot)", stderr);
              continue;
            }
          strcpy(path_temp, path);
          strcat(path_temp, dp->d_name);
          lstat(path_temp, &buffstat);

          fsal_internal_posix2posixdb_fileinfo(&buffstat, &info);
          FSAL_str2name(dp->d_name, FSAL_MAX_NAME_LEN, &fsalname);
          st = fsal_internal_posixdb_add_entry(p_conn, &fsalname, &info, p_dir_handle,
                                               &new_handle);
          if(FSAL_IS_ERROR(st))
            {
              fprintf(stderr, "[Error %i/%i]\n", st.major, st.minor);
              return;
            }
          if(S_ISDIR(buffstat.st_mode))
            {
              strcat(path_temp, "/");
              add_dir(p_conn, path_temp, &new_handle);
            }
        };
      closedir(dirp);
    }
}

void emptydb(fsal_posixdb_conn * p_conn)
{
  fsal_posixdb_status_t st;

  st = fsal_posixdb_flush(p_conn);
  if(FSAL_POSIXDB_IS_ERROR(st))
    {
      fprintf(stderr, "Error (%i/%i) while emptying the database\n", st.major, st.minor);
    }
  else
    {
      printf("Database entries have been successfully deleted\n");
    }

  return;
}

void find(fsal_posixdb_conn * p_conn)
{
  posixfsal_handle_t handle_root;
  fsal_posixdb_status_t st;

  st = fsal_posixdb_getInfoFromName(p_conn, NULL,       /* parent handle */
                                    NULL,       /* filename */
                                    NULL,       /* path */
                                    &handle_root);
  if(FSAL_POSIXDB_IS_NOENT(st))
    {
      fputs("Error : Root handle not found. Is the database empty ?", stderr);
      return;
    }
  else if(FSAL_POSIXDB_IS_ERROR(st))
    {
      fprintf(stderr, "Error (%i/%i) while getting root handle\n", st.major, st.minor);
      return;
    }

  display_directory(p_conn, &handle_root, "");
  return;
}

void display_directory(fsal_posixdb_conn * p_conn, posixfsal_handle_t * p_handle_parent,
                       char *basedir)
{
  fsal_posixdb_child *p_children;
  fsal_posixdb_status_t st;
  unsigned int count, i;

  st = fsal_posixdb_getChildren(p_conn, p_handle_parent, 0, &p_children, &count);
  if(FSAL_POSIXDB_IS_ERROR(st))
    {
      fprintf(stderr, "Error (%i/%i) while getting children of %s\n", st.major, st.minor,
              basedir);
      return;
    }
  for(i = 0; i < count; i++)
    {
      printf("%llu %s/%s\n", (unsigned long long int)p_children[i].handle.data.info.inode,
             basedir, p_children[i].name.name);
      if(p_children[i].handle.data.info.ftype == FSAL_TYPE_DIR)
        {
          char basedir_new[FSAL_MAX_PATH_LEN];

          memset( basedir_new, 0, FSAL_MAX_PATH_LEN ) ;
          strncpy(basedir_new, basedir, FSAL_MAX_PATH_LEN);
          strncat(basedir_new, "/", FSAL_MAX_PATH_LEN);
          strncat(basedir_new, p_children[i].name.name, FSAL_MAX_PATH_LEN);
          display_directory(p_conn, &(p_children[i].handle), basedir_new);
        }
    }
  Mem_Free(p_children);
}

int main(int argc, char **argv)
{
  fsal_posixdb_conn_params_t dbparams;
  char exec_name[MAXPATHLEN];
  char c, op = 0;
  fsal_posixdb_conn *p_conn;
  fsal_posixdb_status_t statusdb;
  char path[MAXPATHLEN];
  int rc;

  char options[] = "h@H:P:L:D:K:";
  char usage[] =
      "Usage: %s [-h][-H <host>][-P <port>][-L <login>][-D <dbname>][-K <passwd file>] operation operation_parameters\n"
      "\t[-h]               display this help\n"
      "\t[-H <host>]        Database host\n"
      "\t[-P <port>]        Database port\n"
      "\t[-L <login>]       Database login\n"
      "\t[-D <dbname>]      Name of the database\n"
      "\t[-K <passwd file>] Path of the file where is stored the password\n"
      "------------- Default Values -------------\n"
      "host        : localhost\n"
      "port        : default DB port\n"
      "dbname      : posixdb\n"
      "login       : current unix user\n"
      "passwd file : default path ($PGPASSFILE)\n"
      "------------- Operations -----------------\n"
      "test_connection       : try to connect to the database\n"
      "empty_database        : Delete all entries in the database\n"
      "find                  : Print the entries of the database (as 'find' would do it)\n"
      "populate <path>       : Add (recursively) the object in <path> into the database\n\n";

  memset(&dbparams, 0, sizeof(fsal_posixdb_conn_params_t));
  strcpy(dbparams.host, "localhost");
  strcpy(dbparams.dbname, "posixdb");

  /* What is the executable file's name */
  if(*exec_name == '\0')
    strcpy((char *)exec_name, basename(argv[0]));

  /* now parsing options with getopt */
  while((c = getopt(argc, argv, options)) != EOF)
    {
      switch (c)
        {
        case '@':
          /* A litlle backdoor to keep track of binary versions */
          printf("%s compiled on %s at %s\n", exec_name, __DATE__, __TIME__);
          exit(0);
          break;
        case 'H':
          strncpy(dbparams.host, optarg, FSAL_MAX_DBHOST_NAME_LEN);
          break;
        case 'P':
          strncpy(dbparams.port, optarg, FSAL_MAX_DBPORT_STR_LEN);
          break;
        case 'L':
          strncpy(dbparams.login, optarg, FSAL_MAX_DB_LOGIN_LEN);
          break;
        case 'D':
          strncpy(dbparams.dbname, optarg, FSAL_MAX_DB_NAME_LEN);
          break;
        case 'K':
          strncpy(dbparams.passwdfile, optarg, PATH_MAX);
          break;
        default:
          /* display the help */
          fprintf(stderr, usage, exec_name);
          exit(0);
          break;
        }
    }

  if(optind == argc)
    {
      fprintf(stderr, "No operation specified.\n");
      fprintf(stderr, usage, exec_name);
      exit(0);
    }
  if(optind < argc)
    {
      if(!strcmp(argv[optind], "test_connection"))
        {
          op = OP_TESTCONN;
        }
      else if(!strcmp(argv[optind], "empty_database"))
        {
          op = OP_EMPTYDB;
        }
      else if(!strcmp(argv[optind], "find"))
        {
          op = OP_FIND;
        }
      else if(!strcmp(argv[optind], "populate"))
        {
          op = OP_POPULATE;
          optind++;
          if(optind < argc)
            {
              strncpy(path, argv[optind], MAXPATHLEN);
            }
          else
            {
              fputs("Operation 'populate' need a parameter", stderr);
              fprintf(stderr, usage, exec_name);
              exit(-1);
            }
        }
      else
        {
          fprintf(stderr, "Unknown operation : %s\n", argv[optind]);
          fprintf(stderr, usage, exec_name);
          exit(-1);
        }
    }
#ifndef _NO_BUDDY_SYSTEM
  BuddyInit(NULL);
#endif

  /* Connecting to database */
  if(*(dbparams.passwdfile) != '\0')
    {
      rc = setenv("PGPASSFILE", dbparams.passwdfile, 1);
      if(rc != 0)
        fputs("Could not set POSTGRESQL keytab path.", stderr);
    }

  fprintf(stderr, "Opening database connection to %s...\n", dbparams.host);
  statusdb = fsal_posixdb_connect(&dbparams, &p_conn);
  if(FSAL_POSIXDB_IS_ERROR(statusdb))
    {
      fprintf(stderr, "Error %i. exiting.\n", statusdb.minor);
      exit(-1);
    }
  else
    {
      fprintf(stderr, "Connected.\n");
    }

  /* Execute the operation */
  switch (op)
    {
    case OP_TESTCONN:
      /* nothing to do */
      break;
    case OP_EMPTYDB:
      emptydb(p_conn);
      break;
    case OP_FIND:
      find(p_conn);
      break;
    case OP_POPULATE:
      populatedb(p_conn, path);
      break;
    default:
      puts("Bad operation !!");
      fprintf(stderr, usage, exec_name);
    }

  fsal_posixdb_disconnect(p_conn);

  exit(0);
}
