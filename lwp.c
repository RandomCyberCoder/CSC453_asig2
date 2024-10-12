#include "lwp.h"
#include "fp.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
/*
schedule_one is the next thread in the list
schedule_two is the previous thread in the list
*/
/*
The thread at the start of the list is the next thread that
will run
*/

thread head = NULL;
thread tail = NULL;
unsigned long qLen = 0;

void rrInit(){
    //do we want to do anything here?? Can't see why we would.
}

void rrShutdown(){
    //do we want to do anything here?????
    /*
    we don't use any dynamic memory so maybe not.... if another scheduler
    is given could this be useful?? I don't think we wan't any of that logic
    here though.
    */
}

void rrAdmit(thread new){
    if(head == NULL){
        /*
        If there are no threads, initialize list
        with the new thread, assigning next and prev
        pointers to NULL and our number of ready processes
        to 1. Set head and tail to the new thread
        */
        head = new;
        tail = new;
        new->sched_one = NULL;
        new->sched_two = NULL;
        qLen = 1;
    }
    else{
        /*
        If there are scheduled threads, set the
        next pointer of the tail thread
        to the new thread, set the next pointer
        of the new thread to NULL, set the previous
        pointer of the new thread to the current tail,
        and update the tail pointer to the new thread.
        Increment the number of ready processes 
        */
        tail->sched_one = new;
        new->sched_one = NULL;
        new->sched_two = tail;
        tail = new;
        qLen += 1;
    }
}

void rrRemove(thread victim){
    /*if the thread is the first victim*/
    if(victim == head){
        head = head->sched_one;
        qLen -= 1;
        if(qLen == 0){
            head == NULL;
            tail == NULL;
        }
        return;
    }

    /*find the victim thread*/
    thread curThread = head;
    while(curThread != victim){
        curThread = curThread->sched_one;
    }
    /*once the victim thread has been found, remove it from the scheduler.
    and have the thread before it an after it point to one another.
    */
    thread prvThread = curThread->sched_two;
    thread nxtThread = curThread->sched_one;
    prvThread->sched_one = nxtThread;
    if(nxtThread != NULL){
        /*if the thread removed was no the last one in the list*/
        nxtThread->sched_two = prvThread;
    }
    else{
        /*if the thread removed was the last one in the list, update
        the tail of the list*/
        tail = prvThread;
    }

    /*lower the thread count*/
    qLen -= 1;

}

thread rrNext(){
    /*NOTE: the first thread in the list is the next thread that will run.*/

    thread nextThread;
    if(qLen == 1){
        /*if there is only one thread available*/
        return head;
    }
    else{
        /*thread we will return*/
        nextThread = head;

        thread oldTailThread = tail;
        thread newHeadThread = head->sched_one;

        /*the tail is updated to the thread at the start of the list*/
        oldTailThread->sched_one = head;
        /*
        the current head of the list should be updated to point to the
        old tail of the list and the next pointer (sched_one) should be 
        null
        */
        head->sched_two = oldTailThread;
        head->sched_one = NULL;
        /*
        update the tail to point to the new tail and the head to point the
        next thread in the list
        */
        tail = head;
        head = newHeadThread;
    }
    
    return nextThread;
}

int rrqlen(){
    return qLen;
}

tid_t lwp_create(lwpfun fun,void *arg){
    /*calculate howBig the stack size should be*/
    /*stacks should be a multiple of the page size*/
    size_t pageSize = _SC_PAGESIZE;
    /*get the resource limits */
    //check the following two lines of the code; delte this comment after
    struct rlimit rlimStruct;
    int retVal = getrlimit(RLIMIT_STACK, &rlimStruct);
    /*
    check if the RLIMIT_STACK exists or ? -> if the structures was set to 
    RLIM_INFINITY; if the value of retVal is negative one that means the 
    call to getrlimit() has failed
    */
    if(retVal == -1 || (&rlimStruct) == RLIM_INFINITY){
        /*if no soft limit given, use a max stack size of
        8 MB*/
        size_t MB_8 = 8388608;
        if((MB_8 % pageSize) != 0){

        }

    }
    else{
        rlim_t softResourceLimit = rlimStruct.rlim_cur;
    }
    size_t howBig = 0;
    void *stackBase = mmap(NULL, howBig, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

}