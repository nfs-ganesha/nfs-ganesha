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
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/HashTable/test_configurable_hash.c,v 1.8 2006/01/27 10:28:36 deniel Exp $
 *
 * $Log: test_configurable_hash.c,v $
 * Revision 1.8  2006/01/27 10:28:36  deniel
 * Now support rpm
 *
 * Revision 1.7  2006/01/20 07:39:22  leibovic
 * Back to the previous version.
 *
 * Revision 1.4  2005/05/10 11:43:57  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.3  2005/04/28 14:04:18  deniel
 * Modified HashTabel_Del prototype
 *
 * Revision 1.2  2004/12/08 14:49:16  deniel
 * Lack of includes in test_configurable_hash.c
 *
 * Revision 1.1  2004/09/21 12:20:45  deniel
 * Differentiation des differents tests configurables
 *
 * Revision 1.7  2004/09/21 07:23:04  deniel
 * Correction du test de non-regression configurable (routine do_get dans test_configurable.c)
 *
 * Revision 1.6  2004/08/26 07:18:19  deniel
 * Mise a jour du test configurable
 *
 * Revision 1.5  2004/08/26 06:52:59  deniel
 * Bug tres con dans HashTable.c, au niveau de hashTabel_Test_And_Set (mauvaise enclosure de #ifdef)
 *
 * Revision 1.4  2004/08/25 08:42:29  deniel
 * Ajout de commentaires dans les tests configurables
 * Suppression des Hash bases sur les listes chainees
 *
 * Revision 1.3  2004/08/25 06:21:24  deniel
 * Mise en place du test configurable ok
 *
 * Revision 1.2  2004/08/24 14:20:19  deniel
 * Ajout d'unn nouveau test parametrable
 *
 * Revision 1.1  2004/08/24 12:03:09  deniel
 * Ajout de test-configurable.c
 *
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
#include <errno.h>
#include "HashTable.h"
#include "MesureTemps.h"
#include "log_macros.h"
#include "stuff_alloc.h"

#define LENBUF 256
#define STRSIZE 10
#define MAXTEST 10000           /* Plus grand que MAXDESTROY !! */
#define MAXDESTROY 5
#define MAXGET 3
#define NB_PREALLOC 10000
#define PRIME 3
#define CRITERE 12
#define CRITERE_2 14

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
  return snprintf(str, HASHTABLE_DISPLAY_STRLEN, "%s", pbuff->pdata);
}

unsigned long simple_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
unsigned long double_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);
unsigned long rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef);

int do_get(hash_table_t * ht, int key, int *pval)
{
  char tmpkey[STRSIZE];
  int rc = 0;
  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  sprintf(tmpkey, "%d", key);
  buffkey.pdata = tmpkey;
  buffkey.len = strlen(tmpkey);

  if((rc = HashTable_Get(ht, &buffkey, &buffval)) == HASHTABLE_SUCCESS)
    *pval = atoi(buffval.pdata);
  else
    *pval = -1;

  return rc;
}                               /* get */

int do_set(hash_table_t * ht, int key, int val)
{
  static char *tmpkey = NULL;
  static char *tmpval = NULL;

  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  if(((tmpkey = (char *)Mem_Alloc(STRSIZE)) == NULL)
     || ((tmpval = (char *)Mem_Alloc(STRSIZE)) == NULL))
    return -1;

  sprintf(tmpkey, "%d", key);
  buffkey.pdata = tmpkey;
  buffkey.len = strlen(tmpkey);

  sprintf(tmpval, "%d", val);
  buffval.pdata = tmpval;
  buffval.len = strlen(tmpval);

  return HashTable_Set(ht, &buffkey, &buffval);
}                               /* set */

int do_new(hash_table_t * ht, int key, int val)
{
  char tmpkey[STRSIZE];
  char tmpval[STRSIZE];

  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  sprintf(tmpkey, "%d", key);
  buffkey.pdata = tmpkey;
  buffkey.len = strlen(tmpkey);

  sprintf(tmpval, "%d", val);
  buffval.pdata = tmpval;
  buffval.len = strlen(tmpval);

  return HashTable_Test_And_Set(ht, &buffkey, &buffval,
                                HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
}                               /* new */

int do_del(hash_table_t * ht, int key)
{
  char tmpkey[STRSIZE];

  hash_buffer_t buffkey;

  sprintf(tmpkey, "%d", key);
  buffkey.pdata = tmpkey;
  buffkey.len = strlen(tmpkey);

  return HashTable_Del(ht, &buffkey, NULL, NULL);
}                               /* set */

int do_test(hash_table_t * ht, int key)
{
  char tmpkey[STRSIZE];

  hash_buffer_t buffkey;
  hash_buffer_t buffval;

  sprintf(tmpkey, "%d", key);
  buffkey.pdata = tmpkey;
  buffkey.len = strlen(tmpkey);

  return HashTable_Test_And_Set(ht, &buffkey, &buffval, HASHTABLE_SET_HOW_TEST_ONLY);
}

int main(int argc, char *argv[])
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_libcmc_config");
  hash_table_t *ht = NULL;
  hash_parameter_t hparam;
  hash_buffer_t buffkey;
  hash_buffer_t buffval;
  struct Temps debut;
  struct Temps fin;
  int i = 0;
  char *astrkey = NULL;
  char *astrval = NULL;
  int hrc = 0;
  int rc = 0;
  char *p;
  char buf[LENBUF];
  int ok = 1;
  int key;
  int val;
  int readval;
  int expected_rc;
  char c;

  BuddyInit(NULL);

  if((astrkey = (char *)Mem_Alloc(MAXTEST * STRSIZE)) == NULL)
    {
      printf("Test FAILED: problem with Mem_Alloc : keys, BuddyErrno = %d", BuddyErrno);
    }

  if((astrval = (char *)Mem_Alloc(MAXTEST * STRSIZE)) == NULL)
    {
      printf("Test FAILED: problem with Mem_Alloc : values, BuddyErrno = %d", BuddyErrno);
    }

  hparam.index_size = PRIME;
  hparam.alphabet_length = STRSIZE;
  hparam.nb_node_prealloc = NB_PREALLOC;
  hparam.hash_func_key = simple_hash_func;
  hparam.hash_func_rbt = rbt_hash_func;
  hparam.hash_func_both = NULL ; /* BUGAZOMEU */
  hparam.compare_key = compare_string_buffer;
  hparam.key_to_str = display_buff;
  hparam.val_to_str = display_buff;

  /* Init de la table */
  if((ht = HashTable_Init(hparam)) == NULL)
    {
      LogTest("Test ECHOUE : Mauvaise init");
      exit(1);
    }

  for(i = 0; i < MAXTEST; i++)
    {
      sprintf((astrkey + STRSIZE * i), "%d", i);
      sprintf((astrval + STRSIZE * i), "%d", i * 10);
    }

  MesureTemps(&debut, NULL);
  for(i = 0; i < MAXTEST; i++)
    {
      buffkey.len = strlen(astrkey + STRSIZE * i);
      buffkey.pdata = astrkey + STRSIZE * i;

      buffval.len = strlen(astrval + STRSIZE * i);
      buffval.pdata = astrval + STRSIZE * i;

      hrc = HashTable_Set(ht, &buffkey, &buffval);
      if(hrc != HASHTABLE_SUCCESS)
        {
          LogTest("Test FAILED: Inserting a new entry impossible : %d, %d", i,
                 hrc);
          exit(1);
        }
      if(isFullDebug(COMPONENT_HASHTABLE))
        LogTest("Adding (%s,%s) , return = %d", astrkey + STRSIZE * i,
                astrval + STRSIZE * i, rc);
    }
  MesureTemps(&fin, &debut);
  LogTest("Added %d entries in %s seconds",
          MAXTEST, ConvertiTempsChaine(fin, NULL));
  LogTest("====================================================");

  HashTable_Log(COMPONENT_HASHTABLE, ht);
  LogTest("====================================================");

  /*
   * The syntax of a test is
   * 'G key val rc': look for the value associated with a key, val expected to read with the status rc
   * 'S key val rc': positions Hash (key) = val, expects to have the status rc
   * 'No key val rc': idem 's' but creates a new entry without an already existing crush
   * 'T key val rc: simply test if the key' key 'exists in the HASH, with the status rc (val is useless)
   * 'S key val rc: destroyed HASH (key), expects to have the status rc (val is useless)
   * 'P key val rc: prints HASH (key, val and rc are useless).
   *
   * A line that starts by '#' is a comment
   * A line that starts by a space or a tab is a blank line [although there are things behind .. :-(]
   * An empty line (just CR) is a blank line (this quote has received the First Prize at the International Festival
   * Tautology of the French Language (PFITLF) has Poully the Marais,
   * in August 2004)
   *
   */

  LogTest("============ Start interactive =================");

  while(ok)
    {
      /* Code Interactive, pump testing rabbit by Jacques */
      fputs("> ", stdout);
      if((p = fgets(buf, LENBUF, stdin)) == NULL)
        {
          LogTest("end of commands");
          ok = 0;
          continue;
        }
      if((p = strchr(buf, '\n')) != NULL)
        *p = '\0';

      rc = sscanf(buf, "%c %d %d %d", &c, &key, &val, &expected_rc);
      if(c == '#')
        {
          /* # indique un commentaire */
          continue;
        }
      else if(c == ' ' || c == '\t' || rc == -1)
        {
          /* Cas d'une ligne vide */
          if(rc > 1)
            LogTest("Syntax error: put at the beginning of diese comment");

          continue;
        }
      else
        {
          if(rc != 4)
            {
              LogTest("Syntax error: sscanf returned %d instead of 4", rc);
              continue;
            }
          LogTest("---> %c %d %d %d", c, key, val, expected_rc);
        }

      switch (c)
        {
        case 's':
          /* set overwrite */
          LogTest("set  %d %d --> %d ?", key, val, expected_rc);

          hrc = do_set(ht, key, val);

          if(hrc != expected_rc)
            LogTest(">>>> ERROR: set  %d %d: %d != %d (expected)",
                    key, val, hrc, expected_rc);
          else
            LogTest(">>>> OK set  %d %d", key, val);
          break;

        case 't':
          /* test */
          LogTest("test %d %d --> %d ?", key, val, expected_rc);

          hrc = do_test(ht, key);

          if(hrc != expected_rc)
            LogTest(">>>> ERROR: test %d : %d != %d (expected)",
                    key, hrc, expected_rc);
          else
            LogTest(">>>> OK test %d ", key);
          break;

        case 'n':
          /* set no overwrite */
          LogTest("new  %d %d --> %d ?", key, val, expected_rc);

          hrc = do_new(ht, key, val);

          if(hrc != expected_rc)
            LogTest(">>>> ERROR: new  %d %d: %d != %d (expected)",
                    key, val, hrc, expected_rc);
          else
            LogTest(">>>> OK new  %d %d", key, val);
          break;

        case 'g':
          /* get */
          LogTest("get  %d %d --> %d ?", key, val, expected_rc);

          hrc = do_get(ht, key, &readval);

          if(hrc != expected_rc)
            LogTest(">>>> ERROR: get  %d %d: %d != %d (expected)",
                    key, val, hrc, expected_rc);
          else
            {
              if(hrc == HASHTABLE_SUCCESS)
                {
                  if(val != readval)
                    LogTest(">>>> ERROR: get %d Bad read value : %d != %d (expected)",
                            key, readval, val);
                  else
                    LogTest(">>>> OK get  %d %d", key, val);
                }
            }
          break;

        case 'd':
          /* del */
          LogTest("del  %d %d --> %d ?", key, val, expected_rc);

          hrc = do_del(ht, key);

          if(hrc != expected_rc)
            LogTest(">>>> ERROR: del  %d  %d != %d (expected)",
                    key, hrc, expected_rc);
          else
            LogTest(">>>> OK del  %d %d", key, val);

          break;

        case 'p':
          /* Print */
          HashTable_Log(COMPONENT_HASHTABLE, ht);
          break;

        default:
          /* syntaxe error */
          LogTest("command '%c' not recognized", c);
          break;
        }

      fflush(stdout);
    }

  BuddyDumpMem(stderr);

  LogTest("====================================================");
  LogTest("Test succeeded: all tests pass successfully");

  exit(0);
}
