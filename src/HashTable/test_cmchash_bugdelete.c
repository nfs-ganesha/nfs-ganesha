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
 *
 * Source du test de la lib de gestion des tables de hachage
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/HashTable/test_cmchash_bugdelete.c,v 1.3 2006/01/20 07:39:22 leibovic Exp $
 *
 * $Log: test_cmchash_bugdelete.c,v $
 * Revision 1.3  2006/01/20 07:39:22  leibovic
 * Back to the previous version.
 *
 * Revision 1.1  2005/11/29 13:38:15  deniel
 * bottlenecked ip_stats
 *
 * Revision 1.18  2005/10/05 14:03:28  deniel
 * DEBUG ifdef are now much cleaner
 *
 * Revision 1.17  2005/05/10 11:43:57  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.16  2005/04/28 14:04:17  deniel
 * Modified HashTabel_Del prototype
 *
 * Revision 1.15  2005/03/02 10:57:00  deniel
 * Corrected a bug in pdata management
 *
 * Revision 1.14  2004/12/08 15:47:17  deniel
 * Inclusion of systenm headers has been reviewed
 *
 * Revision 1.13  2004/09/20 15:36:01  deniel
 * Quelques mods mineures au niveau des #define
 *
 * Revision 1.12  2004/08/25 06:28:15  deniel
 * Remise a plat du test
 *
 * Revision 1.11  2004/08/24 10:41:15  deniel
 * Avant re-ecriture d'un autre test.
 *
 * Revision 1.10  2004/08/23 09:14:35  deniel
 * Ajout de tests de non-regression (pour le delete)
 *
 * Revision 1.9  2004/08/20 08:55:13  deniel
 * Rajout du support des statistique
 * Doxygenisation des sources
 *
 * Revision 1.8  2004/08/19 09:44:07  deniel
 * Ajout d'autres tests dans test_cmchash et maketest.conf
 *
 * Revision 1.7  2004/08/19 09:20:39  deniel
 * Ajout du header CVS dans test_cnmhash.c
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
#include "HashTable.h"
#include "MesureTemps.h"
#include "log_macros.h"

#define MAXTEST 10000           /* Plus grand que MAXDESTROY !! */
#define MAXDESTROY 50
#define MAXGET 30
#define NB_PREALLOC 10000
#define PRIME 109
#define CRITERE 12
#define CRITERE_2 14
int bugdelete_key_array[] = { 4738,
  6483,
  8393,
  9661,
  2990,
  1054,
  9823,
  6386,
  4029,
  3533,
  7074,
  7339,
  5170,
  9363,
  6,
  9754,
  5144,
  2236,
  8039,
  9545,
  5622,
  2693,
  1265,
  9980,
  2834,
  1583,
  1106,
  8567,
  8503,
  8196,
  2963,
  9593,
  4679,
  7708,
  9254,
  4022,
  8762,
  5430,
  408,
  2791,
  8963,
  3835,
  6484,
  6485,
  8394,
  9666,
  2999,
  1055,
  9824,
  6387,
  4030,
  3534
};

int compare_string_buffer(hash_buffer_t * buff1, hash_buffer_t * buff2)
{
  /* Test if one of teh entries are NULL */
  if(buff1->pdata == NULL)
    return (buff2->pdata == NULL) ? 0 : 1;
  else
    {
      if(buff2->pdata == NULL)
        return -1;              /* left member is the greater one */
      else
        return strcmp(buff1->pdata, buff2->pdata);
    }
  /* This line should never be reached */
}

int display_buff(hash_buffer_t * pbuff, char *str)
{
  return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "%s", (char *)pbuff->pdata);
}

unsigned long simple_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
unsigned long double_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
unsigned long rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);

int main(int argc, char *argv[])
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_libcmc_bugdelete");
  LogTest("Initialized test program");
  
  hash_table_t *ht = NULL;
  hash_parameter_t hparam;
  hash_buffer_t buffval;
  hash_buffer_t buffkey;
  hash_buffer_t buffval2;
  hash_buffer_t buffkey2;
  hash_stat_t statistiques;
  int i;
  int rc;
  struct Temps debut, fin;
  char tmpstr[10];
  char strtab[MAXTEST][10];
  int critere_recherche = 0;
  int random_val = 0;

  hparam.index_size = PRIME;
  hparam.alphabet_length = 10;
  hparam.nb_node_prealloc = NB_PREALLOC;
  hparam.hash_func_key = simple_hash_func;
  hparam.hash_func_rbt = rbt_hash_func;
  hparam.hash_func_both = NULL ; /* BUGAZOMEU */
  hparam.compare_key = compare_string_buffer;
  hparam.key_to_str = display_buff;
  hparam.val_to_str = display_buff;

  BuddyInit(NULL);

  /* Init de la table */
  if((ht = HashTable_Init(hparam)) == NULL)
    {
      LogTest("Test FAILED: Bad init");
      exit(1);
    }

  MesureTemps(&debut, NULL);
  LogTest("Created hash table");

  for(i = 0; i < MAXTEST; i++)
    {
      sprintf(strtab[i], "%d", i);

      buffkey.len = strlen(strtab[i]);
      buffkey.pdata = strtab[i];

      buffval.len = strlen(strtab[i]);
      buffval.pdata = strtab[i];

      rc = HashTable_Set(ht, &buffkey, &buffval);
      LogFullDebug(COMPONENT_HASHTABLE,
                   "Added %s , %d , return code = %d", strtab[i], i, rc);
    }

  MesureTemps(&fin, &debut);
  LogTest("Time to insert %d entries: %s", MAXTEST,
         ConvertiTempsChaine(fin, NULL));

  LogFullDebug(COMPONENT_HASHTABLE,
               "-----------------------------------------");
  HashTable_Log(COMPONENT_HASHTABLE, ht);
  LogFullDebug(COMPONENT_HASHTABLE,
               "=========================================");

  /* Premier test simple: verif de la coherence des valeurs lues */
  critere_recherche = CRITERE;

  sprintf(tmpstr, "%d", critere_recherche);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;

  MesureTemps(&debut, NULL);
  rc = HashTable_Get(ht, &buffkey, &buffval);
  MesureTemps(&fin, &debut);

  LogTest("Now, I try to retrieve %d entries (taken at random, almost)",
          MAXGET);

  MesureTemps(&debut, NULL);
  for(i = 0; i < MAXGET; i++)
    {
      random_val = random() % MAXTEST;
      sprintf(tmpstr, "%d", random_val);
      buffkey2.len = strlen(tmpstr);
      buffkey2.pdata = tmpstr;

      rc = HashTable_Get(ht, &buffkey2, &buffval2);
      LogTest("\tPlaying key = %s  --> %s", buffkey2.pdata, buffval2.pdata);
      if(rc != HASHTABLE_SUCCESS)
        {
          LogTest("Error reading %d = %d", i, rc);
          LogTest("Test FAILED: the reading is incorrect");
          exit(1);
        }
    }
  MesureTemps(&fin, &debut);
  LogTest("Time to read elements %d = %s", MAXGET,
         ConvertiTempsChaine(fin, NULL));

  LogTest("-----------------------------------------");

  sprintf(tmpstr, "%d", critere_recherche);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;

  sprintf(tmpstr, "%d", critere_recherche);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;

  sprintf(tmpstr, "%d", critere_recherche);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;

  srandom(getpid());

  MesureTemps(&debut, NULL);
  for(i = 0; i < MAXDESTROY; i++)
    {
      random_val = bugdelete_key_array[i];
      sprintf(tmpstr, "%d", random_val);

      buffkey.len = strlen(tmpstr);
      buffkey.pdata = tmpstr;
      LogFullDebug(COMPONENT_HASHTABLE, "\t Erase %u -> %lu | %lu",
                   random_val,
                   simple_hash_func(&hparam, &buffkey),
                   rbt_hash_func(&hparam, &buffkey));

      rc = HashTable_Del(ht, &buffkey, NULL, NULL);
      if(rc != HASHTABLE_SUCCESS)
        {
          LogTest("Erreur lors de la destruction de %d = %d", random_val, rc);
          LogTest("Test FAILED: delete incorrect");
          exit(1);
        }
    }
  MesureTemps(&fin, &debut);
  LogTest("Time to delete %d elements = %s", MAXDESTROY,
          ConvertiTempsChaine(fin, NULL));

  LogTest("-----------------------------------------");

  LogTest("Now, I try to retrieve %d entries (possibly destroyed)",
          MAXGET);

  MesureTemps(&debut, NULL);
  for(i = 0; i < MAXGET; i++)
    {
      random_val = random() % MAXTEST;
      sprintf(tmpstr, "%d", random_val);
      buffkey.len = strlen(tmpstr);
      buffkey.pdata = tmpstr;

      rc = HashTable_Get(ht, &buffkey, &buffval);
    }
  MesureTemps(&fin, &debut);
  LogTest("Time to read %d elements = %s", MAXGET,
          ConvertiTempsChaine(fin, NULL));

  LogTest("-----------------------------------------");
  LogTest("Writing a duplicated key");
  sprintf(tmpstr, "%d", CRITERE_2);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffval, HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
  LogTest("The value must be HASHTABLE_ERROR_KEY_ALREADY_EXISTS  = %d --> %d",
         HASHTABLE_ERROR_KEY_ALREADY_EXISTS, rc);
  if(rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      LogTest("Test ECHOUE : Clef redondante");
      exit(1);
    }
  LogTest("-----------------------------------------");

  HashTable_Log(COMPONENT_HASHTABLE,ht);
  LogFullDebug(COMPONENT_HASHTABLE,"-----------------------------------------");

  LogTest("Displaying table statistics");
  HashTable_GetStats(ht, &statistiques);
  LogTest(" Number of Entrees = %d", statistiques.dynamic.nb_entries);

  LogTest(" Successful operations : Set = %d,  Get = %d,  Del = %d,  Test = %d",
          statistiques.dynamic.ok.nb_set, statistiques.dynamic.ok.nb_get,
          statistiques.dynamic.ok.nb_del, statistiques.dynamic.ok.nb_test);

  LogTest("   Failed operations : Set = %d,  Get = %d,  Del = %d,  Test = %d",
          statistiques.dynamic.err.nb_set, statistiques.dynamic.err.nb_get,
          statistiques.dynamic.err.nb_del, statistiques.dynamic.err.nb_test);

  LogTest("   Operations 'NotFound': Set = %d,  Get = %d,  Del = %d,  Test = %d",
          statistiques.dynamic.notfound.nb_set, statistiques.dynamic.notfound.nb_get,
          statistiques.dynamic.notfound.nb_del, statistiques.dynamic.notfound.nb_test);

  LogTest("  Statistics computed: min_rbt_node = %d,  max_rbt_node = %d,  average_rbt_node = %d",
          statistiques.computed.min_rbt_num_node, statistiques.computed.max_rbt_num_node,
          statistiques.computed.average_rbt_num_node);

  /* Test sur la pertinence des valeurs de statistiques */
  if(statistiques.dynamic.ok.nb_set != MAXTEST)
    {
      LogTest("Test FAILED: Incorrect statistics: ok.nb_set ");
      exit(1);
    }

  if(statistiques.dynamic.ok.nb_get + statistiques.dynamic.notfound.nb_get !=
     2 * MAXGET + 1)
    {
      LogTest("Test FAILED: Incorrect statistics: *.nb_get.  Expected %d, got %d",
              2 * MAXGET + 1,
              statistiques.dynamic.ok.nb_get + statistiques.dynamic.notfound.nb_get);
      exit(1);
    }

  if(statistiques.dynamic.ok.nb_del != MAXDESTROY)
    {
      LogTest("Test FAILED: Incorrect statistics: *.nb_del. Expected %d, got %d",
              MAXDESTROY, statistiques.dynamic.ok.nb_del);
      exit(1);
    }

  if(statistiques.dynamic.notfound.nb_del != 0)
    {
      LogTest("Test FAILED: Incorrect statistics: *.nb_del. Expected %d, got %d",
              0, statistiques.dynamic.notfound.nb_del);
      exit(1);
    }



  if(statistiques.dynamic.err.nb_test != 1)
    {
      LogTest("Test FAILED: Incorrect statistics: err.nb_test ");
      exit(1);
    }

  /* Tous les tests sont ok */
  BuddyDumpMem(stdout);

  LogTest("\n-----------------------------------------");
  LogTest("Test succeeded: all tests pass successfully");

  exit(0);
}
