/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * Ce logiciel est un serveur implementant le protocole NFS.
 *
 * Ce logiciel est régi par la licence CeCILL soumise au droit français et
 * respectant les principes de diffusion des logiciels libres. Vous pouvez
 * utiliser, modifier et/ou redistribuer ce programme sous les conditions
 * de la licence CeCILL telle que diffusée par le CEA, le CNRS et l'INRIA
 * sur le site "http://www.cecill.info".
 *
 * En contrepartie de l'accessibilité au code source et des droits de copie,
 * de modification et de redistribution accordés par cette licence, il n'est
 * offert aux utilisateurs qu'une garantie limitée.  Pour les mêmes raisons,
 * seule une responsabilité restreinte pèse sur l'auteur du programme,  le
 * titulaire des droits patrimoniaux et les concédants successifs.
 *
 * A cet égard  l'attention de l'utilisateur est attirée sur les risques
 * associés au chargement,  à l'utilisation,  à la modification et/ou au
 * développement et à la reproduction du logiciel par l'utilisateur étant
 * donné sa spécificité de logiciel libre, qui peut le rendre complexe à
 * manipuler et qui le réserve donc à des développeurs et des professionnels
 * avertis possédant  des  connaissances  informatiques approfondies.  Les
 * utilisateurs sont donc invités à charger  et  tester  l'adéquation  du
 * logiciel à leurs besoins dans des conditions permettant d'assurer la
 * sécurité de leurs systèmes et ou de leurs données et, plus généralement,
 * à l'utiliser et l'exploiter dans les mêmes conditions de sécurité.
 *
 * Le fait que vous puissiez accéder à cet en-tête signifie que vous avez
 * pris connaissance de la licence CeCILL, et que vous en avez accepté les
 * termes.
 *
 * ---------------------
 *
 * Copyright CEA/DAM/DIF (2005)
 *  Contributor: Philippe DENIEL  philippe.deniel@cea.fr
 *               Thomas LEIBOVICI thomas.leibovici@cea.fr
 *
 *
 * This software is a server that implements the NFS protocol.
 * 
 *
 * This software is governed by the CeCILL  license under French law and
 * abiding by the rules of distribution of free software.  You can  use,
 * modify and/ or redistribute the software under the terms of the CeCILL
 * license as circulated by CEA, CNRS and INRIA at the following URL
 * "http://www.cecill.info".
 *
 * As a counterpart to the access to the source code and  rights to copy,
 * modify and redistribute granted by the license, users are provided only
 * with a limited warranty  and the software's author,  the holder of the
 * economic rights,  and the successive licensors  have only  limited
 * liability.
 *
 * In this respect, the user's attention is drawn to the risks associated
 * with loading,  using,  modifying and/or developing or reproducing the
 * software by the user in light of its specific status of free software,
 * that may mean  that it is complicated to manipulate,  and  that  also
 therefore means  that it is reserved for developers  and  experienced
 * professionals having in-depth computer knowledge. Users are therefore
 * encouraged to load and test the software's suitability as regards their
 * requirements in conditions enabling the security of their systems and/or
 * data to be ensured and,  more generally, to use and operate it in the
 * same conditions as regards security.
 *
 * The fact that you are presently reading this means that you have had
 * knowledge of the CeCILL license and that you accept its terms.
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
 * Revision 1.19  2005/11/28 17:02:37  deniel
 * Added CeCILL headers
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
  hash_table_t *ht = NULL;
  hash_parameter_t hparam;
  hash_buffer_t buffval;
  hash_buffer_t buffkey;
  hash_buffer_t buffval2;
  hash_buffer_t buffkey2;
  hash_stat_t statistiques;
  int i;
  int val;
  int rc;
  int res;
  struct Temps debut, fin;
  char tmpstr[10];
  char tmpstr2[10];
  char tmpstr3[10];
  char strtab[MAXTEST][10];
  int critere_recherche = 0;
  int random_val = 0;

  hparam.index_size = PRIME;
  hparam.alphabet_length = 10;
  hparam.nb_node_prealloc = NB_PREALLOC;
  hparam.hash_func_key = simple_hash_func;
  hparam.hash_func_rbt = rbt_hash_func;
  hparam.compare_key = compare_string_buffer;
  hparam.key_to_str = display_buff;
  hparam.val_to_str = display_buff;

  BuddyInit(NULL);

  /* Init de la table */
  if((ht = HashTable_Init(hparam)) == NULL)
    {
      printf("Test ECHOUE : Mauvaise init\n");
      exit(1);
    }

  MesureTemps(&debut, NULL);
  printf("Creation de la table\n");

  for(i = 0; i < MAXTEST; i++)
    {
      sprintf(strtab[i], "%d", i);

      buffkey.len = strlen(strtab[i]);
      buffkey.pdata = strtab[i];

      buffval.len = strlen(strtab[i]);
      buffval.pdata = strtab[i];

      rc = HashTable_Set(ht, &buffkey, &buffval);
#ifdef _DEBUG_HASHTABLE
      printf("Ajout de %s , %d , sortie = %d\n", strtab[i], i, rc);
#endif
    }

  MesureTemps(&fin, &debut);
  printf("Duree de l'insertion de %d entrees: %s\n", MAXTEST,
         ConvertiTempsChaine(fin, NULL));
#ifdef _DEBUG_HASHTABLE
  printf("-----------------------------------------\n");
  HashTable_Print(ht);
#endif
  printf("=========================================\n");

  /* Premier test simple: verif de la coherence des valeurs lues */
  critere_recherche = CRITERE;

  sprintf(tmpstr, "%d", critere_recherche);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;

  MesureTemps(&debut, NULL);
  rc = HashTable_Get(ht, &buffkey, &buffval);
  MesureTemps(&fin, &debut);

  printf("Maintenant, j'essaye de recuperer %d entrees (prises au hasard, ou presque) \n",
         MAXGET);
  MesureTemps(&debut, NULL);
  for(i = 0; i < MAXGET; i++)
    {
      random_val = random() % MAXTEST;
      sprintf(tmpstr, "%d", random_val);
      buffkey2.len = strlen(tmpstr);
      buffkey2.pdata = tmpstr;

      rc = HashTable_Get(ht, &buffkey2, &buffval2);
#ifdef _DEBUG_HASHTABLE
      printf("\tLecture  de key = %s  --> %s\n", buffkey2.pdata, buffval2.pdata);
#endif
      if(rc != HASHTABLE_SUCCESS)
        {
          printf("Erreur lors de la lecture de %d = %d\n", i, rc);
          printf("Test ECHOUE : la valeur lue est incorrecte\n");
          exit(1);
        }
    }
  MesureTemps(&fin, &debut);
  printf("Duree de lecture de %d elements = %s\n", MAXGET,
         ConvertiTempsChaine(fin, NULL));

  printf("-----------------------------------------\n");

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
      printf("\t J'efface %d -> %d | %d\n",
             random_val,
             simple_hash_func(&hparam, &buffkey), rbt_hash_func(&hparam, &buffkey));

      rc = HashTable_Del(ht, &buffkey, NULL, NULL);
      if(rc != HASHTABLE_SUCCESS)
        {
          printf("Erreur lors de la destruction de %d = %d\n", random_val, rc);
          printf("Test ECHOUE : effacement incorrect\n");
          exit(1);
        }
    }
  MesureTemps(&fin, &debut);
  printf("Duree de la destruction de %d elements = %s\n", MAXDESTROY,
         ConvertiTempsChaine(fin, NULL));

  printf("-----------------------------------------\n");

  printf("Maintenant, j'essaye de recuperer %d entrees (eventuellement detruites) \n",
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
  printf("Duree de lecture de %d elements = %s\n", MAXGET,
         ConvertiTempsChaine(fin, NULL));

  printf("-----------------------------------------\n");
  printf("Ecriture d'une clef en double \n");
  sprintf(tmpstr, "%d", CRITERE_2);
  buffkey.len = strlen(tmpstr);
  buffkey.pdata = tmpstr;
  rc = HashTable_Test_And_Set(ht, &buffkey, &buffval, HASHTABLE_SET_HOW_SET_NO_OVERWRITE);
  printf("La valeur doit etre HASHTABLE_ERROR_KEY_ALREADY_EXISTS  = %d --> %d\n",
         HASHTABLE_ERROR_KEY_ALREADY_EXISTS, rc);
  if(rc != HASHTABLE_ERROR_KEY_ALREADY_EXISTS)
    {
      printf("Test ECHOUE : Clef redondante\n");
      exit(1);
    }
  printf("-----------------------------------------\n");

#ifdef _DEBUG_HASHTABLE
  HashTable_Print(ht);
  printf("-----------------------------------------\n");
#endif

  printf("Affichage des statistiques de la table \n");
  HashTable_GetStats(ht, &statistiques);
  printf(" Nombre d'entrees = %d\n", statistiques.dynamic.nb_entries);

  printf("   Operations reussies  : Set = %d,  Get = %d,  Del = %d,  Test = %d\n",
         statistiques.dynamic.ok.nb_set, statistiques.dynamic.ok.nb_get,
         statistiques.dynamic.ok.nb_del, statistiques.dynamic.ok.nb_test);

  printf("   Operations en erreur : Set = %d,  Get = %d,  Del = %d,  Test = %d\n",
         statistiques.dynamic.err.nb_set, statistiques.dynamic.err.nb_get,
         statistiques.dynamic.err.nb_del, statistiques.dynamic.err.nb_test);

  printf("   Operations 'NotFound': Set = %d,  Get = %d,  Del = %d,  Test = %d\n",
         statistiques.dynamic.notfound.nb_set, statistiques.dynamic.notfound.nb_get,
         statistiques.dynamic.notfound.nb_del, statistiques.dynamic.notfound.nb_test);

  printf
      ("  Statistiques calculees: min_rbt_node = %d,  max_rbt_node = %d,  average_rbt_node = %d\n",
       statistiques.computed.min_rbt_num_node, statistiques.computed.max_rbt_num_node,
       statistiques.computed.average_rbt_num_node);

  /* Test sur la pertinence des valeurs de statistiques */
  if(statistiques.dynamic.ok.nb_set != MAXTEST)
    {
      printf("Test ECHOUE : statistiques incorrectes: ok.nb_set \n");
      exit(1);
    }

  if(statistiques.dynamic.ok.nb_get + statistiques.dynamic.notfound.nb_get !=
     2 * MAXGET + 3)
    {
      printf("Test ECHOUE : statistiques incorrectes: *.nb_get \n");
      exit(1);
    }

  if(statistiques.dynamic.ok.nb_del != MAXDESTROY + 1
     || statistiques.dynamic.notfound.nb_del != 1)
    {
      printf("Test ECHOUE : statistiques incorrectes: *.nb_del \n");
      exit(1);
    }

  if(statistiques.dynamic.err.nb_test != 1)
    {
      printf("Test ECHOUE : statistiques incorrectes: err.nb_test \n");
      exit(1);
    }

  /* Tous les tests sont ok */
  BuddyDumpMem(stdout);

  printf("\n-----------------------------------------\n");
  printf("Test reussi : tous les tests sont passes avec succes\n");

  exit(0);
}
