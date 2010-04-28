/*
 * 
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * PUT LGPL HERE
 * ---------------------------------------
 *
 * Definition of the data to be stored in the hash table 
 *
 */

#ifndef _HASH_DATA_H
#define _HASH_DATA_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>

/*
 * 
 * Defintion of buffers to be used into the HashTable
 *
 */
typedef struct hashbuff__
{
  caddr_t pdata;
  size_t len;
  unsigned int type;
  struct hashbuff__ *next_alloc;        /* for stuff allocator */
} hash_buffer_t;

typedef struct hash_data__
{
  hash_buffer_t buffval;
  hash_buffer_t buffkey;
  struct hash_data__ *next_alloc;
} hash_data_t;

#endif
