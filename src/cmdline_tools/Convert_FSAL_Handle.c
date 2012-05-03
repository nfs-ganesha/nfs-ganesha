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
 * \brief   extract fileid from FSAL handle.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include "log.h"
#include "stuff_alloc.h"
#include "fsal.h"

#ifdef _USE_HPSS
#include "../FSAL/FSAL_HPSS/HPSSclapiExt/hpssclapiext.h"
#endif

#define CMD_BUFFER_SIZE 1024

#define CONFIG_FILE "/var/hpss/etc/hpss.ganesha.nfsd.conf"

time_t ServerBootTime;

int main(int argc, char *argv[])
{
  int c;
  char exec_name[MAXPATHLEN];
  char *tempo_exec_name = NULL;

#ifdef _USE_HPSS
  char buffer[CMD_BUFFER_SIZE];
  fsal_handle_t fsal_handle;
  char str[2 * CMD_BUFFER_SIZE];
  uint64_t objid;
  ns_ObjHandle_t hpss_hdl;
  hpss_Attrs_t hpss_attr;
  char *tmp_str_uuid;
#endif
  char options[] = "h@";
  char usage[] = "%s [-h] <FSAL_Handle>\n" "   -h               : prints this help\n";

  /* Set the server's boot time and epoch */
  ServerBootTime = time(NULL);
  ServerEpoch    = ServerBootTime;

  /* What is the executable file's name */
  if((tempo_exec_name = strrchr(argv[0], '/')) != NULL)
    strcpy((char *)exec_name, tempo_exec_name + 1);

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

        case '?':
          printf("Unknown option: %c\n", optopt);
          printf(usage, exec_name);
          exit(1);
        }
    }

  if(optind != argc - 1)
    {
      printf("Missing argument: <FSAL_Handle>\n");
      printf(usage, exec_name);
      exit(1);
    }
#ifdef _USE_HPSS
  sscanHandle(&fsal_handle, argv[optind]);

  snprintmem((caddr_t) str, 2 * CMD_BUFFER_SIZE, (caddr_t) & fsal_handle.ns_handle,
             sizeof(ns_ObjHandle_t));
  printf("NS Handle = %s\n", str);

  objid = hpss_GetObjId(&(fsal_handle.ns_handle));
  printf("FileId = %llu\n", objid);
#endif

  exit(0);
}
