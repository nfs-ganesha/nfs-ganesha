/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright IBM  (2011)
 * contributor : Frank Filz  ffilz@us.ibm.com
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
 * ---------------------------------------
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <pthread.h>
#include "log.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <malloc.h>
#include "log.h"
#include "ganesha_rpc.h"
#include "nlm_list.h"
#include "nlm_util.h"
#include "nlm_async.h"
#include "nfs_core.h"


static char usage[] =
    "Usage :\n"
    "\ttest_support <test_name>\n\n"
    "\twhere <test_name> is:\n"

int main(int argc, char **argv)
{
  int rc;

  SetDefaultLogging("TEST");
  SetNamePgm("test_support");
  InitLogging();

  LogTest("%s", usage);

  return 0;
}
