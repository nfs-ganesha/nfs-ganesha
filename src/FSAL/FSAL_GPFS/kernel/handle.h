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

#ifndef HANDLE_H
#define HANDLE_H

#ifdef __KERNEL__
#include <linux/mount.h>
#include <linux/dcache.h>
#endif

/*FIXME!!  need compat arg or rework the structure */
struct file_handle
{
  int handle_size;
  int handle_type;
  /* file identifier */
  unsigned char f_handle[0];
};

struct open_arg
{
  int mountdirfd;
  int flags;
  int openfd;
  struct file_handle *handle;
};

struct name_handle_arg
{
  int dfd;
  int flag;
  char *name;
  struct file_handle *handle;
};

struct link_arg
{
  int file_fd;
  int dir_fd;
  char *name;
};

struct readlink_arg
{
  int fd;
  char *buffer;
  int size;
};

#ifdef __KERNEL__
extern long name_to_handle_at(int dfd, const char __user * name,
                              struct file_handle __user * handle, int flag);
extern long open_by_handle(int mountdirfd, struct file_handle __user * handle, int flags);
extern long link_by_fd(int file_fd, int newdfd, const char __user * newname);
extern long readlink_by_fd(int fd, char __user * buf, int buffsize);
#endif
#endif
