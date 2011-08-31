/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
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
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

/**
 * GPFSFSAL_lock_op:
 * Lock/unlock/test an owner independent (anonymous) lock for a region in a file.
 *
 * \param p_file_descriptor (input):
 *        File descriptor of the file to lock.
 * \param p_filehandle (input):
 *        File handle of the file to lock.
 * \param p_context (input):
 *        Context
 * \param p_owner (input):
 *        Owner for the requested lock
 * \param lock_op (input):
 *        Can be either FSAL_OP_LOCKT, FSAL_OP_LOCK, FSAL_OP_UNLOCK.
 *        The operations are test if a file region is locked, lock a file region, unlock a
 *        file region.
 * \param lock_type (input):
 *        Can be either FSAL_LOCK_R, FSAL_LOCK_W.
 *        Either a read lock or write lock.
 * \param lock_start (input):
 *        Start of lock region measured as offset of bytes from start of file.
 * \param lock_length (input):
 *        Number of bytes to lock.
 *
 * \return Major error codes:
 *      - ERR_FSAL_NO_ERROR: no error.
 *      - ERR_FSAL_FAULT: One of the in put parameters is NULL.
 *      - ERR_FSAL_PERM: lock_op was FSAL_OP_LOCKT and the result was that the operation would not be possible.
 */
fsal_status_t GPFSFSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
                                fsal_handle_t     * p_filehandle,        /* IN */
                                fsal_op_context_t * p_context,           /* IN */
                                void              * p_owner,             /* IN (opaque to FSAL) */
                                fsal_lock_op_t      lock_op,             /* IN */
                                fsal_lock_param_t   request_lock,        /* IN */
                                fsal_lock_param_t * conflicting_lock)    /* OUT */
{
  int retval;
  struct flock lock_args;
  int fcntl_comm;
  gpfsfsal_file_t * pfd = (gpfsfsal_file_t *) p_file_descriptor;

  if(p_file_descriptor == NULL || p_filehandle == NULL || p_context == NULL)
    {
      if(p_file_descriptor == NULL)
        LogDebug(COMPONENT_FSAL, "GPFSFSAL_lock_op_no_owner: p_file_descriptor argument is NULL.");
      if(p_filehandle == NULL)
        LogDebug(COMPONENT_FSAL, "GPFSFSAL_lock_op_no_owner: p_filehandle argument is NULL.");
      if(p_context == NULL)
        LogDebug(COMPONENT_FSAL, "GPFSFSAL_lock_op_no_owner: p_context argument is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }

  if(p_owner != NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);

  if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT)
    {
      LogDebug(COMPONENT_FSAL, "GPFSFSAL_lock_op_no_owner: conflicting_lock argument can't"
               " be NULL with lock_op  = LOCKT");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }

  LogFullDebug(COMPONENT_FSAL, "Locking: op:%d type:%d start:%llu length:%llu ", lock_op,
               request_lock.lock_type, request_lock.lock_start, request_lock.lock_length);

  if(lock_op == FSAL_OP_LOCKT)
    fcntl_comm = F_GETLK;
  else if(lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK)
    fcntl_comm = F_SETLK;
  else
    {
      LogDebug(COMPONENT_FSAL, "ERROR: Lock operation requested was not TEST, READ, or WRITE.");
      Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);      
    }

  if(request_lock.lock_type == FSAL_LOCK_R)
    lock_args.l_type = F_RDLCK;
  else if(request_lock.lock_type == FSAL_LOCK_W)
    lock_args.l_type = F_WRLCK;
  else
    {
      LogDebug(COMPONENT_FSAL, "ERROR: The requested lock type was not read or write.");
      Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);
    }

  if(lock_op == FSAL_OP_UNLOCK)
    lock_args.l_type = F_UNLCK;

  lock_args.l_len = request_lock.lock_length;
  lock_args.l_start = request_lock.lock_start;
  lock_args.l_whence = SEEK_SET;

  errno = 0;
  retval = fcntl(pfd->fd, fcntl_comm, &lock_args);
  if(retval && lock_op == FSAL_OP_LOCK)
    {
      if(conflicting_lock != NULL)
        {
          fcntl_comm = F_GETLK;
          retval = fcntl(pfd->fd, fcntl_comm, &lock_args);
          if(retval)
            {
              LogCrit(COMPONENT_FSAL, "After failing a lock request, I couldn't even"
                      " get the details of who owns the lock.");
              Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock_op);
            }
          if(conflicting_lock != NULL)
            {
              conflicting_lock->lock_owner = lock_args.l_pid;
              conflicting_lock->lock_length = lock_args.l_len;
              conflicting_lock->lock_start = lock_args.l_start;
              conflicting_lock->lock_type = lock_args.l_type;
            }
        }
      Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock_op);
    }

  /* F_UNLCK is returned then the tested operation would be possible. */
  if(conflicting_lock != NULL)
    {
      if(lock_op == FSAL_OP_LOCKT && lock_args.l_type != F_UNLCK)
        {
          conflicting_lock->lock_owner = lock_args.l_pid;
          conflicting_lock->lock_length = lock_args.l_len;
          conflicting_lock->lock_start = lock_args.l_start;
          conflicting_lock->lock_type = lock_args.l_type;
        }
      else
        {
          conflicting_lock->lock_owner = 0;
          conflicting_lock->lock_length = 0;
          conflicting_lock->lock_start = 0;
          conflicting_lock->lock_type = FSAL_NO_LOCK;
        }
    }

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lock_op);
}

static int do_blocking_lock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
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
fsal_status_t GPFSFSAL_lock(fsal_file_t * obj_handle,
                        fsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  /*
   * First try a non blocking lock request. If we fail due to
   * lock already being held, and if blocking is set for
   * a child and do a waiting lock
   */
  retval = fcntl(fd, F_SETLK, &((gpfsfsal_lockdesc_t *)ldesc)->flock);
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
fsal_status_t GPFSFSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                              fsal_lockparam_t * lock_info      /* IN */
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
fsal_status_t GPFSFSAL_unlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  ((gpfsfsal_lockdesc_t *)ldesc)->flock.l_type = F_UNLCK;
  retval = fcntl(fd, F_SETLK, &((gpfsfsal_lockdesc_t *)ldesc)->flock);
  if(retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlock);
}

fsal_status_t GPFSFSAL_getlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  retval = fcntl(fd, F_GETLK, &((gpfsfsal_lockdesc_t *)ldesc)->flock);
  if(retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_getlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getlock);
}
