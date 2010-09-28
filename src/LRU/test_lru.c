/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
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
 * Test for the LRU List layer
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/LRU/test_lru.c,v 1.9 2005/11/28 17:02:39 deniel Exp $
 *
 * Revision 1.8  2005/10/05 14:03:29  deniel
 * DEBUG ifdef are now much cleaner
 *
 * Revision 1.7  2005/05/10 11:44:02  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.6  2004/12/08 15:47:18  deniel
 * Inclusion of systenm headers has been reviewed
 *
 * Revision 1.5  2004/10/19 08:41:09  deniel
 * Lots of memory leaks fixed
 *
 * Revision 1.4  2004/10/18 08:42:43  deniel
 * Modifying prototypes for LRU_new_entry
 *
 * Revision 1.3  2004/09/21 12:21:05  deniel
 * Differentiation des differents tests configurables
 * Premiere version clean
 *
 * Revision 1.2  2004/09/20 15:36:18  deniel
 * Premiere implementation, sans prealloc
 *
 * Revision 1.1  2004/09/01 14:52:24  deniel
 * Population de la branche LRU
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include "BuddyMalloc.h"
#include "LRU_List.h"
#include "log_macros.h"

#define PREALLOC 10000
#define MAXTEST 10
#define KEPT_ENTRY 5

int print_entry(LRU_data_t data, char *str)
{
  return snprintf(str, LRU_DISPLAY_STRLEN, "%s", (char *)data.pdata);
}                               /* print_entry */

int clean_entry(LRU_entry_t * pentry, void *addparam)
{
  return 0;
}                               /* cleanentry */

int main(int argc, char *argv[])
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_lru");

  LRU_list_t *plru;
  LRU_parameter_t param;
  LRU_entry_t *entry = NULL;
  LRU_entry_t *kept_entry = NULL;
  LRU_status_t status = 0;
  int i = 0;
  char strtab[MAXTEST][10];

  param.nb_entry_prealloc = PREALLOC;
  param.entry_to_str = print_entry;
  param.clean_entry = clean_entry;
  param.name = "Test";

  BuddyInit(NULL);

  if((plru = LRU_Init(param, &status)) == NULL)
    {
      LogTest("Test FAILED: Bad Init");
      exit(1);
    }

  for(i = 0; i < MAXTEST; i++)
    {
      LogTest("Added entry %d", i);
      sprintf(strtab[i], "%d", i);
      if((entry = LRU_new_entry(plru, &status)) == NULL)
        {

          LogTest("Test FAILED: bad entry add, status = %d", status);
          exit(1);
        }

      entry->buffdata.pdata = strtab[i];
      entry->buffdata.len = strlen(strtab[i]);

      if(i == KEPT_ENTRY)
        kept_entry = entry;
    }

  /* printing the table */
  LRU_Print(plru);

  LRU_invalidate(plru, kept_entry);

  if(isFullDebug(COMPONENT_LRU))
    LRU_Print(plru);

  if(LRU_gc_invalid(plru, NULL) != LRU_LIST_SUCCESS)
    {
      LogTest("Test FAILED: bad gc");
      exit(1);
    }
  LRU_Print(plru);

  /* Tous les tests sont ok */
  LogTest("\n-----------------------------------------");
  LogTest("Test succeeded: all tests pass successfully");

  exit(0);
}                               /* main */
