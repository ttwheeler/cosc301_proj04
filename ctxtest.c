#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <assert.h>

/*                                                                            
 * three thread contexts.  two for two user-mode threads, and the
 * other to store the state for the main thread in the process.
 * ctx[0] is the saved context for the main thread.
 * ctx[1] is for thread 1.
 * ctx[2] is for thread 2.
 */

static ucontext_t ctx[3];
static int shared_variable = 0;

/*
 * function entry point for the two threads.
 */
static void mythread(int threadid, int x)
{
    shared_variable += 1;
    printf("in thread %d; x is %d and shared_variable is %d\n",
           threadid, x, shared_variable);
    fflush(stdout);
    /* do a context switch from thread 1 to thread 2 (or from 2 to
     * 1)  */
    int otherthread = (threadid == 1) ? 2 : 1;
    swapcontext(&ctx[threadid], &ctx[otherthread]);

    shared_variable += 1;
    printf("back in thread %d; shared_variable is %d\n",
           threadid, shared_variable);
}


/*
 * good ol' main().
 */
int main(int argc, char **argv)
{
#define STACKSIZE 8192
    unsigned char *stack1 = (unsigned char *)malloc(STACKSIZE);
    unsigned char *stack2 = (unsigned char *)malloc(STACKSIZE);   

    assert(stack1 && stack2);

    /* initial context for thread 1 */
    getcontext(&ctx[1]);
    /* set up thread 1's stack */
    ctx[1].uc_stack.ss_sp   = stack1;
    ctx[1].uc_stack.ss_size = STACKSIZE;
    /* set up thread 1's link: when thread 1 exits, the main thread
     * (context 0) will take over */
    ctx[1].uc_link          = &ctx[0];
    /* set the thread entry point (function) for thread 1 */
    /* pass 2 argument (int values 1 and 13) to the function */
    makecontext(&ctx[1], (void (*)(void))mythread, 2, 1, 13);


    /* initial context for thread 2 */
    getcontext(&ctx[2]);
    /* set up thread 2's stack */
    ctx[2].uc_stack.ss_sp   = stack2;
    ctx[2].uc_stack.ss_size = STACKSIZE;
    /* set up thread 2's link.  when thread 2 exits, automatically
     * context switch to thread 1 */
    ctx[2].uc_link          = &ctx[1];
    /* set up thread entry point and function arguments for thread
     * 2.  pass 2 args (int values 2 and 42) to the function */
    makecontext(&ctx[2], (void (*)(void))mythread, 2, 2, 42);


    /* do a context switch.  switch from thread 0 (main thread) 
     * to thread 2 */
    swapcontext(&ctx[0], &ctx[2]);

    /* 
     * with the swapcontext() call directly above, we should first
     * start in thread 2, then swap to thread 1 (because of swapcontext()
     * call in mythread()), then swap back to thread 2 (again, because of 
     * swapcontext() call in mythread()).  thread 2 should then finish.
     * because of its context link pointer, we'll switch to thread 1.  
     * thread 1 should then finish.  because of thread 1's context link,
     * we should switch back to our main thread, and come back here. 
     */
    printf ("threads are all done.  i'm back in main!\n");

    /* free stacks and be done. */
    free (stack1);
    free (stack2);
    return 0;
}
