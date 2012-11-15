/**
 *
 * @file    SemN.h
 * @brief   Portable system tools.
 *
 * Defines system utilities (like semaphores)
 * so that they are POSIX and work on
 * most plateforms.
 *
 * @deprecated This file is going away once we have all FSAL's ported
 * to the new API.
 */

#ifndef TOOLS_H
#define TOOLS_H

#include <pthread.h>
#include <semaphore.h>

/*
 *  Synchronization features
 */

typedef struct semaphore
{
  sem_t semaphore;
} semaphore_t;

/** Initializes a semaphore. */
int semaphore_init(semaphore_t * sem, unsigned int value);
/** Destroys a semaphore. */
int semaphore_destroy(semaphore_t * sem);

/** Takes a token. */
void semaphore_P(semaphore_t * sem);
/** Give back a token. */
void semaphore_V(semaphore_t * sem);

#endif
