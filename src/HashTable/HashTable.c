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
 */

/**
 * \file    HashTable.c
 * \author  $Author: deniel $
 * \date    $Date: 2006/01/31 09:12:06 $
 * \version $Revision: 1.41 $
 * \brief   Gestion des tables de hachage a base de Red/Black Trees.
 *
 * HashTable.c : gestion d'une table de hachage
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/HashTable/HashTable.c,v 1.41 2006/01/31 09:12:06 deniel Exp $
 *
 * $Log: HashTable.c,v $
 * Revision 1.41  2006/01/31 09:12:06  deniel
 * First step in efence debugging
 *
 * Revision 1.40  2006/01/27 10:28:36  deniel
 * Now support rpm
 *
 * Revision 1.39  2006/01/24 13:47:31  leibovic
 * Disabling prealloc checks when _NO_BLOCK_PREALLOC falg is set.
 *
 * Revision 1.38  2006/01/20 07:39:22  leibovic
 * Back to the previous version.
 *
 * Revision 1.35  2005/11/10 07:53:24  deniel
 * Corrected some memory leaks
 *
 * Revision 1.34  2005/08/12 07:11:14  deniel
 * Corrected cache_inode_readdir semantics
 *
 * Revision 1.33  2005/08/03 08:51:43  deniel
 * Added file exports.c in libsupport.a
 *
 * Revision 1.32  2005/08/03 07:22:17  deniel
 * Added dependence management for SemN
 *
 * Revision 1.31  2005/07/28 08:25:10  deniel
 * Adding different ifdef statemement for additional debugging
 *
 * Revision 1.30  2005/07/19 14:43:40  deniel
 * Added mnt_export support, add cache inode client init for each worker
 *
 * Revision 1.29  2005/07/11 15:30:06  deniel
 * Mount udp/tcp ok with NFSv4 on pseudo fs on kernel 2.6.11
 *
 * Revision 1.28  2005/05/10 11:43:57  deniel
 * Datacache and metadatacache are noewqw bounded
 *
 * Revision 1.27  2005/04/28 14:04:17  deniel
 * Modified HashTabel_Del prototype
 *
 * Revision 1.26  2005/04/14 14:43:09  deniel
 * Corrected a bug in HashTable_Test_And_Set (when several nodes have the same
 * rbt value)
 *
 * Revision 1.25  2005/03/02 10:56:59  deniel
 * Corrected a bug in pdata management
 *
 * Revision 1.24  2005/03/01 09:17:09  deniel
 * Added doxygen tags
 *
 * Revision 1.23  2005/02/18 09:35:49  deniel
 * Garbagge collection is ok for file (directory gc is not yet implemented)
 *
 * Revision 1.22  2004/12/15 16:18:35  deniel
 * DEBUG behaviour in HashTable_Print is now the default behaviour
 *
 * Revision 1.21  2004/11/23 16:44:58  deniel
 * Plenty of bugs corrected
 *
 * Revision 1.20  2004/11/15 16:46:16  deniel
 * Integration of pseudo fs
 *
 * Revision 1.19  2004/10/25 06:34:54  deniel
 * Multiples preallocated pool to avoid thread conflict whem inserting new
 * entries in MT environment
 *
 * Revision 1.18  2004/10/13 13:01:35  deniel
 * Now using the stuff allocator
 *
 * Revision 1.17  2004/10/11 07:05:44  deniel
 * Protection des tables de hachage par des mutex (un par RBT)
 *
 * Revision 1.16  2004/09/23 08:19:16  deniel
 * Cleaning
 *
 * Revision 1.15  2004/08/26 06:52:58  deniel
 * Bug tres con dans HashTable.c, au niveau de hashTabel_Test_And_Set (mauvaise enclosure de #ifdef)
 *
 * Revision 1.14  2004/08/25 06:21:24  deniel
 * Mise en place du test configurable ok
 *
 * Revision 1.13  2004/08/24 10:41:14  deniel
 * Avant re-ecriture d'un autre test.
 *
 * Revision 1.12  2004/08/23 16:05:20  deniel
 * Mise en palce d'un test and set a la place du set pure
 *
 * Revision 1.11  2004/08/23 09:14:35  deniel
 * Ajout de tests de non-regression (pour le delete)
 *
 * Revision 1.10  2004/08/23 08:20:55  deniel
 * Mise en place de RBT_FIND_LEFT
 *
 * Revision 1.9  2004/08/23 07:57:10  deniel
 * Injection des nouveaux rbt de Jacques
 *
 * Revision 1.8  2004/08/20 08:55:13  deniel
 * Rajout du support des statistique
 * Doxygenisation des sources
 *
 * Revision 1.7  2004/08/19 09:19:12  deniel
 * des allocations groupees par chunk
 *
 * Revision 1.6  2004/08/19 08:08:04  deniel
 * Mise au carre des tests sur les libs dynamiques et insertions des mesures
 * de temps dans les tests
 *
 * Revision 1.5  2004/08/18 14:26:18  deniel
 * La table de hachage est clean
 *
 * Revision 1.4  2004/08/18 13:49:37  deniel
 * Table de Hash avec RBT, qui marche, mais pas clean au niveau des headers
 *
 * Revision 1.3  2004/08/18 09:14:25  deniel
 * Ok pour les nouvelles tables de hash plus generiques avec des listes chaines
 *
 * Revision 1.2  2004/08/16 12:15:22  deniel
 * Premiere mise en place simple des tables de hash (mais sans RBTree)
 *
 * Revision 1.1  2004/08/16 09:35:05  deniel
 * Population de la repository avec les Hashtables et les RW_Lock
 *
 * Revision 1.4  2004/01/12 15:24:40  deniel
 * Version finalisee
 *
 * Revision 1.2  2004/01/12 14:52:08  deniel
 * Version presque finale (le del, le set et le get fonctionnent)
 *
 * Revision 1.1  2004/01/12 12:31:15  deniel
 * Premiere version des fichiers de gestion de la table de hachage, en debug
 *
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashTable.h"
#include "stuff_alloc.h"
#include "log_macros.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef P
#define P( a ) pthread_mutex_lock( &a )
#endif

#ifndef V
#define V( a ) pthread_mutex_unlock( &a )
#endif

/* ------ This group contains all the functions used to manipulate the hash table internally in this module ------ */

/**
 * @defgroup HashTableInternalFunctions
 *@{
 */

const char *hash_table_err_to_str(int err)
{
  switch(err)
    {
      case HASHTABLE_SUCCESS:                  return "HASHTABLE_SUCCESS";
      case HASHTABLE_UNKNOWN_HASH_TYPE:        return "HASHTABLE_UNKNOWN_HASH_TYPE";
      case HASHTABLE_INSERT_MALLOC_ERROR:      return "HASHTABLE_INSERT_MALLOC_ERROR";
      case HASHTABLE_ERROR_NO_SUCH_KEY:        return "HASHTABLE_ERROR_NO_SUCH_KEY";
      case HASHTABLE_ERROR_KEY_ALREADY_EXISTS: return "HASHTABLE_ERROR_KEY_ALREADY_EXISTS";
      case HASHTABLE_ERROR_INVALID_ARGUMENT:   return "HASHTABLE_ERROR_INVALID_ARGUMENT";
      case HASHTABLE_ERROR_DELALL_FAIL:        return "HASHTABLE_ERROR_DELALL_FAIL";
      case HASHTABLE_NOT_DELETED:              return "HASHTABLE_NOT_DELETED";
    }
  return "UNKNOWN HASH TABLE ERROR";
}

/**
 * 
 * simple_hash_func: A template hash function, which considers the hash key as a polynom
 *
 * A template hash function, which considers the hash key as a polynom.
 * we are supposed to managed string written with ht->alphabet_length different characters 
 * We turn the string into a binary by computing str[0]+str[1]*ht->alphabet_length+str[2]*ht->alphabet_length**2 + ...
 *  ... + str[N]*ht->alphabet_length**N  
 * Then we keep the modulo by index_size. This size has to be a prime integer for performance reason
 * The polynome is computed with the Horner's method.
 *
 * @param hparam   the parameter structure that was used to define the hashtable
 * @param buffclef the key to compute the hash value on
 *
 * @return the hash value
 *
 * @see double_hash_func
 */
unsigned long simple_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  /* we are supposed to managed string written with ht->alphabet_length different characters 
   * We turn the string into a binary by computing str[0]+str[1]*ht->alphabet_length+str[2]*ht->alphabet_length**2 + ...
   *  ... + str[N]*ht->alphabet_length**N  
   * Then we keep the modulo by index_size. This size has to be a prime integer for performance reason
   * The polynome is computed with the Horner's method */
  unsigned long i = 0;
  unsigned long h = 0;
  char c = 0;
  char *sobj = (char *)(buffclef->pdata);

  for(i = 0; i < buffclef->len; i++)
    {
      c = sobj[i];
      h = (p_hparam->alphabet_length * h + (unsigned int)c) % p_hparam->index_size;
    }

  return h;
}                               /* hash_func */

/**
 * 
 * double_hash_func: This function is used for double hashing, based on another hash function.
 *
 * This functions uses the hash function contained in hparam to compute a first hash value, then use it to 
 * compute a second value like this:  h = ( firsthash + ( 8 - ( firsthash % 8  )  ) ) % hparam.index_size 
 * This operation just changes the last 3 bits, but it can be demonstrated that this produced a more 
 * efficient and better balanced hash function (See 'Algorithm in C', Robert Sedjewick for more detail on this). 
 *
 * @param hparam   the parameter structure that was used to define the hashtable
 * @param buffclef the key to compute the hash value on
 *
 * @return the hash value
 *
 * @see double_hash_func
 */
unsigned long double_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  unsigned long firsthash = 0;
  unsigned long h = 0;
  hash_function_t hashfunc = simple_hash_func;

  /* first, we find the intial value for simple hashing */
  firsthash = hashfunc(p_hparam, buffclef);

  /* A second value is computed 
   * a second a value is added to the first one, then the modulo is kept 
   * For the second hash, we choose to change the last 3 bit, which is usually a good compromise */
  h = (firsthash + (8 - (firsthash % 8))) % p_hparam->index_size;

  return h;
}                               /* double_hash_func */

/**
 * 
 * rbt_hash_func: Another hash junction, but to be used for the red-black trees managed internally.
 *
 * This library uses red-black trees to store data. RB trees use key data too. The hash function has to be different than the one
 * used for find the RB Tree, if not all the entry in the tree will have the same hash value which will lead to a very unbalanced tree
 *
 * @param hparam   the parameter structure that was used to define the hashtable
 * @param buffclef the key to compute the hash value on
 *
 * @return the hash value
 *
 */
unsigned int rbt_hash_func(hash_parameter_t * p_hparam, hash_buffer_t * buffclef)
{
  int valeur = atoi(buffclef->pdata) + 3;

  return valeur;
}                               /* hash_func */

/**
 * 
 * Key_Locate: Locate a buffer key in the hash table, as a rbt node.
 * 
 * This function is for internal use only 
 *
 * @param ht the hashtable to be used.
 * @param buffkey a pointeur to an object of type hash_buffer_t which describe the key location in memory.
 * @param hashval hash value associated with the key (in order to avoid computing it a second time)
 * @param rbt_value rbt value associated with the key (in order to avoid computing it a second time)
 * @param ppnode if successfull,will point to the pointer to the rbt node to be used 
 *
 * @return HASHTABLE_SUCCESS if successfull\n.
 * @return HASHTABLE_NO_SUCH_KEY if key was not found 
 *
 */
static int Key_Locate(hash_table_t * ht, hash_buffer_t * buffkey, unsigned int hashval,
                      unsigned int rbt_value, struct rbt_node **ppnode)
{
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  struct rbt_node *pn;
  int found = 0;

  /* Sanity check */
  if(ht == NULL || buffkey == NULL || ppnode == NULL)
    {
      LogFullDebug(COMPONENT_HASHTABLE,
                   "Returning HASHTABLE_ERROR_INVALID_ARGUMENT");
      return HASHTABLE_ERROR_INVALID_ARGUMENT;
    }

  /* Find the head of the rbt */
  tete_rbt = &(ht->array_rbt[hashval]);

  /* I get the node with this value that is located on the left (first with this value in the rbtree) */
  RBT_FIND_LEFT(tete_rbt, pn, rbt_value);

  /* Find was successfull ? */
  if(pn == NULL)
    {
      LogFullDebug(COMPONENT_HASHTABLE,
                   "Returning HASHTABLE_ERROR_NO_SUCH_KEY because pn == NULL, rbtval = %u", rbt_value);
      return HASHTABLE_ERROR_NO_SUCH_KEY;
    }

  /* For each entry with this value, compare the key value */
  while((pn != 0) && (RBT_VALUE(pn) == rbt_value))
    {
      pdata = (hash_data_t *) RBT_OPAQ(pn);
      /* Verify the key value : this function returns 0 if key are indentical */
      if(!ht->parameter.compare_key(buffkey, &(pdata->buffkey)))
        {
          found = 1;
          break;                /* exit the while loop */
        }
      RBT_INCREMENT(pn);
    }                           /* while */

  /* We didn't find anything */
  if(!found)
    {
      LogFullDebug(COMPONENT_HASHTABLE,
                   "Returning HASHTABLE_ERROR_NO_SUCH_KEY because not found");
      return HASHTABLE_ERROR_NO_SUCH_KEY;
    }

  /* Key was found */
  *ppnode = pn;

  return HASHTABLE_SUCCESS;
}                               /* Key_Locate */

/*}@ */

/* ------ This group contains all the functions used to manipulate the hash table from outside this module ----- */

/**
 * @defgroup HashTableExportedFunctions
 *@{
 */

/**
 * 
 * HashTable_Init: Init the Hash Table.
 *
 * Init the Hash Table .
 *
 * @param hparam A structure of type hash_parameter_t which contains the values used to init the hash table.
 *
 * @return NULL if init failed, the pointeur to the hashtable otherwise.
 *
 * @see HashTable_Get
 * @see HashTable_Set
 * @see HashTable_Del
 */

hash_table_t *HashTable_Init(hash_parameter_t hparam)
{
  hash_table_t *ht;
  unsigned int i = 0;
  char *name = "Unamed";

  if (hparam.name != NULL)
    name = hparam.name;

  pthread_mutexattr_t mutexattr;

  /* Sanity check */
  if((ht = (hash_table_t *) Mem_Alloc_Label(sizeof(hash_table_t),
                                            "hash_table_t")) == NULL)
    return NULL;

  /* we have to keep the discriminant values */
  ht->parameter = hparam;

  if(pthread_mutexattr_init(&mutexattr) != 0)
    {
      Mem_Free( ht ) ;
      return NULL;
    }

  /* Initialization of the node array */
  if((ht->array_rbt =
      (struct rbt_head *)Mem_Alloc_Label(sizeof(struct rbt_head) * hparam.index_size,
                                         "rbt_head")) == NULL)
    {
      Mem_Free( ht ) ;
      return NULL;
    }

  memset((char *)ht->array_rbt, 0, sizeof(struct rbt_head) * hparam.index_size);

  /* Initialization of the stat array */
  if((ht->stat_dynamic =
      (hash_stat_dynamic_t *) Mem_Alloc_Label(sizeof(hash_stat_dynamic_t) *
                                              hparam.index_size,
                                              "hash_stat_dynamic_t")) == NULL)
    {
      Mem_Free( ht->array_rbt ) ;
      Mem_Free( ht ) ;
      return NULL;
    }

  /* Init the stats */
  memset((char *)ht->stat_dynamic, 0, sizeof(hash_stat_dynamic_t) * hparam.index_size);

  /* Initialization of the semaphores array */
  if((ht->array_lock =
      (rw_lock_t *) Mem_Alloc_Label(sizeof(rw_lock_t) * hparam.index_size,
                                    "rw_lock_t")) == NULL)
    {
      Mem_Free( ht->array_rbt ) ;
      Mem_Free( ht->stat_dynamic ) ;
      Mem_Free( ht ) ;
      return NULL;
    }

  memset((char *)ht->array_lock, 0, sizeof(rw_lock_t) * hparam.index_size);

  /* Initialize the array of pre-allocated node */
  if((ht->node_prealloc =
      (struct prealloc_pool *)Mem_Calloc_Label(hparam.index_size,
                                               sizeof(struct prealloc_pool),
                                               "rbt_node_pool")) == NULL)
    {
      Mem_Free( ht->array_rbt ) ;
      Mem_Free( ht->stat_dynamic ) ;
      Mem_Free( ht->array_lock ) ;
      Mem_Free( ht ) ;
      return NULL;
    }

  memset((char *)ht->node_prealloc, 0, sizeof(prealloc_pool) * hparam.index_size);

  if((ht->pdata_prealloc =
      (struct prealloc_pool *) Mem_Calloc_Label(hparam.index_size,
                                                sizeof(struct prealloc_pool),
                                                "hash_data_pool")) == NULL)
    {
      Mem_Free( ht->array_rbt ) ;
      Mem_Free( ht->stat_dynamic ) ;
      Mem_Free( ht->array_lock ) ;
      Mem_Free( ht->node_prealloc ) ;
      Mem_Free( ht ) ;
      return NULL;
    }

  memset((char *)ht->pdata_prealloc, 0, sizeof(prealloc_pool) * hparam.index_size);

  for(i = 0; i < hparam.index_size; i++)
    {
      LogFullDebug(COMPONENT_MEMALLOC,
                   "HASH TABLE PREALLOC: Allocating %d new nodes",
                   hparam.nb_node_prealloc);

      /* Allocate a group of nodes to be managed by the RB Tree. */
      MakePool(&ht->node_prealloc[i], hparam.nb_node_prealloc, rbt_node_t, NULL, NULL);
      NamePool(&ht->node_prealloc[i], "%s Hash RBT Nodes index %d", name, i);
      if(!IsPoolPreallocated(&ht->node_prealloc[i]))
        return NULL;

      /* Allocate a group of hash_data_t to be managed as RBT_OPAQ values. */
      MakePool(&ht->pdata_prealloc[i], hparam.nb_node_prealloc, hash_data_t, NULL, NULL);
      NamePool(&ht->pdata_prealloc[i], "%s Hash Data Nodes index %d", name, i);
      if(!IsPoolPreallocated(&ht->pdata_prealloc[i]))
        return NULL;
    }

  /* Initialize each of the RB-Tree, mutexes and stats */
  for(i = 0; i < hparam.index_size; i++)
    {
      /* RBT Init */
      RBT_HEAD_INIT(&(ht->array_rbt[i]));

      /* Mutex Init */
      if(rw_lock_init(&(ht->array_lock[i])) != 0)
        return NULL;

      /* Initialization of the stats structure */
      ht->stat_dynamic[i].nb_entries = 0;

      ht->stat_dynamic[i].ok.nb_set = 0;
      ht->stat_dynamic[i].ok.nb_get = 0;
      ht->stat_dynamic[i].ok.nb_del = 0;
      ht->stat_dynamic[i].ok.nb_test = 0;

      ht->stat_dynamic[i].err.nb_set = 0;
      ht->stat_dynamic[i].err.nb_get = 0;
      ht->stat_dynamic[i].err.nb_del = 0;
      ht->stat_dynamic[i].err.nb_test = 0;

      ht->stat_dynamic[i].notfound.nb_set = 0;
      ht->stat_dynamic[i].notfound.nb_get = 0;
      ht->stat_dynamic[i].notfound.nb_del = 0;
      ht->stat_dynamic[i].notfound.nb_test = 0;
    }

  /* final return, if we arrive here, then everything is alright */
  return ht;
}                               /* HashTable_Init */

/**
 * 
 * HashTable_Test_And_Set: set a pair (key,value) into the Hash Table.
 *
 * Set a (key,val) couple in the hashtable .
 *
 * @param ht the hashtable to be used.
 * @param buffkey a pointeur to an object of type hash_buffer_t which describe the key location in memory.
 * @param buffval a pointeur to an object of type hash_buffer_t which describe the value location in memory.
 * @param how a switch to tell if the entry is to be tested or overwritten or not
 *
 * @return HASHTABLE_SUCCESS if successfull.
 * @return HASHTABLE_INSERT_MALLOC_ERROR if an error occured during the insertion process.
 *
 * @see HashTable_Get
 * @see HashTable_Init
 * @see HashTable_Del
 */
int HashTable_Test_And_Set(hash_table_t * ht, hash_buffer_t * buffkey,
                           hash_buffer_t * buffval, hashtable_set_how_t how)
{
  unsigned int hashval = 0;
  unsigned int rbt_value = 0;
  struct rbt_head *tete_rbt = NULL;
  struct rbt_node *qn = NULL;
  struct rbt_node *pn = NULL;
  hash_data_t *pdata = NULL;

  /* Sanity check */
  if(ht == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;
  else if(buffkey == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;
  else if(buffval == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  /* Compute values to locate into the hashtable */
  if( ht->parameter.hash_func_both != NULL )
   {
      if( (*(ht->parameter.hash_func_both))( &ht->parameter, buffkey, &hashval, &rbt_value ) == 0 ) 
       return HASHTABLE_ERROR_INVALID_ARGUMENT;
   }
  else
   {
    hashval = (*(ht->parameter.hash_func_key)) (&ht->parameter, buffkey);
    rbt_value = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, buffkey);
   }

  tete_rbt = &(ht->array_rbt[hashval]);
  LogFullDebug(COMPONENT_HASHTABLE,
               "Key = %p   Value = %p  hashval = %u  rbt_value = %u",
               buffkey->pdata, buffval->pdata, hashval, rbt_value);

  /* acquire mutex for protection */
  P_w(&(ht->array_lock[hashval]));

  if(Key_Locate(ht, buffkey, hashval, rbt_value, &pn) == HASHTABLE_SUCCESS)
    {
      /* An entry of that key already exists */
      if(how == HASHTABLE_SET_HOW_TEST_ONLY)
        {
          ht->stat_dynamic[hashval].ok.nb_test += 1;
          V_w(&(ht->array_lock[hashval]));
          return HASHTABLE_SUCCESS;
        }

      if(how == HASHTABLE_SET_HOW_SET_NO_OVERWRITE)
        {
          ht->stat_dynamic[hashval].err.nb_test += 1;
          V_w(&(ht->array_lock[hashval]));
          return HASHTABLE_ERROR_KEY_ALREADY_EXISTS;
        }
      qn = pn;
      pdata = RBT_OPAQ(qn);

      LogFullDebug(COMPONENT_HASHTABLE,
                   "Entry already exists (k=%p,v=%p)",
                   buffkey->pdata, buffval->pdata);

    }
  else
    {
      /* No entry of that key, add it to the trees */
      if(how == HASHTABLE_SET_HOW_TEST_ONLY)
        {
          ht->stat_dynamic[hashval].notfound.nb_test += 1;
          V_w(&(ht->array_lock[hashval]));
          return HASHTABLE_ERROR_NO_SUCH_KEY;
        }

      /* Insert a new node in the table */
      RBT_FIND(tete_rbt, pn, rbt_value);

      /* This entry does not exist, create it */
      /* First get a new entry in the preallocated node array */
      GetFromPool(qn, &ht->node_prealloc[hashval], rbt_node_t);
      if(qn == NULL)
        {
          ht->stat_dynamic[hashval].err.nb_set += 1;
          V_w(&(ht->array_lock[hashval]));
          return HASHTABLE_INSERT_MALLOC_ERROR;
        }

      GetFromPool(pdata, &ht->pdata_prealloc[hashval], hash_data_t);
      if(pdata == NULL)
        {
          ht->stat_dynamic[hashval].err.nb_set += 1;
          V_w(&(ht->array_lock[hashval]));
          return HASHTABLE_INSERT_MALLOC_ERROR;
        }

      RBT_OPAQ(qn) = pdata;
      RBT_VALUE(qn) = rbt_value;
      RBT_INSERT(tete_rbt, qn, pn);

      LogFullDebug(COMPONENT_HASHTABLE,
                   "Create new entry (k=%p,v=%p), qn=%p, pdata=%p",
                   buffkey->pdata, buffval->pdata, qn, RBT_OPAQ(qn));
    }

  pdata->buffval.pdata = buffval->pdata;
  pdata->buffval.len = buffval->len;

  pdata->buffkey.pdata = buffkey->pdata;
  pdata->buffkey.len = buffkey->len;

  ht->stat_dynamic[hashval].nb_entries += 1;
  ht->stat_dynamic[hashval].ok.nb_set += 1;

  /* Release mutex */
  V_w(&(ht->array_lock[hashval]));

  return HASHTABLE_SUCCESS;
}                               /* HashTable_Set */

/**
 * 
 * HashTable_Get: Try to retrieve the value associated with a key.
 *
 * Try to retrieve the value associated with a key. The structure buffval will point to the object found if successfull.
 *
 * @param ht the hashtable to be used.
 * @param buffkey a pointeur to an object of type hash_buffer_t which describe the key location in memory.
 * @param buffval a pointeur to an object of type hash_buffer_t which describe the value location in memory.
 *
 * @return HASHTABLE_SUCCESS if successfull.
 * @return HASHTABLE_ERROR_NO_SUCH_KEY is the key was not found.
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Del
 */

int HashTable_GetRef(hash_table_t * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval,
                     void (*get_ref)(hash_buffer_t *) )
{
  unsigned long hashval;
  struct rbt_node *pn;
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  unsigned long rbt_value = 0;
  int rc = 0;

  /* Sanity check */
  if(ht == NULL || buffkey == NULL || buffval == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  /* Compute values to locate into the hashtable */
  if( ht->parameter.hash_func_both != NULL )
   {
     uint32_t hashval32, rbt_value32;

     if((*(ht->parameter.hash_func_both))( &ht->parameter, buffkey, &hashval32, &rbt_value32) == 0) 
       return HASHTABLE_ERROR_INVALID_ARGUMENT;

     hashval   = (unsigned long) hashval32;
     rbt_value = (unsigned long) rbt_value32;
   }
  else
   {
    hashval   = (*(ht->parameter.hash_func_key)) (&ht->parameter, buffkey);
    rbt_value = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, buffkey);
   }

  tete_rbt = &(ht->array_rbt[hashval]);

  /* Acquire mutex */
  P_r(&(ht->array_lock[hashval]));

  /* I get the node with this value that is located on the left (first with this value in the rbtree) */
  if((rc = Key_Locate(ht, buffkey, hashval, rbt_value, &pn)) != HASHTABLE_SUCCESS)
    {
      ht->stat_dynamic[hashval].notfound.nb_get += 1;
      V_r(&(ht->array_lock[hashval]));
      return rc;
    }

  /* Key was found */
  pdata = (hash_data_t *) RBT_OPAQ(pn);
  buffval->pdata = pdata->buffval.pdata;
  buffval->len = pdata->buffval.len;

  ht->stat_dynamic[hashval].ok.nb_get += 1;

  if(get_ref != NULL)
    get_ref(buffval);

  /* Release mutex */
  V_r(&(ht->array_lock[hashval]));

  return HASHTABLE_SUCCESS;
}                               /* HashTable_Get */

int HashTable_Get(hash_table_t * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval)
{
  return HashTable_GetRef(ht, buffkey, buffval, NULL);
}                               /* HashTable_Get */

/**
 * 
 * HashTable_Get_and_Del: Try to retrieve the value associated with a key and remove from the hash table if found.
 *
 * Try to retrieve the value associated with a key. The structure buffval will point to the object found if successfull.
 *
 * @param ht the hashtable to be used.
 * @param buffkey a pointeur to an object of type hash_buffer_t which describe the key location in memory.
 * @param buffval a pointeur to an object of type hash_buffer_t which describe the value location in memory.
 *
 * @return HASHTABLE_SUCCESS if successfull.
 * @return HASHTABLE_ERROR_NO_SUCH_KEY is the key was not found.
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Del
 */

int HashTable_Get_and_Del(hash_table_t  * ht,
                          hash_buffer_t * buffkey,
                          hash_buffer_t * buffval,
                          hash_buffer_t * buff_used_key)
{
  unsigned long hashval;
  struct rbt_node *pn;
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  unsigned long rbt_value = 0;
  int rc = 0;

  /* Sanity check */
  if(ht == NULL || buffkey == NULL || buffval == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  /* Compute values to locate into the hashtable */
  if( ht->parameter.hash_func_both != NULL )
   {
     uint32_t hashval32, rbt_value32;

     if((*(ht->parameter.hash_func_both))( &ht->parameter, buffkey, &hashval32, &rbt_value32) == 0) 
       return HASHTABLE_ERROR_INVALID_ARGUMENT;

     hashval   = (unsigned long) hashval32;
     rbt_value = (unsigned long) rbt_value32;
   }
  else
   {
    hashval   = (*(ht->parameter.hash_func_key)) (&ht->parameter, buffkey);
    rbt_value = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, buffkey);
   }

  tete_rbt = &(ht->array_rbt[hashval]);

  /* Acquire mutex */
  P_w(&(ht->array_lock[hashval]));

  /* I get the node with this value that is located on the left (first with this value in the rbtree) */
  if((rc = Key_Locate(ht, buffkey, hashval, rbt_value, &pn)) != HASHTABLE_SUCCESS)
    {
      ht->stat_dynamic[hashval].notfound.nb_get += 1;
      V_w(&(ht->array_lock[hashval]));
      return rc;
    }

  /* Key was found */
  pdata = (hash_data_t *) RBT_OPAQ(pn);
  *buffval = pdata->buffval;

  /* Return the key buffer back to the end user if pusedbuffkey isn't NULL */
  if(buff_used_key != NULL)
    *buff_used_key = pdata->buffkey;

  ht->stat_dynamic[hashval].ok.nb_get += 1;

  /* Now remove the entry */
  RBT_UNLINK(tete_rbt, pn);

  /* the key was located, the deletion is done */
  ht->stat_dynamic[hashval].nb_entries -= 1;

  /* put back the pdata buffer to pool */
  ReleaseToPool(pdata, &ht->pdata_prealloc[hashval]);

  /* Put the node back in the table of preallocated nodes (it could be reused) */
  ReleaseToPool(pn, &ht->node_prealloc[hashval]);

  ht->stat_dynamic[hashval].ok.nb_del += 1;

  /* Release mutex */
  V_w(&(ht->array_lock[hashval]));

  return HASHTABLE_SUCCESS;
}                               /* HashTable_Get */

/**
 * 
 * HashTable_Delall: Remove and free all (key,val) couples from the hashtable.
 *
 * Remove all (key,val) couples from the hashtable and free the stored data
 * with a function provided as the second argument.
 *
 * @param ht the hashtable to be cleared of all entries.
 * @param free_func the function with which to free the contents of each entry
 *
 * @return HASHTABLE_SUCCESS if successfull.
 * @return HASHTABLE_ERROR_NO_SUCH_KEY is the key was not found.
 *
 */
int HashTable_Delall(hash_table_t * ht, int (*free_func)(hash_buffer_t, hash_buffer_t) )
{
  struct rbt_head *head_rbt;
  hash_data_t *pdata = NULL;
  int hashval, rc;
  struct rbt_node *pn;
  struct rbt_node *qn = NULL;
  hash_buffer_t p_usedbuffkey;
  hash_buffer_t p_usedbuffdata;

  /* Sanity check */
  if (ht == NULL || free_func == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  LogFullDebug(COMPONENT_HASHTABLE, "Deleting all entries in hashtable.");

  /* For each bucket of the hashtable */
  for(hashval = 0; hashval < ht->parameter.index_size; hashval++)
    {
      head_rbt = &(ht->array_rbt[hashval]);

      /* acquire mutex */
      P_w(&(ht->array_lock[hashval]));

      /* continue until there are no more entries in the red-black-tree*/
      while(ht->stat_dynamic[hashval].nb_entries != 0)
        {
          pn = RBT_LEFTMOST(head_rbt);
          if (pn == NULL)
            break;

	  qn = pn;
          RBT_UNLINK(head_rbt, pn);
          pdata = RBT_OPAQ(qn);

	  p_usedbuffkey = pdata->buffkey;
	  p_usedbuffdata = pdata->buffval;

          /* put back the pdata buffer to pool */
          ReleaseToPool(pdata, &ht->pdata_prealloc[hashval]);
          /* Put the node back in the table of preallocated nodes (it could be reused) */
          ReleaseToPool(qn, &ht->node_prealloc[hashval]);

          /* the key was located, the deletion is done */
          ht->stat_dynamic[hashval].nb_entries -= 1;
          ht->stat_dynamic[hashval].ok.nb_del += 1;

          /* Free the data that was being stored for key and value. */
          rc = free_func(p_usedbuffkey, p_usedbuffdata);

          if (rc == 0)
            return HASHTABLE_ERROR_DELALL_FAIL;
        }
      V_w(&(ht->array_lock[hashval]));
    }

  return HASHTABLE_SUCCESS;  
}

/**
 * 
 * HashTable_Del: Remove a (key,val) couple from the hashtable.
 *
 * Remove a (key,val) couple from the hashtable. The parameter buffkey contains the key which describes the object to be removed
 * from the hash table. 
 *
 * @param ht the hashtable to be used.
 * @param buffkey a pointeur to an object of type hash_buffer_t which describe the key location in memory.
 * @param pusedbuffkeydata the key data buffer that was associated with this entry. Not considered if equal to NULL.
 *
 * @return HASHTABLE_SUCCESS if successfull.
 * @return HASHTABLE_ERROR_NO_SUCH_KEY is the key was not found.
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Get
 */
int HashTable_DelRef(hash_table_t * ht, hash_buffer_t * buffkey,
                     hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata,
                     int (*put_ref)(hash_buffer_t *) )
{
  unsigned int hashval;
  struct rbt_node *pn;
  unsigned int rbt_value = 0;
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  int rc = 0;

  /* Sanity check */
  if(ht == NULL || buffkey == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  /* Compute values to locate into the hashtable */
  if( ht->parameter.hash_func_both != NULL )
   {
      if( (*(ht->parameter.hash_func_both))( &ht->parameter, buffkey, &hashval, &rbt_value ) == 0 ) 
       return HASHTABLE_ERROR_INVALID_ARGUMENT;
   }
  else
   {
    hashval = (*(ht->parameter.hash_func_key)) (&ht->parameter, buffkey);
    rbt_value = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, buffkey);
   }

  /* acquire mutex */
  P_w(&(ht->array_lock[hashval]));

  /* We didn't find anything */
  if((rc = Key_Locate(ht, buffkey, hashval, rbt_value, &pn)) != HASHTABLE_SUCCESS)
    {
      ht->stat_dynamic[hashval].notfound.nb_del += 1;
      V_w(&(ht->array_lock[hashval]));
      return rc;
    }

  pdata = (hash_data_t *) RBT_OPAQ(pn);

  /* Return the key buffer back to the end user if pusedbuffkey isn't NULL */
  if(p_usedbuffkey != NULL)
    *p_usedbuffkey = pdata->buffkey;

  if(p_usedbuffdata != NULL)
    *p_usedbuffdata = pdata->buffval;

  if(put_ref != NULL)
    if(put_ref(&pdata->buffval) != 0)
      {
        V_w(&(ht->array_lock[hashval]));
        return HASHTABLE_NOT_DELETED;
      }

  /* Key was found */
  tete_rbt = &(ht->array_rbt[hashval]);
  RBT_UNLINK(tete_rbt, pn);

  /* the key was located, the deletion is done */
  ht->stat_dynamic[hashval].nb_entries -= 1;

  /* put back the pdata buffer to pool */
  ReleaseToPool(pdata, &ht->pdata_prealloc[hashval]);

  /* Put the node back in the table of preallocated nodes (it could be reused) */
  ReleaseToPool(pn, &ht->node_prealloc[hashval]);

  ht->stat_dynamic[hashval].ok.nb_del += 1;

  /* release mutex */
  V_w(&(ht->array_lock[hashval]));

  return HASHTABLE_SUCCESS;
}                               /*  HashTable_Del */

int HashTable_Del(hash_table_t * ht, hash_buffer_t * buffkey,
                  hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata)
{
  return HashTable_DelRef(ht, buffkey, p_usedbuffkey, p_usedbuffdata, NULL);
}

/**
 * 
 * HashTable_GetStats: Computes statistiques on the hashtable
 *
 * Print information about the hashtable (mostly for debugging purpose).
 *
 * @param ht the hashtable to be used.
 * @param hstat pointer to the result
 *
 * @return none (returns void).
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Get
 */

void HashTable_GetStats(hash_table_t * ht, hash_stat_t * hstat)
{
  unsigned int i = 0;

  /* Sanity check */
  if(ht == NULL || hstat == NULL)
    return;

  /* Firt, copy the dynamic values */
  memcpy(&(hstat->dynamic), ht->stat_dynamic, sizeof(hash_stat_dynamic_t));

  /* Then computed the other values */
  hstat->computed.min_rbt_num_node = 1 << 31;   /* A min value hash to be initialized with a huge value */
  hstat->computed.max_rbt_num_node = 0; /* A max value is initialized with 0 */
  hstat->computed.average_rbt_num_node = 0;     /* And so does the averagle value */

  hstat->dynamic.nb_entries = 0;

  hstat->dynamic.ok.nb_set = 0;
  hstat->dynamic.ok.nb_test = 0;
  hstat->dynamic.ok.nb_get = 0;
  hstat->dynamic.ok.nb_del = 0;

  hstat->dynamic.err.nb_set = 0;
  hstat->dynamic.err.nb_test = 0;
  hstat->dynamic.err.nb_get = 0;
  hstat->dynamic.err.nb_del = 0;

  hstat->dynamic.notfound.nb_set = 0;
  hstat->dynamic.notfound.nb_test = 0;
  hstat->dynamic.notfound.nb_get = 0;
  hstat->dynamic.notfound.nb_del = 0;

  for(i = 0; i < ht->parameter.index_size; i++)
    {
      if(ht->array_rbt[i].rbt_num_node > hstat->computed.max_rbt_num_node)
        hstat->computed.max_rbt_num_node = ht->array_rbt[i].rbt_num_node;

      if(ht->array_rbt[i].rbt_num_node < hstat->computed.min_rbt_num_node)
        hstat->computed.min_rbt_num_node = ht->array_rbt[i].rbt_num_node;

      hstat->computed.average_rbt_num_node += ht->array_rbt[i].rbt_num_node;

      hstat->dynamic.nb_entries += ht->stat_dynamic[i].nb_entries;

      hstat->dynamic.ok.nb_set += ht->stat_dynamic[i].ok.nb_set;
      hstat->dynamic.ok.nb_test += ht->stat_dynamic[i].ok.nb_test;
      hstat->dynamic.ok.nb_get += ht->stat_dynamic[i].ok.nb_get;
      hstat->dynamic.ok.nb_del += ht->stat_dynamic[i].ok.nb_del;

      hstat->dynamic.err.nb_set += ht->stat_dynamic[i].err.nb_set;
      hstat->dynamic.err.nb_test += ht->stat_dynamic[i].err.nb_test;
      hstat->dynamic.err.nb_get += ht->stat_dynamic[i].err.nb_get;
      hstat->dynamic.err.nb_del += ht->stat_dynamic[i].err.nb_del;

      hstat->dynamic.notfound.nb_set += ht->stat_dynamic[i].notfound.nb_set;
      hstat->dynamic.notfound.nb_test += ht->stat_dynamic[i].notfound.nb_test;
      hstat->dynamic.notfound.nb_get += ht->stat_dynamic[i].notfound.nb_get;
      hstat->dynamic.notfound.nb_del += ht->stat_dynamic[i].notfound.nb_del;
    };

  hstat->computed.average_rbt_num_node /= ht->parameter.index_size;
}

/**
 * 
 * HashTable_GetSize: Gets the number of entries in the hashtable. 
 *
 * Gets the number of entries in the hashtable. 
 *
 * @param ht the hashtable to be used.
 *
 * @return the number of found entries
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Get
 */
unsigned int HashTable_GetSize(hash_table_t * ht)
{
  unsigned int i = 0;
  unsigned int nb_entries = 0;

  /* Sanity check */
  if(ht == NULL)
    return HASHTABLE_ERROR_INVALID_ARGUMENT;

  for(i = 0; i < ht->parameter.index_size; i++)
    nb_entries += ht->stat_dynamic[i].nb_entries;

  return nb_entries;
}                               /* HashTable_GetSize */

/**
 * 
 * HashTable_Print: Print information about the hashtable (mostly for debugging purpose).
 *
 * Print information about the hashtable (mostly for debugging purpose).
 *
 * @param component the component debugging config to use.
 * @param ht the hashtable to be used.
 * @return none (returns void).
 *
 * @see HashTable_Set
 * @see HashTable_Init
 * @see HashTable_Get
 */
void HashTable_Log(log_components_t component, hash_table_t * ht)
{
  struct rbt_node *it;
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  char dispkey[HASHTABLE_DISPLAY_STRLEN];
  char dispval[HASHTABLE_DISPLAY_STRLEN];
  unsigned int i = 0;
  int nb_entries = 0;

  unsigned long rbtval;
  unsigned long hashval;

  /* Sanity check */
  if(ht == NULL)
    return;

  LogFullDebug(component,
               "The hash has %d nodes (this number MUST be a prime integer for performance's issues)",
                ht->parameter.index_size);

  for(i = 0; i < ht->parameter.index_size; i++)
    nb_entries += ht->stat_dynamic[i].nb_entries;

  LogFullDebug(component, "The hash contains %d entries", nb_entries);

  for(i = 0; i < ht->parameter.index_size; i++)
    {
      tete_rbt = &((ht->array_rbt)[i]);
      LogFullDebug(component,
                   "The node in position %d contains: %d entries",
                   i, tete_rbt->rbt_num_node);
      RBT_LOOP(tete_rbt, it)
      {
        pdata = (hash_data_t *) it->rbt_opaq;

        ht->parameter.key_to_str(&(pdata->buffkey), dispkey);
        ht->parameter.val_to_str(&(pdata->buffval), dispval);

        /* Compute values to locate into the hashtable */
        if(ht->parameter.hash_func_both != NULL)
         {
           uint32_t rbtval32;
           uint32_t hashval32;
           if((*(ht->parameter.hash_func_both))(&ht->parameter, &(pdata->buffkey), &hashval32, &rbtval32) == 0) 
	     {
               LogCrit(component,
                       "Possible implementation error in hash_func_both");
               hashval = 0;
               rbtval  = 0;
             }
           else
             {
               hashval = rbtval32;
               rbtval  = hashval32;
             }
          }
        else
          {
            hashval = (*(ht->parameter.hash_func_key)) (&ht->parameter, &(pdata->buffkey));
            rbtval  = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, &(pdata->buffkey));
          }

        LogFullDebug(component,
                     "%s => %s; hashval=%lu rbtval=%lu",
                     dispkey, dispval, hashval, rbtval);
        RBT_INCREMENT(it);
      }
    }
}                               /* HashTable_Print */

/**
 * 
 * HashTable_Print: Print information about the hashtable (mostly for debugging purpose).
 *
 * Print information about the hashtable (mostly for debugging purpose).
 *
 * @param ht the hashtable to be used.
 * @return none (returns void).
 *
 */
void HashTable_Print( hash_table_t * ht)
{
  struct rbt_node *it;
  struct rbt_head *tete_rbt;
  hash_data_t *pdata = NULL;
  char dispkey[HASHTABLE_DISPLAY_STRLEN];
  char dispval[HASHTABLE_DISPLAY_STRLEN];
  unsigned int i = 0;
  int nb_entries = 0;

  unsigned long rbtval;
  unsigned long hashval;

  /* Sanity check */
  if(ht == NULL)
    return;

  fprintf(stderr, 
          "The hash has %d nodes (this number MUST be a prime integer for performance's issues)\n",
          ht->parameter.index_size);

  for(i = 0; i < ht->parameter.index_size; i++)
    nb_entries += ht->stat_dynamic[i].nb_entries;

  fprintf(stderr,"The hash contains %d entries\n", nb_entries);

  for(i = 0; i < ht->parameter.index_size; i++)
    {
      tete_rbt = &((ht->array_rbt)[i]);
      fprintf(stderr,
              "The node in position %d contains:  %d entries \n",
              i, tete_rbt->rbt_num_node);
      RBT_LOOP(tete_rbt, it)
      {
        pdata = (hash_data_t *) it->rbt_opaq;

        ht->parameter.key_to_str(&(pdata->buffkey), dispkey);
        ht->parameter.val_to_str(&(pdata->buffval), dispval);

        /* Compute values to locate into the hashtable */
        if( ht->parameter.hash_func_both != NULL )
         {
           if( (*(ht->parameter.hash_func_both))( &ht->parameter, &(pdata->buffkey), (uint32_t *)&hashval, (uint32_t *)&rbtval ) == 0 ) 
	     {
               LogCrit(COMPONENT_HASHTABLE,
                       "Possible implementation error at line %u file %s",
                       __LINE__, __FILE__ ) ;
               hashval = 0 ;
               rbtval = 0 ;
             }
          }
        else
          {
            hashval = (*(ht->parameter.hash_func_key)) (&ht->parameter, &(pdata->buffkey));
            rbtval = (*(ht->parameter.hash_func_rbt)) (&ht->parameter, &(pdata->buffkey));
          }

        fprintf(stderr, "%s => %s; hashval=%lu rbtval=%lu\n ",
                dispkey, dispval, hashval, rbtval);
        RBT_INCREMENT(it);
      }
    }
}                               /* HashTable_Print */


/* @} */

