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

#define AT_FDCWD   -100

#define OPENHANDLE_DRIVER_MAGIC     'O'
#define OPENHANDLE_OPEN_BY_HANDLE _IOWR(OPENHANDLE_DRIVER_MAGIC, 1, struct open_arg)
#define OPENHANDLE_LINK_BY_FD     _IOWR(OPENHANDLE_DRIVER_MAGIC, 2, struct link_arg)
#define OPENHANDLE_READLINK_BY_FD _IOWR(OPENHANDLE_DRIVER_MAGIC, 3, struct readlink_arg)

main(int argc, char *argv[])
{
  int handle_fd;
  char buf[100];
  int fd, rc, file_fd;
  struct open_arg oarg;
  struct file_handle *handle;

  if(argc != 4)
    {
      fprintf(stderr, "Usage: %s,  <device> <mountdir> <handle-file>\n", argv[0]);
      exit(1);
    }
  fd = open(argv[1], O_RDONLY);
  if(fd < 0)
    perror("open"), exit(1);

  handle = malloc(sizeof(struct file_handle) + 20);

  /* read the handle to a handle.data file */
  handle_fd = open(argv[3], O_RDONLY);
  read(handle_fd, handle, sizeof(struct file_handle) + 20);
  printf("Handle size is %d\n", handle->handle_size);

  oarg.mountdirfd = open(argv[2], O_RDONLY | O_DIRECTORY);
  if(oarg.mountdirfd < 0)
    perror("open"), exit(2);
  oarg.handle = handle;
  oarg.flags = O_RDWR | O_TRUNC;
  file_fd = ioctl(fd, OPENHANDLE_OPEN_BY_HANDLE, &oarg);
  if(file_fd < 0)
    perror("ioctl"), exit(2);
  memset(buf, 0, 100);
  printf("There should not be any content shown afte this\n");
  while(1)
    {
      rc = read(file_fd, buf, 99);
      if(rc <= 0)
        break;
      buf[rc] = '\0';
      printf("%s", buf);
      memset(buf, 0, 100);
    }
  close(file_fd);
  close(fd);
}
