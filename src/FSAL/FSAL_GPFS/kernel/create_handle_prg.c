/*
 *   Copyright (C) International Business Machines  Corp., 2010
 *   Author(s): Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as published
 *   by the Free Software Foundation; either version 2.1 of the License, or
 *   (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public License
 *   along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include "handle.h"

#define OPENHANDLE_DRIVER_MAGIC     'O'
#define OPENHANDLE_NAME_TO_HANDLE _IOWR(OPENHANDLE_DRIVER_MAGIC, 0, struct name_handle_arg)

main(int argc, char *argv[])
{
  int fd, handle_fd, rc;
  struct name_handle_arg harg;

  if(argc != 4)
    {
      fprintf(stderr, "Usage: %s,  <device> <filename> <handle_file>\n", argv[0]);
      exit(1);
    }
  fd = open(argv[1], O_RDONLY);
  if(fd < 0)
    perror("open"), exit(1);
  harg.name = argv[2];
  harg.dfd = AT_FDCWD;
  harg.flag = 0;
  harg.handle = malloc(sizeof(struct file_handle) + 20);
  harg.handle->handle_size = 20;

  rc = ioctl(fd, OPENHANDLE_NAME_TO_HANDLE, &harg);
  if(rc < 0)
    perror("ioctl"), exit(2);

  /* write the handle to a handle.data file */
  handle_fd = open(argv[3], O_RDWR | O_CREAT | O_TRUNC, 0600);
  write(handle_fd, harg.handle, sizeof(harg) + harg.handle->handle_size);
  printf("Handle size is %d\n", harg.handle->handle_size);

  close(handle_fd);
  close(fd);
}
