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
 *        Owner for the requested lock; Opaque to FSAL.
 * \param lock_op (input):
 *        Can be either FSAL_OP_LOCKT, FSAL_OP_LOCK, FSAL_OP_UNLOCK.
 *        The operations are test if a file region is locked, lock a
 *        file region, unlock a file region.
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
fsal_status_t GPFSFSAL_lock_op( fsal_file_t       * p_file_descriptor, /* IN */
                                fsal_handle_t     * p_filehandle,      /* IN */
                                fsal_op_context_t * p_context,         /* IN */
                                void              * p_owner,           /* IN */
                                fsal_lock_op_t      lock_op,           /* IN */
                                fsal_lock_param_t   request_lock,      /* IN */
                                fsal_lock_param_t * conflicting_lock)  /* OUT */
{
  int retval;
  struct glock glock_args;
  struct set_get_lock_arg gpfs_sg_arg;
  glock_args.lfd = ((gpfsfsal_file_t *)p_file_descriptor)->fd;
  gpfsfsal_op_context_t *gpfs_op_cxt = (gpfsfsal_op_context_t *)p_context;
  gpfsfsal_file_t * pfd = (gpfsfsal_file_t *) p_file_descriptor;

  if(p_file_descriptor == NULL)
    {
      LogDebug(COMPONENT_FSAL, "p_file_descriptor arg is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }
  if(p_filehandle == NULL)
    {
      LogDebug(COMPONENT_FSAL, "p_filehandle arg is NULL.");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }
  if(p_context == NULL)
    {
        LogDebug(COMPONENT_FSAL, "p_context arg is NULL.");
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }
  if(p_owner == NULL)
    {
        LogDebug(COMPONENT_FSAL, "p_owner arg is NULL.");
        Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }

  if(conflicting_lock == NULL && lock_op == FSAL_OP_LOCKT)
    {
      LogDebug(COMPONENT_FSAL,
               "Conflicting_lock argument can't be NULL with lock_op  = LOCKT");
      Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_lock_op);
    }

  LogFullDebug(COMPONENT_FSAL,
               "Locking: op:%d type:%d start:%llu length:%llu owner:%p",
               lock_op, request_lock.lock_type, request_lock.lock_start,
               request_lock.lock_length, p_owner);

  if(lock_op == FSAL_OP_LOCKT)
    glock_args.cmd = F_GETLK;
  else if(lock_op == FSAL_OP_LOCK || lock_op == FSAL_OP_UNLOCK)
    glock_args.cmd = F_SETLK;
  else if(lock_op == FSAL_OP_LOCKB)
    glock_args.cmd = F_SETLKW; /*TODO: Handle FSAL_OP_CANCEL */
  else
    {
      LogDebug(COMPONENT_FSAL,
               "ERROR: Lock operation requested was not TEST, GET, or SET.");
      Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);      
    }

  if(request_lock.lock_type == FSAL_LOCK_R)
    glock_args.flock.l_type = F_RDLCK;
  else if(request_lock.lock_type == FSAL_LOCK_W)
    glock_args.flock.l_type = F_WRLCK;
  else
    {
      LogDebug(COMPONENT_FSAL,
               "ERROR: The requested lock type was not read or write.");
      Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_lock_op);
    }

  if(lock_op == FSAL_OP_UNLOCK)
    glock_args.flock.l_type = F_UNLCK;

  glock_args.flock.l_len = request_lock.lock_length;
  glock_args.flock.l_start = request_lock.lock_start;
  glock_args.flock.l_whence = SEEK_SET;

  glock_args.lfd = pfd->fd;
  glock_args.lock_owner = p_owner;
  gpfs_sg_arg.mountdirfd = gpfs_op_cxt->export_context->mount_root_fd;
  gpfs_sg_arg.lock = &glock_args;

  errno = 0;

  retval = gpfs_ganesha(lock_op == FSAL_OP_LOCKT ?
      OPENHANDLE_GET_LOCK : OPENHANDLE_SET_LOCK, &gpfs_sg_arg);

  if(retval && lock_op == FSAL_OP_LOCK)
    {
      if(conflicting_lock != NULL)
        {
          glock_args.cmd = F_GETLK;
          retval = gpfs_ganesha(OPENHANDLE_GET_LOCK, &gpfs_sg_arg);
          if(retval)
            {
              LogCrit(COMPONENT_FSAL,
                      "After failing a set lock request, An attempt to get the current owner details also failed.");
              Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock_op);
            }
          conflicting_lock->lock_owner = glock_args.flock.l_pid;
          conflicting_lock->lock_length = glock_args.flock.l_len;
          conflicting_lock->lock_start = glock_args.flock.l_start;
          conflicting_lock->lock_type = glock_args.flock.l_type;
        }
      Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock_op);
    }

  /* F_UNLCK is returned then the tested operation would be possible. */
  if(conflicting_lock != NULL)
    {
      if(lock_op == FSAL_OP_LOCKT && glock_args.flock.l_type != F_UNLCK)
        {
          conflicting_lock->lock_owner = glock_args.flock.l_pid;
          conflicting_lock->lock_length = glock_args.flock.l_len;
          conflicting_lock->lock_start = glock_args.flock.l_start;
          conflicting_lock->lock_type = glock_args.flock.l_type;
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
