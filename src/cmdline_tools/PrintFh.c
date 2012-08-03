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
 * ---------------------------------------*/

/**
 *
 * \file    main.c
 * \author  $Author: leibovic $
 * \date    $Date: 2006/02/23 07:42:53 $
 * \version $Revision: 1.28 $
 * \brief   main shell routine.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nfs_init.h"
#include "fsal.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>             /* for sigaction */
#include <errno.h>
#include <ctype.h>

#define CMD_BUFFER_SIZE 1024
#define DEFAULT_CONFIG_FILE "/etc/ganesha/"FS_NAME".ganesha.nfsd.conf"

short HashFileID4(u_int64_t fileid4);
time_t ServerBootTime;

char ganesha_exec_path[MAXPATHLEN];     /* Just because the symbol is required to compile */

/* determine buffer type and display it */
void print_buffer(caddr_t buffer, size_t sz_returned)
{
  unsigned int i;
  int ascii, numeric, hexa;

  /* print the value */
  if(sz_returned == 0)
    {
      printf("(empty)\n");
      return;
    }

  /* ascii, numeric or hexa ? */
  ascii = numeric = hexa = FALSE;

  /* is it ascii ? */
  if(strlen(buffer) == sz_returned - 1 || strlen(buffer) == sz_returned)
    {
      char *str = buffer;
      int tmp_is_ascii = TRUE;

      for(i = 0; i < strlen(str); i++)
        {
          if(!isprint(str[i]) && !isspace(str[i]))
            {
              tmp_is_ascii = FALSE;
              break;
            }
        }
      if(tmp_is_ascii)
        ascii = TRUE;
    }

  /* is it numeric ? */
  if(!ascii)
    {
      if(sz_returned == 1 || sz_returned == 2 || sz_returned == 4 || sz_returned == 8)
        numeric = TRUE;
      else
        hexa = TRUE;
    }

  if(ascii)
    {
      printf("%s\n", buffer);
    }
  else if(numeric)
    {
      if(sz_returned == 1)
        printf("%hhu\n", buffer[0]);
      else if(sz_returned == 2)
        printf("%hu\n", *((unsigned short *)buffer));
      else if(sz_returned == 4)
        printf("%u\n", *((unsigned int *)buffer));
      else if(sz_returned == 8)
        printf("%llu\n", *((unsigned long long *)buffer));
      else
        {
          for(i = 0; i < sz_returned; i += 8)
            {
              unsigned long long *p64 = (unsigned long long *)(buffer + i);
              if(i == 0)
                printf("%llu", *p64);
              else
                printf(".%llu", *p64);
            }
          printf("\n");
        }
    }
  else if(hexa)                 /* hexa */
    {
      printf("0x");
      for(i = 0; i < sz_returned; i++)
        {
          unsigned char val = buffer[i];
          printf("%hhX", val);
        }
      printf("\n");

    }

  return;
}                               /* print_buffer */

int main(int argc, char *argv[])
{
  int c;
  int exportid = 0;
  char argpath[FSAL_MAX_PATH_LEN];
  char filepath[FSAL_MAX_PATH_LEN];
  fsal_path_t fsal_path ;
  char exec_name[MAXPATHLEN];
  char *tempo_exec_name = NULL;
  fsal_op_context_t fsal_op_context;
  fsal_export_context_t fsal_export_context;
  exportlist_t *pexport = NULL;
  nfs_start_info_t nfs_start_info;
  fsal_status_t fsal_status;
  path_str_t fsal_path_lib[NB_AVAILABLE_FSAL];

  fsal_handle_t fsal_handle;
  fsal_path_t export_path = FSAL_PATH_INITIALIZER;
  FILE * file = NULL ;

  char buff[2*MAXPATHLEN] ;

  char options[] = "h@f:i:p:";
  char usage[] = "%s [-h][-f <cfg_path>] {-v 2|3|4 <NFS_FileHandle> | -i <inum>}\n"
      "   -h               : prints this help\n"
      "   -f <config_file> : sets the ganesha configuration file to be used\n"
      "   -p <path>        : FSAL path to the related object\n" 
      "   -i <exportid>    : export id to be used for this path\n" ;

  /* Set the server's boot time and epoch */
  ServerBootTime = time(NULL);
  ServerEpoch    = ServerBootTime;

  SetDefaultLogging("STDERR");

  /* What is the executable file's name */
  if((tempo_exec_name = strrchr(argv[0], '/')) != NULL)
    strcpy((char *)exec_name, tempo_exec_name + 1);

  strncpy(config_path, DEFAULT_CONFIG_FILE, MAXPATHLEN);

  /* now parsing options with getopt */
  while((c = getopt(argc, argv, options)) != EOF)
    {
      switch (c)
        {
        case '@':
          printf("%s compiled on %s at %s\n", exec_name, __DATE__, __TIME__);
          printf("Git HEAD = %s\n", _GIT_HEAD_COMMIT ) ;
          printf("Git Describe = %s\n", _GIT_DESCRIBE ) ;
          exit(0);
          break;

        case 'h':
          printf(usage, exec_name);
          exit(0);
          break;

        case 'f':
          strncpy(config_path, optarg, MAXPATHLEN);
          break;

        case 'i':
          if(sscanf(optarg, "%u", &exportid) != 1)
            {
              fprintf(stderr, "Invalid object_id %s (base-10 integer expected)\n",
                      optarg);
              exit(1);
            }
          break;

        case 'p':
          strncpy( argpath, optarg, MAXPATHLEN ) ;
          break ;

        case '?':
          printf("Unknown option: %c\n", optopt);
          printf(usage, exec_name);
          exit(1);
        }
    }

  /* initialize memory and logging */

  nfs_prereq_init("convert_fh", "localhost", NIV_MAJ, "/dev/tty");

  /* Load the FSAL library (if needed) */
  if(!FSAL_LoadLibrary((char *)fsal_path_lib))  /** @todo: this part of the code and this utility has to be checked */
    {
      fprintf(stderr, "NFS MAIN: Could not load FSAL dynamic library %s", (char *)fsal_path_lib[0]);
      exit(1);
    }

  /* Get the FSAL functions */
  FSAL_LoadFunctions();

  /* Get the FSAL consts */
  FSAL_LoadConsts();

  /* initialize default parameters */

  nfs_set_param_default();

  /* parse configuration file */

  if(nfs_set_param_from_conf(&nfs_start_info))
    {
      fprintf(stderr, "Error parsing configuration file '%s'", config_path);
      exit(1);
    }

  /* check parameters consitency */

  if(nfs_check_param_consistency())
    {
      fprintf(stderr, "Inconsistent parameters found");
      exit(1);
    }

  if(!nfs_param.pexportlist)
    {
      fprintf(stderr, "No export entries found in configuration file !!!\n");
      return -1;
    }

  /* not initialization is needed for converting fileid to path in datacache */
  fsal_status = FSAL_Init(&nfs_param.fsal_param);
  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      fprintf(stderr, "FSAL library could not be initialized, major=%d minor=%d\n",
              fsal_status.major, fsal_status.minor);
      exit(1);
    }

  if((pexport = nfs_Get_export_by_id(nfs_param.pexportlist, exportid)) == NULL)
    {
      fprintf(stderr, "NFS FH has exportid %u which is invalid....\n", exportid);
      exit(1);
    }

  /* INITIALIZING A CLIENT CONTEXT FOR FSAL */

  FSAL_str2path(pexport->fullpath, MAXPATHLEN, &export_path);

  if(FSAL_IS_ERROR
     (fsal_status =
      FSAL_BuildExportContext(&fsal_export_context, &export_path,
                              pexport->FS_specific)))
    {
      fprintf(stderr, "Error in FSAL_BuildExportContext, major=%u, minor=%u\n",
              fsal_status.major, fsal_status.minor);
      exit(1);
    }

  fsal_status = FSAL_InitClientContext(&fsal_op_context);
  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      fprintf(stderr, "Could not init client context... major=%d minor=%d\n",
              fsal_status.major, fsal_status.minor);
      exit(1);
    }

  fsal_status = FSAL_GetClientContext(&fsal_op_context,
                                      &fsal_export_context, 0, 0, NULL, 0);

  if(FSAL_IS_ERROR(fsal_status))
    {
      /* Failed init */
      fprintf(stderr, "Could not get cred for uid=%d gid=%d, major=%d minor=%d\n",
              getuid(), getgid(), fsal_status.major, fsal_status.minor);
      exit(1);
    }

  if( ( file = fopen( argpath, "r" ) ) == NULL )
    {
       fprintf( stderr, "Can't open input file %s\n", argpath ) ;
       exit( 1 ) ;
    }

  while( fgets( filepath, FSAL_MAX_PATH_LEN, file ) )
   { 
     /* Chomp */
     filepath[strlen(filepath)-1] = '\0'  ;

     if(FSAL_IS_ERROR( FSAL_str2path( filepath, FSAL_MAX_PATH_LEN, &fsal_path)))
       {
         /* Failed init */
         fprintf( stderr, "Could not convert string '%s' to valid fsal_path\n", filepath ) ;
         exit(1);
       }

     if(FSAL_IS_ERROR((fsal_status = FSAL_lookupPath(&fsal_path, &fsal_op_context, &fsal_handle, NULL))))
       {
         /* Failed init */
         fprintf(stderr, "Could not look up path %s\n", filepath ) ;
         continue ;
       }

       
      snprintHandle(buff, 2 * sizeof(fsal_handle_t) + 1, &fsal_handle);
      printf( "%s %s\n", filepath, buff ) ;
       
   }
   
  fclose( file ) ;

  exit(0);
}
