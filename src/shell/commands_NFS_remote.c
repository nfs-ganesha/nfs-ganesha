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
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _SOLARIS
#include "solaris_port.h"
#define     UDPMSGSIZE      8800        /* rpc imposed limit on udp msg size */
#ifndef _USE_SNMP
typedef unsigned short u_short;
#endif
#endif

#include "rpc.h"
#include <string.h>
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

#include "nfs23.h"
#include "mount.h"

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>
#include <ctype.h>

#define MAXIT 10
#define MAXRETRY 3

#ifdef _APPLE
#define HOST_NAME_MAX          64
#define strndup( s, l ) strdup( s )
#endif

#ifdef _SOLARIS
#define strndup( s, l ) strdup( s )
#endif

/*char *strndup(const char *s, size_t n); */

nfs_parameter_t nfs_param;

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

/* ---------------------- For RPCs----------------------------- */

static struct timeval timeout = { 5, 0 };

typedef struct prog_vers_def__
{
  char *name;
  u_long prog;
  u_long vers;
} prog_vers_def_t;

static prog_vers_def_t progvers_rpcs[] = {
  {"nfs2", NFS_PROGRAM, NFS_V2},
  {"nfs3", NFS_PROGRAM, NFS_V3},
  {"nfs4", NFS4_PROGRAM, NFS_V4},
  {"mount1", MOUNTPROG, MOUNT_V1},
  {"mount3", MOUNTPROG, MOUNT_V3},

  {NULL, 0, 0}
};

typedef struct prog_vers_client_def__
{
  char *name;
  CLIENT *clnt;
  char *hostname;
  char *proto;
  int port;
} prog_vers_client_def_t;

static prog_vers_client_def_t progvers_clnts[] = {
  {"nfs2", NULL, "", "", 0},
  {"nfs3", NULL, "", "", 0},
  {"nfs4", NULL, "", "", 0},
  {"mount1", NULL, "", "", 0},
  {"mount3", NULL, "", "", 0},

  {NULL, NULL, "", "", 0},
};

/** getCLIENT */
CLIENT *getCLIENT(char *name    /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          return clnts->clnt;
        }
      clnts++;
    }
  return NULL;
}                               /* getCLIENT */

/** setCLIENT */
int setCLIENT(char *name,       /* IN */
              CLIENT * clnt     /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          clnts->clnt = clnt;
          return 0;
        }
      clnts++;
    }
  return -1;
}                               /* setCLIENT */

/** getHostname */
char *getHostname(char *name    /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          return clnts->hostname;
        }
      clnts++;
    }
  return "";
}                               /* getHostname */

/** setHostname */
int setHostname(char *name,     /* IN */
                char *hostname  /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          clnts->hostname = (char *)strndup(hostname, HOST_NAME_MAX);
          return 0;
        }
      clnts++;
    }
  return -1;
}                               /* setHostname */

/** getProto */
char *getProto(char *name       /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          return clnts->proto;
        }
      clnts++;
    }
  return "";
}                               /* getProto */

/** setProto */
int setProto(char *name,        /* IN */
             char *proto        /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          clnts->proto = (char *)strndup(proto, 4);
          return 0;
        }
      clnts++;
    }
  return -1;
}                               /* setProto */

/** getPort */
int getPort(char *name          /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          return clnts->port;
        }
      clnts++;
    }
  return 0;
}                               /* getPort */

/** setPort */
int setPort(char *name,         /* IN */
            int port            /* IN */
    )
{
  prog_vers_client_def_t *clnts = progvers_clnts;

  while(clnts->name != NULL)
    {
      if(!strcmp(clnts->name, name))
        {
          clnts->port = port;
          return 0;
        }
      clnts++;
    }
  return -1;
}                               /* setPort */

/* passwd for CLIENT */
struct passwd *current_pw;

/* ------------------------- END ------------------------------ */

/* Global variable: local host name */
static char localmachine[256];

/* info for advanced commands (pwd, ls, cd, ...) */
static int is_mounted_path;

static shell_fh3_t mounted_path_hdl;
static char mounted_path[NFS2_MAXPATHLEN];

static shell_fh3_t current_path_hdl;
static char current_path[NFS2_MAXPATHLEN];

/** rpc_init */
int rpc_init(char *hostname,    /* IN */
             char *name,        /* IN */
             char *proto,       /* IN */
             int port,          /* IN */
             FILE * output      /* IN */
    )
{
  int rc;
  u_long prog = 0;
  u_long vers = 0;

  CLIENT *clnt_res = NULL;
  struct hostent *h;
  struct protoent *p;
  struct sockaddr_in sin;
  int sock;
  struct passwd *pw_struct;
#define MAX_GRPS  128
  gid_t groups_tab[MAX_GRPS];
  int nb_grp;

  prog_vers_def_t *progvers = progvers_rpcs;

  while(progvers->name != NULL)
    {
      if(!strcmp(progvers->name, name))
        {
          prog = progvers->prog;
          vers = progvers->vers;
          //fprintf(output, "(%s) Prog : %d - Vers : %d - Proto : %s\n", name, prog, vers, proto);
          h = gethostbyname(hostname);
          if(h == NULL)
            {
              rpc_createerr.cf_stat = RPC_UNKNOWNHOST;
              fprintf(output, "rpc_init : unknown host %s\n", hostname);
              return (-1);
            }
          if(h->h_addrtype != AF_INET)
            {
              /*
               * Only support INET for now
               */
              rpc_createerr.cf_stat = RPC_SYSTEMERROR;
              rpc_createerr.cf_error.re_errno = EAFNOSUPPORT;
              return (-1);
            }
          memset(&sin, 0, sizeof(sin));

          sin.sin_family = h->h_addrtype;
          sin.sin_port = htons((u_short) port);
          memcpy((char *)&sin.sin_addr, h->h_addr, h->h_length);

          p = getprotobyname(proto);
          if(p == NULL)
            {
              fprintf(output, "rpc_init : protocol %s not found\n", proto);
              return (-1);
            }
          sock = RPC_ANYSOCK;

          switch (p->p_proto)
            {
            case IPPROTO_UDP:
              clnt_res = clntudp_bufcreate(&sin, prog, vers,
                                           timeout, &sock, UDPMSGSIZE, UDPMSGSIZE);
              if(clnt_res == NULL)
                {
                  fprintf(output, "rpc_init : Clntudp_bufcreate failed\n");
                  return (-1);
                }
              break;
            case IPPROTO_TCP:
              clnt_res = clnttcp_create(&sin, prog, vers, &sock, 8800, 8800);
              if(clnt_res == NULL)
                {
                  fprintf(output, "rpc_init : Clnttcp_create failed\n");
                  return (-1);
                }
              break;
            default:
              rpc_createerr.cf_stat = RPC_SYSTEMERROR;
              rpc_createerr.cf_error.re_errno = EPFNOSUPPORT;
              fprintf(output, "rpc_init : unknown protocol %d (%s)\n", p->p_proto, proto);
              return (-1);
            }

          if(current_pw == NULL)
            {                   // first rpc_init
              pw_struct = getpwuid(getuid());
              if(pw_struct == NULL)
                {
                  fprintf(output, "getpwuid failed\n");
                  return -1;
                }
              current_pw = (struct passwd *)malloc(sizeof(struct passwd));
              memcpy(current_pw, pw_struct, sizeof(struct passwd));
            }
          nb_grp =
              getugroups(MAX_GRPS, groups_tab, current_pw->pw_name, current_pw->pw_gid);

          clnt_res->cl_auth =
              authunix_create(localmachine, current_pw->pw_uid, current_pw->pw_gid,
                              nb_grp, groups_tab);
          if(clnt_res->cl_auth == NULL)
            {
              fprintf(stdout, "rpc_init : error during creating Auth\n");
            }

          rc = setCLIENT(name, clnt_res);
          if(rc != 0)
            {
              fprintf(output, "rpc_init : error during setCLIENT\n");
              return rc;
            }
          rc = setHostname(name, hostname);
          if(rc != 0)
            {
              fprintf(output, "rpc_init : error during setHostname\n");
              return rc;
            }
          rc = setProto(name, proto);
          if(rc != 0)
            {
              fprintf(output, "rpc_init : error during setProto\n");
              return rc;
            }
          rc = setPort(name, port);
          if(rc != 0)
            {
              fprintf(output, "rpc_init : error during setPort\n");
              return rc;
            }
          //fprintf(output, "rpc_init : %s client set (%s)\n", name, proto);
          return 0;
        }

      progvers++;
    }

  fprintf(output, "rpc_init : %s: program not found\n", name);
  return -1;
}                               /* rpc_init */

/** rpc_reinit */
int rpc_reinit(char *name,      /* IN */
               FILE * output    /* IN */
    )
{
  int rc;

  int port;
  char *proto;
  char *hostname;

  hostname = getHostname(name);
  //if(strlen(hostname) == 0)
  if(*hostname == '\0')
    {
      fprintf(output, "rpc_reinit client %s : getHostname failed\n", name);
      return -1;
    }
  proto = getProto(name);
  //if(strlen(proto) == 0)
  if( *proto == '\0' )
    {
      fprintf(output, "rpc_reinit client %s : getProto failed\n", name);
      return -1;
    }
  port = getPort(name);

  rc = rpc_init(hostname, name, proto, port, output);
  if(rc != 0)
    {
      fprintf(output, "rpc_reinit failed\n");
      return -1;
    }

  return rc;
}                               /* rpc_reinit */

/** try_rpc_reinit */
int try_rpc_reinit(char *name,  /* IN */
                   int error,   /* IN */
                   FILE * output        /* IN */
    )
{
  unsigned int i;

  for(i = 1; i <= MAXIT; i++)
    {
      if(rpc_reinit(name, output) == 0)
        break;
      if(i >= MAXIT)
        {
          return error;
        }
      sleep(1);
    }
  return 0;
}                               /* try_rpc_reinit */

int switch_result(int result,
                  int i,
                  char *name, char *func_name, char *func_called_name, FILE * output)
{
  int rc = 0;

  switch (result)
    {
    case RPC_SUCCESS:
      break;
    case RPC_CANTRECV:
    case RPC_TIMEDOUT:
      if(i < MAXRETRY)
        {
          rc = try_rpc_reinit(name, rc, output);
          if(rc == 0)
            return -1;
        }
      setCLIENT(name, NULL);
    default:
      fprintf(output, "Error %s (%d) in %s (%s).\n", clnt_sperrno(result), result,
              func_called_name, func_name);
      return result;
      break;
    }
  return 0;
}

/** getopt_init */
static void getopt_init()
{
  /* disables getopt error message */
  Opterr = 0;
  /* reinits getopt processing */
  Optind = 1;
}                               /* getopt_init */

/** fn_rpc_init */
int fn_rpc_init(int argc,       /* IN : number of args in argv */
                char **argv,    /* IN : arg list               */
                FILE * output   /* IN : output stream          */
    )
{
  static char format[] = "h";
  int flag_h = 0;
  int err_flag = 0;
  int option;
  int rc;

  char *hostname = "";
  char *name = "";
  char *proto = "";
  int port = 0;

  const char help_rpc_init[] =
      "usage: rpc_init [options] <hostname> <program_version> <protocol> [<port>]\n"
      "<hostname> : name, localhost, machine.mondomaine.com ...\n"
      "<program> : nfs2 / nfs3 / mount1 / mount3\n"
      "<protocol> : udp / tcp\n" "options :\n" "\t-h print this help\n";

  /* analysing options */
  getopt_init();
  while((option = Getopt(argc, argv, format)) != -1)
    {
      switch (option)
        {
        case 'h':
          if(flag_h)
            fprintf(output,
                    "rpc_init: warning: option 'h' has been specified more than once.\n");
          else
            flag_h++;
          break;
        case '?':
          fprintf(output, "rpc_init: unknown option : %c\n", Optopt);
          err_flag++;
          break;
        }                       /* switch */
    }                           /* while */
  if(flag_h)
    {
      fprintf(output, help_rpc_init);
      return 0;
    }

  /* verifies mandatory argument */
  if(Optind > (argc - 3) || Optind < (argc - 4))
    {
      /* too much or not enough arguments */
      err_flag++;
    }
  else
    {
      hostname = argv[Optind];
      name = argv[Optind + 1];
      proto = argv[Optind + 2];
      port = (argc == Optind + 4) ? atoi(argv[Optind + 3]) : 0;
    }

  if(err_flag)
    {
      fprintf(output, help_rpc_init);
      return -1;
    }

  /* getting the hostname */
  //if(strlen(localmachine) == 0)
  if(*localmachine == '\0' )
    {
      rc = gethostname(localmachine, sizeof(localmachine));
      if(rc != 0)
        {
          fprintf(output, "rpc_init: Error %d while getting hostname.\n", rc);
          return -1;
        }
    }

  rc = rpc_init(hostname, name, proto, port, output);

  return rc;
}                               /* fn_rpc_init */

/* nfs_remote_layer_SetLogLevel */
void nfs_remote_layer_SetLogLevel(int log_lvl)
{

  /* Nothing to do. */
  return;

}                               /* nfs_remote_layer_SetLogLevel */

/** process MNT1 protocol's command. */
int fn_MNT1_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    )
{
  cmdnfsremote_funcdesc_t *funcdesc = mnt1_remote_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {
          do
            {
              clnt = getCLIENT("mount1");
              if(clnt == NULL)
                {
                  fprintf(output, "MOUNT1 client not initialized\n");
                  return -1;
                }

              /* encoding args */

              if(funcdesc->func_encode(CMDNFS_ENCODE,
                                       argc - 1, argv + 1,
                                       0, NULL, (caddr_t) & nfs_arg) == FALSE)
                {
                  fprintf(output, "%s: bad arguments.\n", argv[0]);
                  fprintf(output, "Usage: %s\n", funcdesc->func_help);
                  return -1;
                }

              /* nfs call */

              rc = funcdesc->func_call(clnt, &nfs_arg, &nfs_res);
              rc = switch_result(rc, i, "mount1", argv[0], "fn_MNT1_remote_command",
                                 output);
              if(rc > 0)
                return rc;
              i += 1;
            }
          while(rc == -1);

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
}                               /* fn_MNT1_remote_command */

/** process MNT3 protocol's command. */
int fn_MNT3_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    )
{
  cmdnfsremote_funcdesc_t *funcdesc = mnt3_remote_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {
          do
            {
              clnt = getCLIENT("mount3");
              if(clnt == NULL)
                {
                  fprintf(output, "MOUNT3 client not initialized\n");
                  return -1;
                }

              /* encoding args */

              if(funcdesc->func_encode(CMDNFS_ENCODE,
                                       argc - 1, argv + 1,
                                       0, NULL, (caddr_t) & nfs_arg) == FALSE)
                {
                  fprintf(output, "%s: bad arguments.\n", argv[0]);
                  fprintf(output, "Usage: %s\n", funcdesc->func_help);
                  return -1;
                }

              /* nfs call */

              rc = funcdesc->func_call(clnt, &nfs_arg, &nfs_res);
              rc = switch_result(rc, i, "mount3", argv[0], "fn_MNT3_remote_command",
                                 output);
              if(rc > 0)
                return rc;
              i += 1;
            }
          while(rc == -1);

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

}                               /* fn_MNT3_remote_command */

/** process NFS2 protocol's command. */
int fn_NFS2_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    )
{
  cmdnfsremote_funcdesc_t *funcdesc = nfs2_remote_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {
          do
            {
              clnt = getCLIENT("nfs2");
              if(clnt == NULL)
                {
                  fprintf(output, "NFS2 client not initialized\n");
                  return -1;
                }

              /* encoding args */

              if(funcdesc->func_encode(CMDNFS_ENCODE,
                                       argc - 1, argv + 1,
                                       0, NULL, (caddr_t) & nfs_arg) == FALSE)
                {
                  fprintf(output, "%s: bad arguments.\n", argv[0]);
                  fprintf(output, "Usage: %s\n", funcdesc->func_help);
                  return -1;
                }

              /* nfs call */

              rc = funcdesc->func_call(clnt, &nfs_arg, &nfs_res);
              rc = switch_result(rc, i, "nfs2", argv[0], "fn_NFS2_remote_command",
                                 output);
              if(rc > 0)
                return rc;
              i += 1;
            }
          while(rc == -1);

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

}                               /* fn_NFS2_remote_command */

/** process NFS3 protocol's command. */
int fn_NFS3_remote_command(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
    )
{
  cmdnfsremote_funcdesc_t *funcdesc = nfs3_remote_funcdesc;

  nfs_arg_t nfs_arg;
  nfs_res_t nfs_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  while(funcdesc->func_name != NULL)
    {
      if(!strcmp(funcdesc->func_name, argv[0]))
        {
          do
            {
              clnt = getCLIENT("nfs3");
              if(clnt == NULL)
                {
                  fprintf(output, "NFS3 client not initialized\n");
                  return -1;
                }

              /* encoding args */

              if(funcdesc->func_encode(CMDNFS_ENCODE,
                                       argc - 1, argv + 1,
                                       0, NULL, (caddr_t) & nfs_arg) == FALSE)
                {
                  fprintf(output, "%s: bad arguments.\n", argv[0]);
                  fprintf(output, "Usage: %s\n", funcdesc->func_help);
                  return -1;
                }

              /* nfs call */

              rc = funcdesc->func_call(clnt, &nfs_arg, &nfs_res);
              rc = switch_result(rc, i, "nfs3", argv[0], "fn_NFS3_remote_command",
                                 output);
              if(rc > 0)
                return rc;
              i += 1;
            }
          while(rc == -1);

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

}                               /* fn_NFS3_remote_command */

/*------------------------------------------------------------
 *     Wrapping of NFS calls (used by high level commands)
 *-----------------------------------------------------------*/

/* solves a relative or aboslute path */
int nfs_remote_solvepath(shell_fh3_t * p_mounted_path_hdl,      /* IN - handle of mounted path */
                         char *io_global_path,  /* IN/OUT - global path */
                         int size_global_path,  /* IN - max size for global path */
                         char *i_spec_path,     /* IN - specified path */
                         shell_fh3_t * p_current_hdl,   /* IN - current directory handle */
                         shell_fh3_t * pnew_hdl,        /* OUT - pointer to solved handle */
                         FILE * output  /* IN */
    )
{
  char str_path[NFS2_MAXPATHLEN];
  char *pstr_path = str_path;

  char tmp_path[NFS2_MAXPATHLEN];
  char *next_name;
  char *curr;
  int last = 0;
  int rc;
  unsigned int i;
  CLIENT *clnt;

  shell_fh3_t hdl_lookup;
  nfs_fh3 hdl_param;

  diropargs3 dirop_arg;
  LOOKUP3res lookup_res;

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
      hdl_lookup = *p_mounted_path_hdl;
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
      i = 0;
      /* tokenize to the next '/' */
      while((curr[0] != '\0') && (curr[0] != '/'))
        curr++;

      if(!curr[0])
        last = 1;               /* remembers if it was the last dir */

      curr[0] = '\0';

      /* build the arguments */

      set_nfs_fh3(&dirop_arg.dir, &hdl_lookup);
      dirop_arg.name = next_name;

      /* lookup this name */
      do
        {
          clnt = getCLIENT("nfs3");
          if(clnt == NULL)
            {
              fprintf(output, "NFS3 client not initialized\n");
              return -1;
            }

          rc = nfs3_remote_Lookup(clnt,
                                  (nfs_arg_t *) & dirop_arg, (nfs_res_t *) & lookup_res);
          rc = switch_result(rc, i, "nfs3", "nfs3_remote_Lookup", "nfs_remote_solvepath",
                             output);
          if(rc > 0)
            return rc;
          i += 1;
        }
      while(rc == -1);

      rc = lookup_res.status;
      if(rc != NFS3_OK)
        {
          nfs3_remote_Lookup_Free((nfs_res_t *) & lookup_res);
          fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
          return rc;
        }

      /* updates current handle */
      set_shell_fh3(&hdl_lookup, &lookup_res.LOOKUP3res_u.resok.object);

      nfs3_remote_Lookup_Free((nfs_res_t *) & lookup_res);

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

}                               /* nfs_remote_solvepath */

/** nfs_remote_getattr */
int nfs_remote_getattr(shell_fh3_t * p_hdl,     /* IN */
                       fattr3 * attrs,  /* OUT */
                       FILE * output    /* IN */
    )
{
  GETATTR3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  nfs_fh3 nfshdl;

  set_nfs_fh3(&nfshdl, p_hdl);

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Getattr(clnt, (nfs_arg_t *) & nfshdl, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Getattr", "nfs_remote_getattr",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));

      nfs3_remote_Getattr_Free((nfs_res_t *) & res);
      return rc;
    }

  /* updates current handle */
  *attrs = res.GETATTR3res_u.resok.obj_attributes;

  nfs3_remote_Getattr_Free((nfs_res_t *) & res);

  return 0;
}                               /* nfs_remote_getattr */

/** nfs_remote_access */
int nfs_remote_access(shell_fh3_t * p_hdl,      /* IN */
                      nfs3_uint32 * access_mask,        /* IN/OUT */
                      FILE * output     /* IN */
    )
{
  ACCESS3args arg;
  ACCESS3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing args */
  set_nfs_fh3(&arg.object, p_hdl);
  arg.access = *access_mask;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Access(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Access", "nfs_remote_access",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Access_Free((nfs_res_t *) & res);
      return rc;
    }

  /* updates access mask */
  *access_mask = res.ACCESS3res_u.resok.access;

  nfs3_remote_Access_Free((nfs_res_t *) & res);

  return 0;

}                               /* nfs_remote_access */

/** nfs_remote_readlink */
int nfs_remote_readlink(shell_fh3_t * p_hdl,    /* IN */
                        char *linkcontent,      /* OUT */
                        FILE * output   /* IN */
    )
{
  READLINK3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  nfs_fh3 nfshdl;

  set_nfs_fh3(&nfshdl, p_hdl);

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Readlink(clnt, (nfs_arg_t *) & nfshdl, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Readlink", "nfs_remote_readlink",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Readlink_Free((nfs_res_t *) & res);
      return rc;
    }

  /* copy link content */
  strcpy(linkcontent, res.READLINK3res_u.resok.data);

  nfs3_remote_Readlink_Free((nfs_res_t *) & res);

  return 0;

}                               /* nfs_remote_readlink */

/** nfs_remote_readdirplus */
int nfs_remote_readdirplus(shell_fh3_t * p_dir_hdl,     /* IN */
                           cookie3 cookie,      /* IN */
                           cookieverf3 * p_cookieverf,  /* IN/OUT */
                           dirlistplus3 * dirlist,      /* OUT */
                           nfs_res_t ** to_be_freed,    /* OUT */
                           FILE * output        /* IN */
    )
{
  READDIRPLUS3args arg;
  READDIRPLUS3res *p_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  *to_be_freed = NULL;

  /* args */
  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.cookie = cookie;
  memcpy(&arg.cookieverf, p_cookieverf, sizeof(cookieverf3));
  arg.dircount = 1024;
  arg.maxcount = 4096;

  p_res = (READDIRPLUS3res *) Mem_Alloc(sizeof(READDIRPLUS3res));

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Readdirplus(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) p_res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Readdirplus",
                         "nfs_remote_readdirplus", output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = p_res->status;
  if(rc != NFS3_OK)
    {
      nfs3_remote_Readdirplus_Free((nfs_res_t *) p_res);
      Mem_Free(p_res);
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      return rc;
    }

  memcpy(p_cookieverf, p_res->READDIRPLUS3res_u.resok.cookieverf, sizeof(cookieverf3));

  *dirlist = p_res->READDIRPLUS3res_u.resok.reply;
  *to_be_freed = (nfs_res_t *) p_res;

  return 0;
}                               /* nfs_remote_readdirplus */

/** nfs_remote_readdirplus_free */
void nfs_remote_readdirplus_free(nfs_res_t * to_free)
{
  if(to_free == NULL)
    return;

  nfs3_remote_Readdirplus_Free((nfs_res_t *) to_free);
  Mem_Free(to_free);
}                               /* nfs_remote_readdirplus_free */

/** nfs_remote_readdir */
int nfs_remote_readdir(shell_fh3_t * p_dir_hdl, /* IN */
                       cookie3 cookie,  /* IN */
                       cookieverf3 * p_cookieverf,      /* IN/OUT */
                       dirlist3 * dirlist,      /* OUT */
                       nfs_res_t ** to_be_freed,        /* OUT */
                       FILE * output    /* IN */
    )
{
  READDIR3args arg;
  READDIR3res *p_res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  *to_be_freed = NULL;

  /* args */
  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.cookie = cookie;
  memcpy(&arg.cookieverf, p_cookieverf, sizeof(cookieverf3));
  arg.count = 4096;

  p_res = (READDIR3res *) Mem_Alloc(sizeof(READDIR3res));

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Readdir(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) p_res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Readdir", "nfs_remote_readdir",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = p_res->status;
  if(rc != NFS3_OK)
    {
      nfs3_remote_Readdir_Free((nfs_res_t *) p_res);
      Mem_Free(p_res);
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      return rc;
    }

  memcpy(p_cookieverf, p_res->READDIR3res_u.resok.cookieverf, sizeof(cookieverf3));

  *dirlist = p_res->READDIR3res_u.resok.reply;
  *to_be_freed = (nfs_res_t *) p_res;

  return 0;
}                               /* nfs_remote_readdirplus */

/** nfs_remote_readdir_free */
void nfs_remote_readdir_free(nfs_res_t * to_free)
{
  if(to_free == NULL)
    return;

  nfs3_remote_Readdir_Free((nfs_res_t *) to_free);
  Mem_Free(to_free);
}                               /* nfs_remote_readdir_free */

/** nfs_remote_create */
int nfs_remote_create(shell_fh3_t * p_dir_hdl,  /* IN */
                      char *obj_name,   /* IN */
                      mode_t posix_mode,        /* IN */
                      shell_fh3_t * p_obj_hdl,  /* OUT */
                      FILE * output     /* IN */
    )
{
  CREATE3args arg;
  CREATE3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, p_dir_hdl);
  arg.where.name = obj_name;
  arg.how.mode = GUARDED;

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

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Create(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Create", "nfs_remote_create",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Create_Free((nfs_res_t *) & res);
      return rc;
    }

  /* object handle */
  if(res.CREATE3res_u.resok.obj.handle_follows)
    set_shell_fh3(p_obj_hdl, &res.CREATE3res_u.resok.obj.post_op_fh3_u.handle);
  else
    {
      fprintf(output, "Warning: nfs3_remote_Create did not return file handle.\n");
      nfs3_remote_Create_Free((nfs_res_t *) & res);
      return -1;
    }

  nfs3_remote_Create_Free((nfs_res_t *) & res);

  return 0;

}                               /* nfs_remote_create */

/** nfs_remote_mkdir */
int nfs_remote_mkdir(shell_fh3_t * p_dir_hdl,   /* IN */
                     char *obj_name,    /* IN */
                     mode_t posix_mode, /* IN */
                     shell_fh3_t * p_obj_hdl,   /* OUT */
                     FILE * output      /* IN */
    )
{
  MKDIR3args arg;
  MKDIR3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, p_dir_hdl);
  arg.where.name = obj_name;

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

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Mkdir(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Mkdir", "nfs_remote_mkdir", output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Mkdir_Free((nfs_res_t *) & res);
      return rc;
    }

  /* object handle */
  if(res.MKDIR3res_u.resok.obj.handle_follows)
    set_shell_fh3(p_obj_hdl, &res.MKDIR3res_u.resok.obj.post_op_fh3_u.handle);
  else
    {
      fprintf(output, "Warning: nfs3_remote_Mkdir did not return file handle.\n");
      nfs3_remote_Mkdir_Free((nfs_res_t *) & res);
      return -1;
    }

  nfs3_remote_Mkdir_Free((nfs_res_t *) & res);

  return 0;
}                               /*nfs_remote_mkdir */

/** nfs_remote_rmdir */
int nfs_remote_rmdir(shell_fh3_t * p_dir_hdl,   /* IN */
                     char *obj_name,    /* IN */
                     FILE * output      /* IN */
    )
{
  diropargs3 arg;
  RMDIR3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.name = obj_name;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Rmdir(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Rmdir", "nfs_remote_rmdir", output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Rmdir_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs3_remote_Rmdir_Free((nfs_res_t *) & res);
  return 0;

}                               /* nfs_remote_rmdir */

/** nfs_remote_remove */
int nfs_remote_remove(shell_fh3_t * p_dir_hdl,  /* IN */
                      char *obj_name,   /* IN */
                      FILE * output     /* IN */
    )
{
  diropargs3 arg;
  REMOVE3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.dir, p_dir_hdl);
  arg.name = obj_name;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Remove(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Remove", "nfs_remote_remove",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Remove_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs3_remote_Remove_Free((nfs_res_t *) & res);
  return 0;

}                               /* nfs_remote_remove */

/** nfs_remote_setattr */
int nfs_remote_setattr(shell_fh3_t * p_obj_hdl, /* IN */
                       sattr3 * p_attributes,   /* IN */
                       FILE * output    /* IN */
    )
{
  SETATTR3args arg;
  SETATTR3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.object, p_obj_hdl);
  arg.new_attributes = *p_attributes;
  arg.guard.check = FALSE;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Setattr(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Setattr", "nfs_remote_setattr",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Setattr_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs3_remote_Setattr_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_remote_setattr */

/** nfs_remote_rename */
int nfs_remote_rename(shell_fh3_t * p_src_dir_hdl,      /* IN */
                      char *src_name,   /* IN */
                      shell_fh3_t * p_tgt_dir_hdl,      /* IN */
                      char *tgt_name,   /* IN */
                      FILE * output     /* IN */
    )
{
  RENAME3args arg;
  RENAME3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.from.dir, p_src_dir_hdl);
  arg.from.name = src_name;
  set_nfs_fh3(&arg.to.dir, p_tgt_dir_hdl);
  arg.to.name = tgt_name;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Rename(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Rename", "nfs_remote_rename",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Rename_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs3_remote_Rename_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_remote_rename */

/** nfs_remote_link */
int nfs_remote_link(shell_fh3_t * p_file_hdl,   /* IN */
                    shell_fh3_t * p_tgt_dir_hdl,        /* IN */
                    char *tgt_name,     /* IN */
                    FILE * output       /* IN */
    )
{
  LINK3args arg;
  LINK3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.file, p_file_hdl);
  set_nfs_fh3(&arg.link.dir, p_tgt_dir_hdl);
  arg.link.name = tgt_name;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Link(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Link", "nfs_remote_link", output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      nfs3_remote_Link_Free((nfs_res_t *) & res);
      return rc;
    }

  nfs3_remote_Link_Free((nfs_res_t *) & res);
  return 0;

}                               /*nfs_remote_link */

/** nfs_remote_symlink */
int nfs_remote_symlink(shell_fh3_t path_hdl,    /* IN */
                       char *link_name, /* IN */
                       char *link_content,      /* IN */
                       sattr3 * p_setattr,      /* IN */
                       shell_fh3_t * p_link_hdl,        /* OUT */
                       FILE * output    /* IN */
    )
{
  SYMLINK3args arg;
  SYMLINK3res res;
  int rc;
  unsigned int i = 0;
  CLIENT *clnt;

  /* preparing arguments */

  set_nfs_fh3(&arg.where.dir, &path_hdl);
  arg.where.name = link_name;
  arg.symlink.symlink_attributes = *p_setattr;
  arg.symlink.symlink_data = link_content;

  do
    {
      clnt = getCLIENT("nfs3");
      if(clnt == NULL)
        {
          fprintf(output, "NFS3 client not initialized\n");
          return -1;
        }

      rc = nfs3_remote_Symlink(clnt, (nfs_arg_t *) & arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "nfs3", "nfs3_remote_Symlink", "nfs_remote_symlink",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  rc = res.status;
  if(rc != NFS3_OK)
    {
      fprintf(output, "Error %d in NFSv3 protocol: %s\n", rc, nfsstat3_to_str(rc));
      /* free nfs call resources */
      nfs3_remote_Symlink_Free((nfs_res_t *) & res);
      return rc;
    }

  /* returned handle */
  if(res.SYMLINK3res_u.resok.obj.handle_follows)
    {
      set_shell_fh3(p_link_hdl, &res.SYMLINK3res_u.resok.obj.post_op_fh3_u.handle);
    }
  else
    {
      fprintf(output, "Warning: nfs3_remote_Symlink did not return file handle.\n");
      nfs3_remote_Symlink_Free((nfs_res_t *) & res);
      return -1;
    }

  /* free nfs call resources */
  nfs3_remote_Symlink_Free((nfs_res_t *) & res);

  return 0;

}                               /*nfs_remote_symlink */

/** nfs_remote_mount */
int nfs_remote_mount(char *str_path,    /* IN */
                     shell_fh3_t * p_mnt_hdl,   /* OUT */
                     FILE * output      /* IN */
    )
{
  int rc;
  unsigned int i = 0;
  nfs_arg_t nfs_arg;
  mountres3 res;
  CLIENT *clnt;

  rc = cmdnfs_dirpath(CMDNFS_ENCODE, 1, &str_path, 0, NULL, (caddr_t) & nfs_arg);
  if(rc == FALSE)
    {
      fprintf(output, "nfs_remote_mount : Error during encoding args\n");
      return -1;
    }

  do
    {
      clnt = getCLIENT("mount3");
      if(clnt == NULL)
        {
          fprintf(output, "MOUNT3 client not initialized\n");
          return -1;
        }

      /* nfs call */

      rc = mnt3_remote_Mnt(clnt, &nfs_arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "mount3", "mnt3_remote_Mnt", "nfs_remote_mount", output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

  rc = res.fhs_status;
  if(rc != MNT3_OK)
    {
      mnt3_remote_Mnt_Free((nfs_res_t *) & res);
      fprintf(output, "nfs_remote_mount: Error %d in MNT3 protocol.\n", rc);
      return rc;
    }

  set_shell_fh3(p_mnt_hdl, (nfs_fh3 *) & res.mountres3_u.mountinfo.fhandle);

  mnt3_remote_Mnt_Free((nfs_res_t *) & res);

  return 0;
}                               /* nfs_remote_mount */

/*------------------------------------------------------------
 *          High level, shell-like commands
 *-----------------------------------------------------------*/

/** mount a path to browse it. */
int fn_nfs_remote_mount(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output)  /* IN : output stream          */
{
  int rc;
  char buff[2 * NFS3_FHSIZE + 1];
  shell_fh3_t mnt_hdl;

  /* check if a path has already been mounted */

  if(is_mounted_path != FALSE)
    {
      fprintf(output, "%s: a path is already mounted. Use \"umount\" command first.\n",
              argv[0]);
      return -1;
    }

  if(argc - 1 != 1)
    {
      fprintf(output, "%s: bad arguments.\n", argv[0]);
      fprintf(output, "Usage: mount <path>.\n");
      return -1;
    }

  rc = nfs_remote_mount(argv[1], &mnt_hdl, output);
  if(rc != 0)
    return -1;

  memcpy(&mounted_path_hdl, &mnt_hdl, sizeof(shell_fh3_t));

  strcpy(mounted_path, argv[1]);

  current_path_hdl = mounted_path_hdl;
  strcpy(current_path, "/");

  is_mounted_path = TRUE;

  fprintf(output, "Current directory is \"%s\" \n", current_path);
  snprintmem(buff, 2 * NFS3_FHSIZE + 1,
             (caddr_t) current_path_hdl.data_val, current_path_hdl.data_len);
  fprintf(output, "Current File handle is \"@%s\" \n", buff);

  return 0;
}

/** umount a mounted path */
int fn_nfs_remote_umount(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output) /* IN : output stream          */
{
  int rc;
  unsigned int i = 0;
  nfs_arg_t nfs_arg;
  nfs_res_t res;
  CLIENT *clnt;

  /* check if a path has already been mounted */

  if(is_mounted_path != TRUE)
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

  if(strncmp(argv[1], mounted_path, NFS2_MAXPATHLEN))
    {
      fprintf(output, "%s: this path is not mounted.\n", argv[0]);
      fprintf(output, "Current monted path : %s.\n", mounted_path);
      return -1;
    }

  do
    {
      clnt = getCLIENT("mount3");
      if(clnt == NULL)
        {
          fprintf(output, "MOUNT3 client not initialized\n");
          return -1;
        }

      /* nfs call */

      rc = mnt3_remote_Mnt(clnt, &nfs_arg, (nfs_res_t *) & res);
      rc = switch_result(rc, i, "mount3", "mnt3_remote_Umnt", "fn_nfs_remote_umount",
                         output);
      if(rc > 0)
        return rc;
      i += 1;
    }
  while(rc == -1);

  /* freeing args */

  cmdnfs_dirpath(CMDNFS_FREE, 0, NULL, 0, NULL, (caddr_t) & nfs_arg);

  if(rc != 0)
    {
      fprintf(output, "%s: Error %d in mnt_Umnt.\n", argv[0], rc);
      return rc;
    }

  mnt3_remote_Umnt_Free(&res);

  is_mounted_path = FALSE;

  return 0;
}

/** prints current path */
int fn_nfs_remote_pwd(int argc, /* IN : number of args in argv */
                      char **argv,      /* IN : arg list               */
                      FILE * output)    /* IN : output stream          */
{
  char buff[2 * NFS3_FHSIZE + 1];
  if(is_mounted_path != TRUE)
    {
      fprintf(output, "\t%s: no mounted path. Use \"mount\" command first.\n", argv[0]);
      return -1;
    }

  fprintf(output, "Current directory is \"%s\" \n", current_path);
  snprintmem(buff, 2 * NFS3_FHSIZE + 1,
             (caddr_t) current_path_hdl.data_val, current_path_hdl.data_len);
  fprintf(output, "Current File handle is \"@%s\" \n", buff);

  return 0;
}

/** proceed an ls command using NFS protocol NFS */
int fn_nfs_remote_ls(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output)     /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* first, retrieve the argument (if any) */
  if(Optind == argc - 1)
    {
      str_name = argv[Optind];

      /* retrieving handle */
      if((rc = nfs_remote_solvepath(&mounted_path_hdl,
                                   glob_path,
                                   NFS2_MAXPATHLEN,
                                    str_name, &current_path_hdl, &handle_tmp, output)))
        return rc;
    }
  else
    {
      str_name = ".";
      handle_tmp = current_path_hdl;
    }

  if(flag_v)
    fprintf(output, "proceeding ls (using NFS protocol) on \"%s\"\n", glob_path);

  if((rc = nfs_remote_getattr(&handle_tmp, &attrs, output)))
    return rc;

  /*
   * if the object is a file or a directoy with the -d option specified,
   * we only show its info and exit.
   */
  if((attrs.type != NF3DIR) || flag_d)
    {
      if((attrs.type == NF3LNK) && flag_l)
        {
          if((rc = nfs_remote_readlink(&handle_tmp, linkdata, output)))
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
        fprintf(output, "-->nfs3_remote_Readdirplus( path=%s, cookie=%llu )\n",
                glob_path, begin_cookie);

      if((rc = nfs_remote_readdirplus(&handle_tmp, begin_cookie, &cookieverf,    /* IN/OUT */
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
              if((rc = nfs_remote_readlink(p_hdl, linkdata, output)))
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

  nfs_remote_readdirplus_free(to_free);

  return 0;
}                               /* fn_nfs_remote_ls */

/** change current path */
int fn_nfs_remote_cd(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
    )
{

  const char help_cd[] = "usage: cd <path>\n";

  char glob_path[NFS2_MAXPATHLEN];
  shell_fh3_t new_hdl;
  int rc;
  fattr3 attrs;
  nfs3_uint32 mask;

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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

  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, glob_path, NFS2_MAXPATHLEN,
                          argv[1], &current_path_hdl, &new_hdl, output)))
    return rc;

  /* verify if the object is a directory */

  if((rc = nfs_remote_getattr(&new_hdl, &attrs, output)))
    return rc;

  if(attrs.type != NF3DIR)
    {
      fprintf(output, "Error: %s is not a directory\n", glob_path);
      return ENOTDIR;
    }

  /* verify lookup permission  */
  mask = ACCESS3_LOOKUP;
  if((rc = nfs_remote_access(&new_hdl, &mask, output)))
    return rc;

  if(!(mask & ACCESS3_LOOKUP))
    {
      fprintf(output, "Error: %s: permission denied.\n", glob_path);
      return EACCES;
    }

  /* if so, apply changes */
  strncpy(current_path, glob_path, NFS2_MAXPATHLEN);
  current_path_hdl = new_hdl;

  {
    char buff[2 * NFS3_FHSIZE + 1];
    fprintf(output, "Current directory is \"%s\" \n", current_path);
    snprintmem(buff, 2 * NFS3_FHSIZE + 1,
               (caddr_t) current_path_hdl.data_val, current_path_hdl.data_len);
    fprintf(output, "Current File handle is \"@%s\" \n", buff);
  }

  return 0;

}

/** create a file */
int fn_nfs_remote_create(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc = nfs_remote_solvepath(&mounted_path_hdl, glob_path, NFS2_MAXPATHLEN,
                                path, &current_path_hdl, &subdir_hdl, output)))
    return rc;

  if((rc = nfs_remote_create(&subdir_hdl, file, mode, &new_hdl, output)))
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
int fn_nfs_remote_mkdir(int argc,       /* IN : number of args in argv */
                        char **argv,    /* IN : arg list               */
                        FILE * output   /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc = nfs_remote_solvepath(&mounted_path_hdl, glob_path, NFS2_MAXPATHLEN,
                                path, &current_path_hdl, &subdir_hdl, output)))
    return rc;

  if((rc = nfs_remote_mkdir(&subdir_hdl, file, mode, &new_hdl, output)))
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
int fn_nfs_remote_unlink(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path_parent, current_path, NFS2_MAXPATHLEN);

  /* retrieves parent dir handle */
  if((rc = nfs_remote_solvepath(&mounted_path_hdl, glob_path_parent, NFS2_MAXPATHLEN,
                                path, &current_path_hdl, &subdir_hdl, output)))
    return rc;

  /* copy parent path */
  strncpy(glob_path_object, glob_path_parent, NFS2_MAXPATHLEN);

  /* lookup on child object */
  if((rc = nfs_remote_solvepath(&mounted_path_hdl, glob_path_object, NFS2_MAXPATHLEN,
                                file, &subdir_hdl, &obj_hdl, output)))
    return rc;

  /* get attributes of child object */
  if(flag_v)
    fprintf(output, "Getting attributes for %s...\n", glob_path_object);

  if((rc = nfs_remote_getattr(&obj_hdl, &attrs, output)))
    return rc;

  if(attrs.type != NF3DIR)
    {
      if(flag_v)
        fprintf(output, "%s is not a directory: calling nfs3_remove...\n",
                glob_path_object);

      if((rc = nfs_remote_remove(&subdir_hdl, file, output)))
        return rc;
    }
  else
    {
      if(flag_v)
        fprintf(output, "%s is a directory: calling nfs3_rmdir...\n", glob_path_object);

      if((rc = nfs_remote_rmdir(&subdir_hdl, file, output)))
        return rc;
    }

  if(flag_v)
    fprintf(output, "%s successfully removed.\n", glob_path_object);

  return 0;

}

/** setattr */
int fn_nfs_remote_setattr(int argc,     /* IN : number of args in argv */
                          char **argv,  /* IN : arg list               */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieve handle to the file whose attributes are to be changed */
  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, glob_path, NFS2_MAXPATHLEN, file,
                          &current_path_hdl, &obj_hdl, output)))
    return rc;

  /* Convert the peer (attr_name,attr_val) to an sattr3 structure. */
  if((rc = cmdnfs_sattr3(CMDNFS_ENCODE,
                         1, &attr_string, 0, NULL, (caddr_t) & set_attrs)) == FALSE)
    return rc;

  /* executes set attrs */
  if((rc = nfs_remote_setattr(&obj_hdl, &set_attrs, output)))
    return rc;

  if(flag_v)
    fprintf(output, "Attributes of \"%s\" successfully changed.\n", glob_path);

  return 0;
}                               /* fn_nfs_remote_setattr */

/** proceed a rename command. */
int fn_nfs_remote_rename(int argc,      /* IN : number of args in argv */
                         char **argv,   /* IN : arg list               */
                         FILE * output  /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(src_glob_path, current_path, NFS2_MAXPATHLEN);
  strncpy(tgt_glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieves paths handles */
  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, src_glob_path, NFS2_MAXPATHLEN,
                          src_path, &current_path_hdl, &src_path_handle, output)))
    return rc;

  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, tgt_glob_path, NFS2_MAXPATHLEN,
                          tgt_path, &current_path_hdl, &tgt_path_handle, output)))
    return rc;

  /* Rename operation */

  if((rc = nfs_remote_rename(&src_path_handle,   /* IN */
                            src_file,   /* IN */
                            &tgt_path_handle,   /* IN */
                            tgt_file,   /* IN */
                             output)))
    return rc;

  if(flag_v)
    fprintf(output, "%s/%s successfully renamed to %s/%s\n",
            src_glob_path, src_file, tgt_glob_path, tgt_file);

  return 0;

}

/** proceed a hardlink command. */
int fn_nfs_remote_hardlink(int argc,    /* IN : number of args in argv */
                           char **argv, /* IN : arg list               */
                           FILE * output        /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path_target, current_path, NFS2_MAXPATHLEN);
  strncpy(glob_path_link, current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle for target */
  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, glob_path_target, NFS2_MAXPATHLEN,
                          target, &current_path_hdl, &target_hdl, output)))
    return rc;

  /* retrieves path handle for parent dir */
  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, glob_path_link, NFS2_MAXPATHLEN,
                          path, &current_path_hdl, &dir_hdl, output)))
    return rc;

  rc = nfs_remote_link(&target_hdl,     /* IN - target file */
                       &dir_hdl,        /* IN - parent dir handle */
                       name,    /* IN - link name */
                       output); /* IN */

  if(rc)
    return rc;

  if(flag_v)
    fprintf(output, "%s/%s <=> %s successfully created\n", path, name, glob_path_target);

  return 0;

}

/** proceed an ln command. */

int fn_nfs_remote_ln(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output      /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieves path handle */
  if((rc =
     nfs_remote_solvepath(&mounted_path_hdl, glob_path, NFS2_MAXPATHLEN,
                          path, &current_path_hdl, &path_hdl, output)))
    return rc;

  /* Prepare link attributes : empty sattr3 list */

  if(cmdnfs_sattr3(CMDNFS_ENCODE, 0, NULL, 0, NULL, (caddr_t) & set_attrs) == FALSE)
    {
      /* invalid handle */
      fprintf(output, "\tError encoding nfs arguments.\n");
      return -1;
    }

  rc = nfs_remote_symlink(path_hdl,     /* IN - parent dir handle */
                          name, /* IN - link name */
                          content,      /* IN - link content */
                          &set_attrs,   /* Link attributes */
                          &link_hdl,    /* OUT - link handle */
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
int fn_nfs_remote_stat(int argc,        /* IN : number of args in argv */
                       char **argv,     /* IN : arg list               */
                       FILE * output)   /* IN : output stream          */
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

  /* check if a path has been mounted */

  if(is_mounted_path != TRUE)
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
  strncpy(glob_path, current_path, NFS2_MAXPATHLEN);

  /* retrieving handle */
  if((rc = nfs_remote_solvepath(&mounted_path_hdl,
                               glob_path,
                               NFS2_MAXPATHLEN,
                                str_name, &current_path_hdl, &handle_tmp, output)))
  {
    return rc;
  }

  if(flag_v)
    fprintf(output, "proceeding stat (using NFS protocol) on \"%s\"\n", glob_path);

  if((rc = nfs_remote_getattr(&handle_tmp, &attrs, output)))
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
      fprintf(output, "%s :\n", str_name);
      print_nfs_attributes(&attrs, output);
    }

  return 0;
}                               /* fn_nfs_remote_stat */

/** change thread credentials. */
int fn_nfs_remote_su(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output)     /* IN : output stream          */
{
  int i;
  char *str_uid;
  uid_t uid;
  struct passwd *pw_struct;

#define MAX_GRPS  128
  gid_t groups_tab[MAX_GRPS];
  int nb_grp;

  const char help_su[] = "usage: su <uid>\n";

  prog_vers_client_def_t *clnts = progvers_clnts;
  AUTH *auth;

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

  auth =
      authunix_create(localmachine, pw_struct->pw_uid, pw_struct->pw_gid, nb_grp,
                      groups_tab);
  if(auth == NULL)
    {
      fprintf(stdout, "su %s : error during creating Auth\n", pw_struct->pw_name);
    }
  while(clnts->name != NULL)
    {
      if(clnts->clnt != NULL)
        {
          clnts->clnt->cl_auth = auth;
        }
      clnts++;
    }
  memcpy(current_pw, pw_struct, sizeof(struct passwd));

  fprintf(output, "Done.\n");

  return 0;

}

int fn_nfs_remote_id(int argc,  /* IN : number of args in argv */
                     char **argv,       /* IN : arg list               */
                     FILE * output)     /* IN : output stream          */
{
  fprintf(output, "Current user : %s ( uid = %d, gid = %d )\n",
          current_pw->pw_name, current_pw->pw_uid, current_pw->pw_gid);

  return 0;
}
