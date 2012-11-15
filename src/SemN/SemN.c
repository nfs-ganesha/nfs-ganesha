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
 * @file  SemN.c
 * @brief Portable system tools.
 *
 * Implements system utilities (like semaphores)
 * so that they are POSIX and work on
 * most plateforms.
 *
 * @deprecated This file is going away.  Soon.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include "SemN.h"
#include <stdio.h>
#include <semaphore.h>

#define MODULE "SemN"

int semaphore_init(semaphore_t * sem, unsigned int value)
{
  if(!sem)
    return EINVAL;

   return sem_init(&sem->semaphore,
                   0 /* Not shared accross processes */, 
                   value) ;
}

int semaphore_destroy(semaphore_t * sem)
{

  if(!sem)
    return EINVAL;

  return sem_destroy( &sem->semaphore ) ;
}

void semaphore_P(semaphore_t * sem)
{
    sem_wait( &sem->semaphore ); 
}

void semaphore_V(semaphore_t * sem)
{
    sem_post( &sem->semaphore ) ;
}
