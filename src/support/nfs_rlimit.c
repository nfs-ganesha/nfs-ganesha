#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "log.h"


static const uint32_t FD_FALLBACK_LIMIT = 0x400;
static const uint32_t FD_WARN_LIMIT = 4096;     /* only used for warning. same as INR_OPEN_MAX in linux/fs.h */
static const uint32_t FD_MAX_LIMIT = 0x20000000;


/* called from main as a first order of business */
long nfs_rlimit_read_os_fd_rlimit(void)
{
  long retval = 0;

  /* Rlimit for open file descriptors */
  struct rlimit rlim = {
    .rlim_cur = RLIM_INFINITY,
    .rlim_max = RLIM_INFINITY
  };
  /* Return code from system calls */
  int code = 0;

  struct rlimit _rlim;
  bool did_raise = false;

  /* Find out the system-imposed file descriptor limit */
  if(getrlimit(RLIMIT_NOFILE, &rlim) != 0)
    {
      code = errno;
      LogCrit(COMPONENT_CACHE_INODE_LRU,
              "Call to getrlimit failed with error %d.  "
              "This should not happen.  Assigning default of %d.", code, FD_FALLBACK_LIMIT);
      return FD_FALLBACK_LIMIT;
    }

  retval = rlim.rlim_cur;
  _rlim.rlim_cur = _rlim.rlim_max = rlim.rlim_max;
  if(_rlim.rlim_cur > FD_MAX_LIMIT)     /* If rlim.rlim_max is 2**63-1 then this gives a */
    _rlim.rlim_cur = FD_MAX_LIMIT;      /* fds_system_imposed of -1 which is a bug; so cap it. */
  if(!setrlimit(RLIMIT_NOFILE, &_rlim))
    {
      retval = _rlim.rlim_cur;
      did_raise = true;
    }
  else
    {
/* Note that, there is no such thing as a system that (a) has a
sizeof(rlim.rlim_cur) less than 4, or (b) can handle anywhere near 500
million open files; hence this loop below always works: */
      while(rlim.rlim_cur <= FD_MAX_LIMIT && !setrlimit(RLIMIT_NOFILE, &rlim))
        {
          retval = rlim.rlim_cur;
          rlim.rlim_cur *= 2;
          did_raise = true;
        }
    }
  if(!did_raise)
    {
/* Log an *error* since the admin wants to know that his file-server is
going to under-perform: */
      LogCrit(COMPONENT_CACHE_INODE_LRU, "Error attempting to raise soft FD limit [%s].", strerror(errno));
      LogCrit(COMPONENT_CACHE_INODE_LRU, "Soft FD limit set to %ld.", retval);
    }
  else if(retval < FD_WARN_LIMIT)
    {
      LogWarn(COMPONENT_CACHE_INODE_LRU, "Soft FD limit set to %ld. This should be at least %ld; "
              "check your OS settings.", retval, (long) FD_WARN_LIMIT);
    }
  else
    {
      LogInfo(COMPONENT_CACHE_INODE_LRU, "Soft FD limit set to %ld.", retval);
    }

  return retval;
}


