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
 * Configurable test for the LRU List layer
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/LRU/test_configurable_lru.c,v 1.7 2005/11/28 17:02:39 deniel Exp $
 *
 * $Log: test_configurable_lru.c,v $
 * Revision 1.7  2005/11/28 17:02:39  deniel
 * Added CeCILL headers
 *
 * Revision 1.6  2005/05/10 11:44:02  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.5  2004/12/08 14:49:44  deniel
 * Lack of includes in test_configurable_hash.c and  test_configurable_lru.c
 *
 * Revision 1.4  2004/10/19 08:41:09  deniel
 * Lots of memory leaks fixed
 *
 * Revision 1.3  2004/10/18 08:42:43  deniel
 * Modifying prototypes for LRU_new_entry
 *
 * Revision 1.2  2004/09/23 14:34:48  deniel
 * Mise en place du test configurable
 *
 * Revision 1.1  2004/09/21 13:18:40  deniel
 *  test_configurable.c renomme en test_configurable_lru.c
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

#define LENBUF 256
#define STRSIZE 10
#define PREALLOC 1000000
#define MAXTEST 1000000

LRU_entry_t *tabentry[MAXTEST];

static int print_entry(LRU_data_t data, char *str)
{
  return snprintf(str, LRU_DISPLAY_STRLEN, "%s, len=%llu", (char *)data.pdata,
                  (unsigned long long)data.len);
}                               /* print_entry */

static int clean_entry(LRU_entry_t * pentry, void *addparam)
{
  return 0;
}                               /* cleanentry */

int do_invalidate(LRU_list_t * plru, int key)
{
  LRU_entry_t *pentry = NULL;

  pentry = tabentry[key];

  return LRU_invalidate(plru, pentry);
}

int do_new(LRU_list_t * plru, int key)
{
  char *tmpkey = NULL;

  LRU_entry_t *pentry = NULL;
  LRU_status_t status;

  if((tmpkey = (char *)malloc(STRSIZE)) == NULL)
    return -1;

  sprintf(tmpkey, "%d", key);

  if((pentry = LRU_new_entry(plru, &status)) == NULL)
    {
      free(tmpkey);
      return status;
    }

  pentry->buffdata.len = strlen(tmpkey);
  pentry->buffdata.pdata = tmpkey;

  tabentry[key] = pentry;

  return status;
}

int do_gc(LRU_list_t * plru)
{
  return LRU_gc_invalid(plru, NULL);
}

int main(int argc, char *argv[])
{
  char buf[LENBUF];
  int ok = 1;
  int hrc = 0;
  int rc = 0;
  int expected_rc;
  char c;
  char *p;
  int key;

  LRU_status_t status = 0;
  LRU_list_t *plru;
  LRU_parameter_t param;

  param.nb_entry_prealloc = PREALLOC;
  param.entry_to_str = print_entry;
  param.clean_entry = clean_entry;

  BuddyInit(NULL);

  if((plru = LRU_Init(param, &status)) == NULL)
    {
      printf("Test ECHOUE : Mauvaise init\n");
      exit(1);
    }

  /*
   *
   * La syntaxe d'un test est 
   * 'i key rc' : invalide l'entree avec la clef key
   * 'n key rc' : cree une nouvelle entree avec la clef key
   * 'g key rc' : passage du garbage collector (key ne sert a rien)
   * 'p key rc' : imprime le LRU (key et rc ne servent a rien).
   * 
   * Une ligne qui debute par '#' est un commentaire
   * Une ligne qui debute par un espace ou un tab est une ligne vide [meme si il y a des trucs derriere.. :-( ]
   * Une ligne vide (juste un CR) est une ligne vide (cette citation a recu le Premier Prix lors du Festival International 
   * de la Tautologie de Langue Francaise (FITLF), a Poully le Marais, en Aout 2004)
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

      rc = sscanf(buf, "%c %d %d", &c, &key, &expected_rc);
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
          if(rc != 3)
            {
              printf("Erreur de syntaxe : sscanf retourne %d au lieu de 3\n", rc);
              continue;
            }
          printf("---> %c %d %d\n", c, key, expected_rc);
        }

      switch (c)
        {
        case 'i':
          /* set overwrite */
          printf("invalidate  %d  --> %d ?\n", key, expected_rc);

          hrc = do_invalidate(plru, key);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: invalidate  %d : %d != %d (expected)\n", key, hrc,
                   expected_rc);
          else
            printf(">>>> OK invalidate %d\n", key);
          break;

        case 'n':
          /* test */
          printf("new %d --> %d ?\n", key, expected_rc);

          hrc = do_new(plru, key);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: new %d : %d != %d (expected)\n", key, hrc, expected_rc);
          else
            printf(">>>> OK new %d\n", key);
          break;

        case 'g':
          /* set no overwrite */
          printf("gc  %d --> %d ?\n", key, expected_rc);

          hrc = do_gc(plru);

          if(hrc != expected_rc)
            printf(">>>> ERREUR: gc %d: %d != %d (expected)\n", key, hrc, expected_rc);
          else
            printf(">>>> OK new  %d\n", key);
          break;

        case 'p':
          /* Print */
          LRU_Print(plru);
          break;

        default:
          /* syntaxe error */
          printf("ordre '%c' non-reconnu\n", c);
          break;
        }

      fflush(stdin);
    }

  printf("====================================================\n");
  printf("Test reussi : tous les tests sont passes avec succes\n");
  exit(0);
  return;
}                               /* main */
