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
  char buffer[CMD_BUFFER_SIZE];
  char str[2 * CMD_BUFFER_SIZE];
  fhandle2 filehandle_v2;
  struct nfs_fh3 filehandle_v3;
  nfs_fh4 filehandle_v4;
  int flag_i = FALSE;
  char exec_name[MAXPATHLEN];
  char *tempo_exec_name = NULL;
  cache_inode_fsal_data_t fsal_data;
  fsal_op_context_t fsal_op_context;
  fsal_export_context_t fsal_export_context;
  exportlist_t *pexportlist = NULL;
  exportlist_t *pexport = NULL;
  nfs_start_info_t nfs_start_info;
  fsal_status_t fsal_status;
  unsigned int nfs_version = 3;
  path_str_t fsal_path_lib[NB_AVAILABLE_FSAL];
#ifdef _USE_SHARED_FSAL
  int lentab = NB_AVAILABLE_FSAL ;
#endif

  short cache_content_hash;
  char entry_path[MAXPATHLEN];
  int i, nb_char;

  fsal_path_t export_path = FSAL_PATH_INITIALIZER;
  unsigned int cookie;
  fsal_xattrent_t xattr_array[256];
  unsigned int nb_returned;
  int eol;
  char attr_buffer[4096];
  size_t sz_returned;
  fsal_u64_t objid;
  char options[] = "h@f:v:i:";
  char usage[] = "%s [-h][-f <cfg_path>] {-v 2|3|4 <NFS_FileHandle> | -i <inum>}\n"
      "   -h               : prints this help\n"
      "   -f <config_file> : sets the ganesha configuration file to be used\n"
      "   -v <nfs_version> : sets the NFS version the file handle passed as argument\n"
      "   -i <inum>        : get datacache path for the given inode number (decimal)\n";

  ServerBootTime = time(NULL);

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
          if(sscanf(optarg, "%llu", &objid) != 1)
            {
              fprintf(stderr, "Invalid object_id %s (base-10 integer expected)\n",
                      optarg);
              exit(1);
            }
          flag_i = TRUE;
          break;

        case 'v':
          nfs_version = atoi(optarg);
          if((nfs_version < 2) || (nfs_version > 4))
            {
              fprintf(stderr, "Invalid nfs version %u\n", nfs_version);
              exit(1);
            }
          break;
        case '?':
          printf("Unknown option: %c\n", optopt);
          printf(usage, exec_name);
          exit(1);
        }
    }

  if(!flag_i && (optind != argc - 1))
    {
      printf("Missing argument: <NFS_FileHandle>\n");
      printf(usage, exec_name);
      exit(1);
    }

  /* initialize memory and logging */

  nfs_prereq_init("convert_fh", "localhost", NIV_MAJ, "/dev/tty");

#ifdef _USE_SHARED_FSAL
  if(nfs_get_fsalpathlib_conf(config_path, fsal_path_lib, &lentab))
    {
      fprintf(stderr, "NFS MAIN: Error parsing configuration file.");
      exit(1);
    }
#endif                          /* _USE_SHARED_FSAL */

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

  pexportlist = nfs_param.pexportlist;

  /* not initialization is needed for converting fileid to path in datacache */
  if(!flag_i)
    {

#ifdef _USE_SHARED_FSAL
      fsal_status = FSAL_Init(&nfs_param.fsal_param[0]);
#else
      fsal_status = FSAL_Init(&nfs_param.fsal_param);
#endif
      if(FSAL_IS_ERROR(fsal_status))
        {
          /* Failed init */
          fprintf(stderr, "FSAL library could not be initialized, major=%d minor=%d\n",
                  fsal_status.major, fsal_status.minor);
          exit(1);
        }

      strncpy(str, argv[optind], 2 * CMD_BUFFER_SIZE);

      switch (nfs_version)
        {
        case 2:
          if(sscanmem(filehandle_v2, sizeof(file_handle_v2_t), (char *)str) == -1)
            {
              fprintf(stderr, "Bad FH as input (expected size: %lu bytes)\n",
                      (unsigned long)sizeof(file_handle_v2_t));
              exit(1);
            }

          exportid = nfs2_FhandleToExportId(&filehandle_v2);

          break;

        case 3:
          if(sscanmem(buffer, sizeof(file_handle_v3_t), (char *)str) == -1)
            {
              fprintf(stderr, "Bad FH as input (expected size: %lu bytes)\n",
                      (unsigned long)sizeof(file_handle_v3_t));
              exit(1);
            }
          filehandle_v3.data.data_val = (char *)buffer;
          filehandle_v3.data.data_len = sizeof(file_handle_v3_t);

          exportid = nfs3_FhandleToExportId(&filehandle_v3);
          break;

        case 4:
          if(sscanmem(buffer, sizeof(file_handle_v4_t), (char *)str) == -1)
            {
              fprintf(stderr, "Bad FH as input (expected size: %lu bytes)\n",
                      (unsigned long)sizeof(file_handle_v4_t));
              exit(1);
            }
          filehandle_v4.nfs_fh4_val = (char *)buffer;
          filehandle_v4.nfs_fh4_len = sizeof(file_handle_v4_t);

          exportid = nfs4_FhandleToExportId(&filehandle_v4);
          break;

        }
      if((pexport = nfs_Get_export_by_id(pexportlist, exportid)) == NULL)
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

      /* now, can use the fsal_op_context */
      switch (nfs_version)
        {
        case 2:
          if(!nfs2_FhandleToFSAL(&filehandle_v2, &fsal_data.handle, &fsal_op_context))
            {
              fprintf(stderr, "Cannot convert Fhandle to FSAL\n");
              exit(1);
            }
          break;

        case 3:
          if(!nfs3_FhandleToFSAL(&filehandle_v3, &fsal_data.handle, &fsal_op_context))
            {
              fprintf(stderr, "Cannot convert Fhandle to FSAL\n");
              exit(1);
            }
          break;

        case 4:
          if(!nfs4_FhandleToFSAL(&filehandle_v4, &fsal_data.handle, &fsal_op_context))
            {
              fprintf(stderr, "Cannot convert Fhandle to FSAL\n");
              exit(1);
            }
          break;
        }

      printf("\n");

      snprintmem((caddr_t) str, 2 * CMD_BUFFER_SIZE, (caddr_t) & fsal_data.handle,
                 sizeof(fsal_data.handle));

      printf("%-18s = %s\n", "FSAL Handle", str);

      /* Now, list FSAL extended attributes */

      cookie = XATTRS_READLIST_FROM_BEGINNING;
      eol = FALSE;

      while(!eol)
        {
          unsigned int index;

          fsal_status = FSAL_ListXAttrs(&fsal_data.handle, cookie, &fsal_op_context,
                                        xattr_array, 256, &nb_returned, &eol);

          if(FSAL_IS_ERROR(fsal_status))
            {
              fprintf(stderr, "Error executing FSAL_ListXAttrs\n");
              exit(1);
            }

          /* list attributes and get their value */

          for(index = 0; index < nb_returned; index++)
            {
              cookie = xattr_array[index].xattr_cookie;

              printf("%-18s = ", xattr_array[index].xattr_name.name);

              fsal_status =
                  FSAL_GetXAttrValueByName(&fsal_data.handle,
                                           &xattr_array[index].xattr_name,
                                           &fsal_op_context, attr_buffer, 4096,
                                           &sz_returned);

              if(FSAL_IS_ERROR(fsal_status))
                {
                  fprintf(stderr, "Error executing FSAL_GetXAttrValueByName\n");
                }

              /* Display it */
              print_buffer(attr_buffer, sz_returned);

            }

        }

      /* get object ID */
      fsal_status =
          FSAL_DigestHandle(&fsal_export_context, FSAL_DIGEST_FILEID4, &fsal_data.handle,
                            (caddr_t) & objid);

      if(FSAL_IS_ERROR(fsal_status))
        {
          fprintf(stderr, "Error retrieving fileid from handle\n");
        }
      else
        {
          printf("%-18s = %llu\n", "FileId", objid);
        }

    }

  /* end of retrieval of objid */
  /* build the path in the datacache */
  cache_content_hash = HashFileID4(objid);

  /* for limiting the number of entries into each datacache directory
   * we create 256 subdirectories on 2 levels, depending on the entry's fileid.
   */
  nb_char = snprintf(entry_path, MAXPATHLEN, "export_id=%d", 0);

  for(i = 0; i <= 8; i += 8)
    {
      /* concatenation of hashval */
      nb_char += snprintf((char *)(entry_path + nb_char), MAXPATHLEN - nb_char,
                          "/%02hhX", (char)((cache_content_hash >> i) & 0xFF));
    }

  /* displays the node name */

  printf("%-18s = %s/%s/node=%llx*\n", "DataCache path",
         nfs_param.cache_layers_param.cache_content_client_param.cache_dir, entry_path,
         objid);

  exit(0);
}
