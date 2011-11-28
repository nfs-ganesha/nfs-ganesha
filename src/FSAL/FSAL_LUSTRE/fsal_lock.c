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
 * LUSTREFSAL_lock_op:
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
fsal_status_t LUSTREFSAL_lock_op( fsal_file_t       * p_file_descriptor,   /* IN */
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
  lustrefsal_file_t * pfd = (lustrefsal_file_t *) p_file_descriptor;


  if(p_file_descriptor == NULL || p_filehandle == NULL || p_context == NULL)
    {
      if(p_file_descriptor == NULL)
        LogDebug(COMPONENT_FSAL, "p_file_descriptor argument is NULL.");
      if(p_filehandle == NULL)
        LogDebug(COMPONENT_FSAL, "p_filehandle argument is NULL.");
      if(p_context == NULL)
        LogDebug(COMPONENT_FSAL, "p_context argument is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }

  if(p_owner != NULL)
    Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);

  if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT)
    {
      LogDebug(COMPONENT_FSAL, "conflicting_lock argument can't"
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
