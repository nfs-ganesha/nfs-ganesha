/*
 *
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
 */

/**
 * \file    HashTable.h
 * \author  $Author: leibovic $
 * \date    $Date: 2006/01/20 07:36:19 $
 * \version $Revision: 1.35 $
 * \brief   Gestion des tables de hachage a base de Red/Black Trees.
 *
 * HashTable.h : gestion d'une table de hachage
 *
 *
 */

#ifndef _HASHTABLE_H
#define _HASHTABLE_H

#include <rbt_node.h>
#include <rbt_tree.h>
#include <pthread.h>
#include "RW_Lock.h"
#include "HashData.h"

/**
 * @defgroup HashTableStructs
 * 
 * @{
 */

typedef struct hashparameter__ *p_hash_parameter_t;

typedef struct hashparameter__ {
  unsigned int index_size;                                    /**< Number of rbtree managed, this MUST be a prime number. */
  unsigned int alphabet_length;                               /**< Number of characters used to write the buffer (polynomial approach). */
  unsigned int nb_node_prealloc;                              /**< Number of node to allocated when new nodes are necessary. */
  unsigned long (*hash_func_key) (p_hash_parameter_t, hash_buffer_t *);     /**< Hashing function, returns an integer from 0 to index_size - 1 . */
  unsigned long (*hash_func_rbt) (p_hash_parameter_t, hash_buffer_t *);     /**< Rbt value calculator (for rbt management). */
  int (*compare_key) (hash_buffer_t *, hash_buffer_t *);                        /**< Function used to compare two keys together. */
  int (*key_to_str) (hash_buffer_t *, char *);                                  /**< Function used to convert a key to a string. */
  int (*val_to_str) (hash_buffer_t *, char *);                                  /**< Function used to convert a value to a string. */
} hash_parameter_t;

typedef unsigned long (*hash_function_t) (hash_parameter_t *, hash_buffer_t *);
typedef long (*hash_buff_comparator_t) (hash_buffer_t *, hash_buffer_t *);
typedef long (*hash_key_display_convert_func_t) (hash_buffer_t *, char *);
typedef long (*hash_val_display_convert_func_t) (hash_buffer_t *, char *);

typedef struct hashstat_op__ {
  unsigned int nb_set;   /**< Number of 'set' operations,  */
  unsigned int nb_test;  /**< Number of 'test' operations, */
  unsigned int nb_get;   /**< Number of 'get' operations,  */
  unsigned int nb_del;   /**< Number of 'del' operations,  */
} hash_stat_op_t;

typedef struct hashstat_dynamic__ {
  unsigned int nb_entries;   /**< Number of entries managed in the HashTable. */
  hash_stat_op_t ok;         /**< Statistics of the operation that completed successfully. */
  hash_stat_op_t err;        /**< Statistics of the operation that failed. */
  hash_stat_op_t notfound;   /**< Statistics of the operation that returned HASHTABLE_ERROR_NO_SUCH_KEY */
} hash_stat_dynamic_t;

typedef struct hashstat_computed__ {
  unsigned int min_rbt_num_node;      /**< Minimum size (in number of node) of the rbt used. */
  unsigned int max_rbt_num_node;      /**< Maximum size (in number of node) of the rbt used. */
  unsigned int average_rbt_num_node;  /**< Average size (in number of node) of the rbt used. */
} hash_stat_computed_t;

typedef struct hashstat__ {
  hash_stat_dynamic_t dynamic;    /**< Dynamic statistics (computed on the fly). */
  hash_stat_computed_t computed;  /**< Statistics computed when HashTable_GetStats is called. */
} hash_stat_t;

typedef struct hashtable__ {
  hash_parameter_t parameter;           /**< Definition parameter for the HashTable */
  hash_stat_dynamic_t *stat_dynamic;    /**< Dynamic statistics for the HashTable. */
  struct rbt_head *array_rbt;           /**< Array of reb-black tree (of size parameter.index_size) */
  rw_lock_t *array_lock;                /**< Array of rw-locks for MT-safe management */
  struct rbt_node **node_prealloc;      /**< Pre-allocated nodes, ready to use for new entries (array of size parameter.nb_node_prealloc) */
  hash_data_t **pdata_prealloc;         /**< Pre-allocated pdata buffers  ready to use for new entries */
} hash_table_t;

typedef enum hashtable_set_how__ { HASHTABLE_SET_HOW_TEST_ONLY = 1,
  HASHTABLE_SET_HOW_SET_OVERWRITE = 2,
  HASHTABLE_SET_HOW_SET_NO_OVERWRITE = 3
} hashtable_set_how_t;

/* @} */

/* How many character used to display a key or value */
#define HASHTABLE_DISPLAY_STRLEN 1024

/* Possible errors */
#define HASHTABLE_SUCCESS                  0
#define HASHTABLE_UNKNOWN_HASH_TYPE        1
#define HASHTABLE_INSERT_MALLOC_ERROR      2
#define HASHTABLE_ERROR_NO_SUCH_KEY        3
#define HASHTABLE_ERROR_KEY_ALREADY_EXISTS 4
#define HASHTABLE_ERROR_INVALID_ARGUMENT   5

unsigned long double_hash_func(hash_parameter_t * hc, hash_buffer_t * buffclef);
hash_table_t *HashTable_Init(hash_parameter_t hc);
int HashTable_Test_And_Set(hash_table_t * ht, hash_buffer_t * buffkey,
                           hash_buffer_t * buffval, hashtable_set_how_t how);
int HashTable_Get(hash_table_t * ht, hash_buffer_t * buffkey, hash_buffer_t * buffval);
int HashTable_Del(hash_table_t * ht, hash_buffer_t * buffkey,
                  hash_buffer_t * p_usedbuffkey, hash_buffer_t * p_usedbuffdata);
#define HashTable_Set( ht, buffkey, buffval ) HashTable_Test_And_Set( ht, buffkey, buffval, HASHTABLE_SET_HOW_SET_OVERWRITE )
void HashTable_GetStats(hash_table_t * ht, hash_stat_t * hstat);
void HashTable_Print(hash_table_t * ht);
unsigned int HashTable_GetSize(hash_table_t * ht);

#endif                          /* _HASHTABLE_H */
