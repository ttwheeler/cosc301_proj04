#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <assert.h>

#include "threadsalive.h"

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

talock_t mutex;
tacond_t condv;
int value = 0;

void thread1(void *v)
{
    fprintf(stderr, "thread1 started up\n");

    ta_lock(&mutex);
    ta_yield();
    while (value == 0)
    {
        fprintf(stderr, "thread1 going into cond_wait()\n");
        ta_wait(&mutex, &condv);
    }
    ta_unlock(&mutex);
   
    fprintf(stderr, "thread1 emerged from cond_wait()\n");
    value = 42;

    ta_yield();
    fprintf(stderr, "thread1 exiting\n");
}

void thread2(void *v)
{
    fprintf(stderr, "thread2 started up\n");

    ta_lock(&mutex);
    ta_yield();
    fprintf(stderr, "thread2 not updating value, signalling thread1\n");
    ta_signal(&condv);
    ta_unlock(&mutex);
    ta_yield();

    ta_lock(&mutex);
    ta_yield();
    fprintf(stderr, "thread2 IS updating value, signalling thread1\n");
    value = -1;
    ta_signal(&condv);
    ta_unlock(&mutex);
    fprintf(stderr, "thread2 exiting\n");
}


int main(int argc, char **argv)
{
    printf("Tester for stage 3.\n");
    ta_libinit();

    ta_lock_init(&mutex);
    ta_cond_init(&condv);

    ta_create(thread1, NULL);
    ta_create(thread2, NULL);
    
    int rv=ta_waitall();
    assert(rv==0);
    assert(value==42);

    ta_lock_destroy(&mutex);
    ta_cond_destroy(&condv);

    return 0;
}

