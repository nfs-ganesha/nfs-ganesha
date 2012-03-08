#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include "SemN.h"
#include "log_macros.h"

semaphore_t x;

void *sem_me(void *arg)
{
        int me = (int)arg;
        semaphore_P(&x);
        printf("%d: Got it\n", me);
        sleep(1);
        semaphore_V(&x);
        printf("%d: dropped it\n", me);
}

int main()
{
        int i;
        pthread_t t[3];

        semaphore_init(&x, 2);

        for (i=0; i < 3; i++) {
                pthread_create(t+i, NULL, sem_me, (void *)i);
        }

        for (i=0; i < 3; i++) {
                pthread_join(t[i], NULL);
        }

        return 0;
}
