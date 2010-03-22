/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
 *
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 * therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fsal.h"
#include "fsal_internal.h"
#include "fsal_convert.h"

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
fsal_status_t FSAL_lock(fsal_file_t * obj_handle,
                        fsal_lockdesc_t * ldesc, fsal_boolean_t blocking)
{
  int cmd;
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  /*
   * First try a non blocking lock request. If we fail due to
   * lock already being held, and if blocking is set for
   * a child and do a waiting lock
   */
  retval = fcntl(fd, F_SETLK, &ldesc->flock);
  if (retval && ((errno == EACCES) || (errno == EAGAIN)))
    {
      if (blocking)
        {
          /*
           * Conflicting lock present create a child and
           * do F_SETLKW if we can block. The lock is already
           * added to the blocking list.
           */
          do_blocking_lock(obj_handle, ldesc);
          /* We need to send NLM4_BLOCKED reply */
          Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock);
        }
      Return(posix2fsal_error(errno), errno, INDEX_FSAL_lock);

    }
  /* granted lock. Now ask NSM to monitor the host */
  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_lock);
}

/**
 * FSAL_changelock:
 * Not implemented.
 */
fsal_status_t FSAL_changelock(fsal_lockdesc_t * lock_descriptor,        /* IN / OUT */
                              fsal_lockparam_t * lock_info      /* IN */
    )
{

  /* sanity checks. */
  if (!lock_descriptor)
    Return(ERR_FSAL_FAULT, 0, INDEX_FSAL_changelock);

  Return(ERR_FSAL_NOTSUPP, 0, INDEX_FSAL_changelock);

}

/**
 * FSAL_unlock:
 *
 */
fsal_status_t FSAL_unlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  ldesc->flock.l_type = F_UNLCK;
  retval = fcntl(fd, F_SETLK, &ldesc->flock);
  if (retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_unlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_unlock);
}

fsal_status_t FSAL_getlock(fsal_file_t * obj_handle, fsal_lockdesc_t * ldesc)
{
  int retval;
  int fd = FSAL_FILENO(obj_handle);

  errno = 0;
  retval = fcntl(fd, F_GETLK, &ldesc->flock);
  if (retval)
    Return(posix2fsal_error(errno), errno, INDEX_FSAL_getlock);

  Return(ERR_FSAL_NO_ERROR, 0, INDEX_FSAL_getlock);
}
