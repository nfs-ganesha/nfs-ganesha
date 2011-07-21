/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * test_rw.c: test program for rw locks 
 *
 * $Header: /cea/home/cvs/cvs/SHERPA/BaseCvs/GANESHA/src/RW_Lock/test_rw.c,v 1.3 2004/08/16 14:48:38 deniel Exp $
 *
 * $Log: test_rw.c,v $
 * Revision 1.3  2004/08/16 14:48:38  deniel
 * Distrib de RW_Lock et HashTable avec les tests de non regression
 *
 * Revision 1.2  2004/08/16 12:49:09  deniel
 * Mise a plat ok pour HashTable et RWLock
 *
 * Revision 1.1  2004/08/16 09:35:08  deniel
 * Population de la repository avec les Hashtables et les RW_Lock
 *
 * Revision 1.5  2003/12/19 16:31:52  deniel
 * Resolution du pb de deadlock avec des variables de conditions
 *
 * Revision 1.4  2003/12/19 13:18:58  deniel
 * Identification du pb de deadlock: on ne peux pas unlocker deux fois de suite
 *
 * Revision 1.3  2003/12/18 14:15:38  deniel
 * Correction d'un probleme lors de la declaration des init de threads
 *
 * Revision 1.1.1.1  2003/12/17 10:29:49  deniel
 * Recreation de la base 
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include "RW_Lock.h"
#include "log_macros.h"

#define MAX_WRITTERS 3
#define MAX_READERS 5
#define NB_ITER 40
#define MARGE_SECURITE 10

rw_lock_t lock;

int OkWrite = 0;
int OkRead = 0;

void *thread_writter(void *arg)
{
  int duree_sleep = 1;
  int nb_iter = NB_ITER;

  while(nb_iter > 0)
    {
      P_w(&lock);
      sleep(duree_sleep);
      V_w(&lock);
      nb_iter -= 1;
    }
  OkWrite = 1;                  /* Ecriture concurrente ici, mais on s'en fout (pas d'impact) */
  return NULL;
}                               /* thread_writter */

void *thread_reader(void *arg)
{
  int duree_sleep = 1;
  int nb_iter = NB_ITER;

  while(nb_iter > 0)
    {
      P_r(&lock);
      sleep(duree_sleep);
      V_r(&lock);
      nb_iter -= 1;
    }
  OkRead = 1;                   /* Ecriture concurrente ici, mais on s'en fout (pas d'impact) */
  return NULL;
}                               /* thread_writter */

int main(int argc, char *argv[])
{
  SetDefaultLogging("TEST");
  SetNamePgm("test_rw");
  pthread_attr_t attr_thr;
  pthread_t ThrReaders[MAX_READERS];
  pthread_t ThrWritters[MAX_WRITTERS];
  int i;
  int rc;

  pthread_attr_init(&attr_thr);
  pthread_attr_setscope(&attr_thr, PTHREAD_SCOPE_SYSTEM);
  pthread_attr_setdetachstate(&attr_thr, PTHREAD_CREATE_JOINABLE);

  LogTest("Init lock: %d", rw_lock_init(&lock));

  LogTest("ESTIMATED TIME OF TEST: %d s",
         (MAX_WRITTERS + MAX_READERS) * NB_ITER + MARGE_SECURITE);
  fflush(stdout);

  for(i = 0; i < MAX_WRITTERS; i++)
    {
      if((rc =
          pthread_create(&ThrWritters[i], &attr_thr, thread_writter, (void *)NULL)) != 0)
        {
          LogTest("pthread_create: Error %d %d ", rc, errno);
          LogTest("RW_Lock Test FAILED: Bad allocation thread");
          exit(1);
        }
    }

  for(i = 0; i < MAX_READERS; i++)
    {
      if((rc =
          pthread_create(&ThrReaders[i], &attr_thr, thread_reader, (void *)NULL)) != 0)
        {
          LogTest("pthread_create: Error %d %d ", rc, errno);
          LogTest("RW_Lock Test FAILED: Bad allocation thread");
          exit(1);
        }
    }

  LogTest("Main thread sleeping while threads run locking tests");

  sleep((MAX_WRITTERS + MAX_READERS) * NB_ITER + MARGE_SECURITE);

  LogTest("End of sleep( %d ) ",
         (MAX_WRITTERS + MAX_READERS) * NB_ITER + MARGE_SECURITE);
  if((OkWrite == 1) && (OkRead == 1))
    {
      LogTest("Test RW_Lock succeeded: no deadlock detected");
      exit(0);
      return 0;                 /* for compiler */
    }
  else
    {
      if(OkWrite == 0)
        LogTest("RW_Lock test FAIL: deadlock in the editors");
      if(OkRead == 0)
        LogTest("RW_Lock Test FAILED: deadlock in the drive");
      exit(1);
      return 1;                 /* for compiler */

    }

}                               /* main */
