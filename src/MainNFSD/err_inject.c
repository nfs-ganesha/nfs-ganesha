/*
 * Copyright IBM Corporation, 2011
 *  Contributor: Frank Filz  <ffilzlnx@us.ibm.com>
 *
 * --------------------------
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

#ifdef _SOLARIS
#include "solaris_port.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

/* function name resolution */
#include "nfs_stat.h"

#include "nfs_core.h"
#include "nfs_stat.h"
#include "nfs_exports.h"
#include "external_tools.h"
#include "snmp_adm.h"

#include "stuff_alloc.h"
#include "common_utils.h"
#include "log_macros.h"

int getErrInjectInteger(snmp_adm_type_union * param, void *opt)
{
  long option = (long)opt;

  switch(option)
    {
      case 0: param->integer = 0; break;
      default: return 1;
    }

  return 0;
}

int setErrInjectInteger(const snmp_adm_type_union * param, void *opt)
{
  long option = (long)opt;

  switch(option)
    {
      case 0: param->integer; break;
      default: return 1;
    }

  return 0;
}

static register_get_set snmp_error_injection[] = {

  {"worker_delay", "Delay for each request processed by worker threads", SNMP_ADM_INTEGER, SNMP_ADM_ACCESS_RW,
   getErrInjectInteger, setErrInjectInteger, (void *)0},
};

#define SNMPADM_ERROR_INJECTION_COUNT 1

int init_error_injector()
{
   if(snmp_adm_register_get_set_function(INJECT_OID, snmp_error_injection,
                                          SNMPADM_ERROR_INJECTION_COUNT))
     {
       LogCrit(COMPONENT_INIT, "Error registering error injection to SNMP");
       return 2;
     }

  return 0;
}
