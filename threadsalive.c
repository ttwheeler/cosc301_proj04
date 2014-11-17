/* Tom Wheeler and Zach Schneiwiess
 * Nov 17, 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <strings.h>
#include <string.h>
#include <ucontext.h>
#include <assert.h>
#include "threadsalive.h"

/* ***************************** 
     stage 1 library functions
   ***************************** */
static struct node *head;
static int waitCalled;//To determine if wait all has been called yet
static ucontext_t mainT;//Stores main thread
static int blocked;//Number of threads currently waiting on semaphore
static int locked;//Number of threads currently waiting on lock
static int condBlocked;//Number of threads currently waiting on CV

struct node
{
    ucontext_t con;
    struct node *next;
};

void ta_create(void(*func)(void *), void *arg)
{
    struct node *insNode=malloc(sizeof(struct node));//Pointer to new node
    assert(getcontext(&insNode->con)==0);
    unsigned char *newStack=(unsigned char *) malloc(128000);//Create stack
    assert(newStack);
    insNode->con.uc_stack.ss_sp=newStack;//Assign stack and size
    insNode->con.uc_stack.ss_size=128000;
    insNode->con.uc_link=&mainT;//Link to main function
    insNode->next=NULL;
    makecontext(&insNode->con,(void(*)(void))func,1,arg);//Make the context

    if(head==NULL)head=insNode;
    else
    {
        struct node *tmp=head;
        while(tmp->next!=NULL) tmp=tmp->next;
        tmp->next=insNode;
    }
    return;
}

void ta_libinit()
{
    head=NULL;
    waitCalled=0;
    assert(getcontext(&mainT)==0);
    blocked=0;
    locked=0;
    condBlocked=0;
}

void ta_yield()
{
    if(head==NULL) return;//If no threads have been created, simply return
    else if(waitCalled==0)//ta_waitall hasn't been called
    {
        swapcontext(&mainT,&head->con);
    }
    //We are currently running from the head
    else
    {
        struct node *old=head;
        struct node *tmp=head;
        while(tmp->next!=NULL)tmp=tmp->next;
        tmp->next=old;
        head=head->next;
        old->next=NULL;
        swapcontext(&old->con,&head->con);
    }
}

int ta_waitall()
{
    if(head==NULL)return 0;//No threads yet created
    waitCalled=1;
    struct node *tmp=NULL;
    while(head!=NULL)
    {
        swapcontext(&mainT,&head->con);//If not blocked, run thread
        if(head!=NULL)//swapcontext could have cleared ready list,check for that
        {
            tmp=head;//Current head is entirely done, we can free it
            head=head->next;//Update head
            free(tmp->con.uc_stack.ss_sp);//Free stack of old head
            free(tmp);//Free old head
        }
    }
    if(locked!=0||blocked!=0||condBlocked!=0) return -1;
    else return 0;
}


/* ***************************** 
     stage 2 library functions
   ***************************** */

void ta_sem_init(tasem_t *sem, int value)
{
    sem->val=value;
    sem->semQ=NULL;//Essentially the next context waiting on the semaphore
}

void ta_sem_destroy(tasem_t *sem)
{
    while(sem->semQ!=NULL)
    {
        struct node *tmp=sem->semQ;
        sem->semQ=sem->semQ->next;
        free(tmp->con.uc_stack.ss_sp);
        free(tmp);
    }
}

void ta_sem_post(tasem_t *sem)
{
    sem->val++;
    if(sem->val>0&&sem->semQ!=NULL)
    {
        blocked--;
        struct node *old=sem->semQ;
        old->next=NULL;//First waiting thread will be end of ready queue
        sem->semQ=sem->semQ->next;
        struct node *tmp=head;
        while(tmp->next!=NULL)tmp=tmp->next;//Find end of ready queue
        tmp->next=old;//Place first waiting thread on end of ready queue
    }
}

void ta_sem_wait(tasem_t *sem)
{
    
    if(sem==NULL) return;
    else if(sem->val==0)
    {
        blocked++;//Increase number of blocked threads
        struct node *old=head;
        head=head->next;
        old->next=NULL;//Make sure end of queue points to NULL
        struct node *tmp=sem->semQ;
        if(tmp==NULL)sem->semQ=old;//Place head on blocked queue
        else
        {
            while(tmp->next!=NULL)tmp=tmp->next;//Find the end of the queue
            tmp->next=old;//Move current context from ready queue to end of blocked queue
        }
        swapcontext(&old->con,&head->con);
    }
    else sem->val--;//Post has been called
}

void ta_lock_init(talock_t *mutex)
{
    mutex->val=0;
    mutex->lockQ=NULL;
}

void ta_lock_destroy(talock_t *mutex)
{
    while(mutex->lockQ!=NULL)
    {
        struct node *tmp=mutex->lockQ;
        mutex->lockQ=mutex->lockQ->next;
        free(tmp->con.uc_stack.ss_sp);
        free(tmp);
    }
}

void ta_lock(talock_t *mutex)
{
    if(mutex->val==0)//Lock is unlocked
    {
        mutex->val=1;
        return;
    }
    else//Lock is locked
    {
        locked++;//Increment count of locked threads
        struct node *tmp=mutex->lockQ;
        struct node *old=head;
        head=head->next;//Increment ready queue
        old->next=NULL;//Old head will be end of locked queue
        if(tmp==NULL)mutex->lockQ=old;
        else
        {
            while(tmp->next!=NULL)tmp=tmp->next;//Find end of lock queue
            tmp->next=old;//Place current thread on end of locked queue
        }
        swapcontext(&old->con,&head->con);
    }
}

void ta_unlock(talock_t *mutex)
{
    mutex->val=0;
    if(mutex->lockQ==NULL) return;
    else
    {
        locked--;
        struct node *old=mutex->lockQ;
        mutex->lockQ=mutex->lockQ->next;//Increment locked queue
        if(head==NULL)head=old;
        else
        {
            struct node *tmp=head;
            while(tmp->next!=NULL)tmp=tmp->next;//Find end of ready queue
            tmp->next=old;//Place current thread on end of ready queue
            old->next=NULL;//Make thread end of queue
        }
    }
}


/* ***************************** 
     stage 3 library functions
   ***************************** */

void ta_cond_init(tacond_t *cond)
{
    cond->conQueue=NULL;
}

void ta_cond_destroy(tacond_t *cond)
{
    while(cond->conQueue!=NULL)
    {
        struct node *tmp=cond->conQueue;
        cond->conQueue=cond->conQueue->next;
        free(tmp->con.uc_stack.ss_sp);
        free(tmp);
    }
}

void ta_wait(talock_t *mutex, tacond_t *cond)
{
    if(mutex==NULL||cond==NULL)return;
    ta_unlock(mutex);
    condBlocked++;//increment conditional blocked counter
    struct node *current=head;
    head=head->next;
    current->next=NULL;//Make old head point to null
    if(cond->conQueue==NULL)cond->conQueue=current;
    else
    {
        struct node *tmp=cond->conQueue;
        while(tmp->next!=NULL)tmp=tmp->next;//Find end of queue
        tmp->next=current;//Place old head on end of queue
    }
    if(head==NULL) swapcontext(&current->con,&mainT);
    else swapcontext(&current->con,&head->con);
    ta_lock(mutex);
}

void ta_signal(tacond_t *cond)
{
    fprintf(stderr,"In signal\n");
    if(cond==NULL||cond->conQueue==NULL) return;
    condBlocked--;//decrement conditional blocked counter
    struct node *old=cond->conQueue;//Old points to old head of conQueue
    cond->conQueue=cond->conQueue->next;//Update head of conQueue
    old->next=NULL;
    struct node *tmp=head;
    while(tmp->next!=NULL)tmp=tmp->next;//Find end of ready queue
    tmp->next=old;//Place old head of conQueue on end of ready queue
    return;
}
