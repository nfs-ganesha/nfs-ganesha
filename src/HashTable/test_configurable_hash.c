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
 * Revision 1.5  2005/11/28 17:02:37  deniel
 * Added CeCILL headers
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

#define LENBUF 256
#define STRSIZE 10
#define MAXTEST 10000           /* Plus grand que MAXDESTROY !! */
#define MAXDESTROY 5
#define MAXGET 3
#define NB_PREALLOC 10000
#define PRIME 3
#define CRITERE 12
#define CRITERE_2 14

#ifdef _NO_BUDDY_SYSTEM
#define Mem_Alloc( a )  malloc( a )
#define Mem_Free( a )   free( a )
#define BuddyErrno errno
#else
#include "BuddyMalloc.h"
#define Mem_Alloc( a ) BuddyMalloc( a )
#define Mem_Free( a ) BuddyFree( a )
#endif

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
      printf("Test ECHOUE : Pb de Mem_Alloc : keys, BuddyErrno = %d\n", BuddyErrno);
    }

  if((astrval = (char *)Mem_Alloc(MAXTEST * STRSIZE)) == NULL)
    {
      printf("Test ECHOUE : Pb de Mem_Alloc : values, BuddyErrno = %d\n", BuddyErrno);
    }

  hparam.index_size = PRIME;
  hparam.alphabet_length = STRSIZE;
  hparam.nb_node_prealloc = NB_PREALLOC;
  hparam.hash_func_key = simple_hash_func;
  hparam.hash_func_rbt = rbt_hash_func;
  hparam.compare_key = compare_string_buffer;
  hparam.key_to_str = display_buff;
  hparam.val_to_str = display_buff;

  /* Init de la table */
  if((ht = HashTable_Init(hparam)) == NULL)
    {
      printf("Test ECHOUE : Mauvaise init\n");
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
          printf("Test ECHOUE : Insertion d'une nouvelle entree impossible : %d, %d\n", i,
                 hrc);
          exit(1);
        }
#ifdef _FULL_DEBUG
      printf("Ajout de (%s,%s) , sortie = %d\n", astrkey + STRSIZE * i,
             astrval + STRSIZE * i, rc);
#endif
    }
  MesureTemps(&fin, &debut);
  printf("Ajout de %d entrees en %s secondes\n", MAXTEST, ConvertiTempsChaine(fin, NULL));
  printf("====================================================\n");

#ifdef _FULL_DEBUG
  HashTable_Print(ht);
  printf("====================================================\n");
#endif

  /*
   *
   * La syntaxe d'un test est 
   * 'g key val rc' : cherche la valeur associee a key, s'attend a lire val avec le status rc
   * 's key val rc' : positionne Hash(key) = val, s'attend a avoir le status rc
   * 'n key val rc' : idem a 's' mais cree une nouvelle entree sans en ecraser une deja existante
   * 't key val rc' : se contente de tester si la clef 'key' existe dans le HASH, avec le status rc (val ne sert a rien)
   * 'd key val rc' : detruit HASH(key), s'attend a avoir le status rc (val ne sert a rien)
   * 'p key val rc' : imprime le HASH (key, val et rc ne servent a rien).
   * 
   * Une ligne qui debute par '#' est un commentaire
   * Une ligne qui debute par un espace ou un tab est une ligne vide [meme si il y a des trucs derriere.. :-( ]
   * Une ligne vide (juste un CR) est une ligne vide (cette citation a recu le Premier Prix lors du Festival International 
   * de la Tautologie de Langue Francaise (PFITLF), a Poully le Marais, en Aout 2004)
   *
   */

  printf("============ Debut de l'interactif =================\n");

  while(ok)
    {
      /* Code interactif, pompe sur le test rbt de Jacques */
      fputs("> ", stdout);
      if((p = fgets(buf, LENBUF, stdin)) == NULL)
        {
          printf("fin des commandes\n");
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
            printf("Erreur de syntaxe : mettre un diese au debut d'un commentaire\n");

          continue;
        }
      else
        {
          if(rc != 4)
            {
              printf("Erreur de syntaxe : sscanf retourne %d au lieu de 4\n", rc);
              continue;
            }
          printf("---> %c %d %d %d\n", c, key, val, expected_rc);
        }

      switch (c)
        {
        case 's':
          /* set overwrite */
          printf("set  %d %d --> %d ?\n", key, val, expected_rc);

          hrc = do_set(ht, key, val);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: set  %d %d: %d != %d (expected)\n", key, val, hrc,
                   expected_rc);
          else
            printf(">>>> OK set  %d %d\n", key, val);
          break;

        case 't':
          /* test */
          printf("test %d %d --> %d ?\n", key, val, expected_rc);

          hrc = do_test(ht, key);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: test %d : %d != %d (expected)\n", key, hrc, expected_rc);
          else
            printf(">>>> OK test %d \n", key);
          break;

        case 'n':
          /* set no overwrite */
          printf("new  %d %d --> %d ?\n", key, val, expected_rc);

          hrc = do_new(ht, key, val);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: new  %d %d: %d != %d (expected)\n", key, val, hrc,
                   expected_rc);
          else
            printf(">>>> OK new  %d %d\n", key, val);
          break;

        case 'g':
          /* get */
          printf("get  %d %d --> %d ?\n", key, val, expected_rc);

          hrc = do_get(ht, key, &readval);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: get  %d %d: %d != %d (expected)\n", key, val, hrc,
                   expected_rc);
          else
            {
              if(hrc == HASHTABLE_SUCCESS)
                {
                  if(val != readval)
                    printf
                        (">>>> ERREUR: get %d Mauvaise valeur lue : %d != %d (expected)\n",
                         key, readval, val);
                  else
                    printf(">>>> OK get  %d %d\n", key, val);
                }
            }
          break;

        case 'd':
          /* del */
          printf("del  %d %d --> %d ?\n", key, val, expected_rc);

          hrc = do_del(ht, key);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: del  %d  %d != %d (expected)\n", key, hrc, expected_rc);
          else
            printf(">>>> OK del  %d %d\n", key, val);

          break;

        case 'p':
          /* Print */
          HashTable_Print(ht);
          break;

        default:
          /* syntaxe error */
          printf("ordre '%c' non-reconnu\n", c);
          break;
        }

      fflush(stdin);
    }

  BuddyDumpMem(stderr);

  printf("====================================================\n");
  printf("Test reussi : tous les tests sont passes avec succes\n");

  exit(0);
}
