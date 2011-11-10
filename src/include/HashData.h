/*
 * 
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
} hash_buffer_t;

typedef struct hash_data__
{
  hash_buffer_t buffval;
  hash_buffer_t buffkey;
} hash_data_t;

#endif
