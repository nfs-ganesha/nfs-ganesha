#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>     /* atoi */
#include <stdbool.h>
#include <assert.h>
#include "log.h"


static int cpu_core_count = 1;

int
nfs_cpu_cores()
{
  assert(cpu_core_count >= 1);
  return cpu_core_count;
}

/* called from main() as a first order of business */
void nfs_cpu_cores_read_os_cpu_core_count(void)
{
  bool error = true;
  char s[16] = "";
  FILE *p;
/* This script works on FreeBSD, MacOSX, and Linux, but there is no reliable way of
getting this information on Solaris -- possible need ganesha config entry for that. */
  p = popen("( sysctl -n hw.ncpu || ls -1d /sys/devices/system/cpu/cpu[0-9]* | wc -l ) 2>/dev/null", "r");
  if(p)
    {
      int ncpu;
      fgets(s, sizeof(s), p);
      ncpu = atoi(s);
      if(ncpu >= 1)
        {
          cpu_core_count = ncpu;
          LogInfo(COMPONENT_DISPATCH, "OS reports %d CPUs", cpu_core_count);
          error = false;
        }
      pclose(p);
    }
  if(error)
    {
      LogCrit(COMPONENT_INIT, "Failed reading number of OS CPUs. See %s to add support.", __FILE__);
    }
}

