/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 * Test for the LRU List layer
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/LRU/test_lru.c,v 1.9 2005/11/28 17:02:39 deniel Exp $
 *
 * $Log: test_lru.c,v $
 * Revision 1.9  2005/11/28 17:02:39  deniel
 * Added CeCILL headers
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

  BuddyInit(NULL);

  if((plru = LRU_Init(param, &status)) == NULL)
    {
      printf("Test ECHOUE : Mauvaise init\n");
      exit(1);
    }

  for(i = 0; i < MAXTEST; i++)
    {
#ifdef _DEBUG_LRU
      printf("Ajout de l'entree %d\n", i);
#endif
      sprintf(strtab[i], "%d", i);
      if((entry = LRU_new_entry(plru, &status)) == NULL)
        {

          printf("Test ECHOUE : Mauvais ajout d'entree, status = %d\n", status);
          exit(1);
        }

      entry->buffdata.pdata = strtab[i];
      entry->buffdata.len = strlen(strtab[i]);

      if(i == KEPT_ENTRY)
        kept_entry = entry;
    }

  /* printing the table */
#ifdef _DEBUG_LRU
  LRU_Print(plru);
#endif

  LRU_invalidate(plru, kept_entry);

#ifdef _DEBUG_LRU
  LRU_Print(plru);
#endif

  if(LRU_gc_invalid(plru, NULL) != LRU_LIST_SUCCESS)
    {
      printf("Test ECHOUE : Mauvais gc\n");
      exit(1);
    }
#ifdef _DEBUG_LRU
  LRU_Print(plru);
#endif

  /* Tous les tests sont ok */
  printf("\n-----------------------------------------\n");
  printf("Test reussi : tous les tests sont passes avec succes\n");

  exit(0);
}                               /* main */
