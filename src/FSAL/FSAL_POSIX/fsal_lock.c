/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 
 * 
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

static int do_blocking_lock(posixfsal_file_t * obj_handle, posixfsal_lockdesc_t * ldesc)
{
  /*
   * Linux client have this grant hack of pooling for
   * availablity when we returned NLM4_BLOCKED. It just
   * poll with a large timeout. So depend on the hack for
   * now. Later we should really do the block lock support
   */
  errno = EAGAIN;
  return -1;
}

/**
 * FSAL_lock:
 */
fsal_status_t POSIXFSAL_lock(posixfsal_file_t * obj_handle,
                             posixfsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  int cmd;
  int retval;
  int fd = obj_handle->filefd;

  errno = 0;
  /*
   * First try a non blocking lock request. If we fail due to
   * lock already being held, and if blocking is set for
   * a child and do a waiting lock
   */
  retval = fcntl(fd, F_SETLK, &ldesc->flock);
  if(retval)
    {
      if((errno == EACCES) || (errno == EAGAIN))
        {
          if(blocking)
            {
              do_blocking_lock(obj_handle, ldesc);
            }
        }
      Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock);
    }
  /* granted lock */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lock);
}

/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t POSIXFSAL_changelock(posixfsal_lockdesc_t * lock_descriptor,      /* IN / OUT */
                                   fsal_lockparam_t * lock_info /* IN */
    )
{

  /* sanity checks. */
  if(!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_changelock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_changelock);

}

/**
 * FSAL_unlock:
 *
 */
fsal_status_t POSIXFSAL_unlock(posixfsal_file_t * obj_handle,
                               posixfsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = obj_handle->filefd;

  errno = 0;
  ldesc->flock.l_type = F_UNLCK;
  retval = fcntl(fd, F_SETLK, &ldesc->flock);
  if(retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlock);
}

fsal_status_t POSIXFSAL_getlock(posixfsal_file_t * obj_handle,
                                posixfsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = obj_handle->filefd;

  errno = 0;
  retval = fcntl(fd, F_GETLK, &ldesc->flock);
  if(retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_getlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getlock);
}
