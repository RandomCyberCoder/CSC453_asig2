#include "lwp.h"
#include "fp.h"
#include "roundRobin.h"
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <bits/mman.h>
#include <errno.h>
#include <stdio.h>

#define STACK_SIZE 8388608
#define MAP_ANONYMOUS 0x20
#define ADDRESS_SIZE 8
#define SYS_FAIL -1
#define NO_FD -1
#define EVEN_PAGE 0
#define NO_OFF 0
#define INIT 1
#define PREV 1

/*
lib_one is a next pointer for the next thread in a list
lib_two is a prev pointer for the prev thread in a list
*/

/*Create RoundRobin scheduler and set it to currentScheduler by default*/

struct scheduler rr_publish = {NULL, NULL, rrAdmit, rrRemove, rrNext, rrqlen};
scheduler currentScheduler = &rr_publish;

/*for global linked list of threads*/

thread threadPool = NULL;
thread callingThread; /*ASK NICO, will this even work? Seems iffy but how do we
                        know what the calling thread is, or if we're calling
                        functions from outside a thread*/

/*Create id counter*/

unsigned long threadIdCounter = INIT;

size_t create_stackSizeHelper()
{
    /*Do we need to worry about 16 byte alignment? ASK NICO*/
    size_t pageSize, howBig, MB_8, multBy;
    struct rlimit rlimStruct;
    int retVal;

    /*stacks should be a multiple of the page size*/

    pageSize = _SC_PAGESIZE;

    /*get the soft & hard resource limits*/

    retVal = getrlimit(RLIMIT_STACK, &rlimStruct);

    /*
    check if the RLIMIT_STACK exists or if the
    soft constraint is set to RLIM_INFINITY; if the
    value of retVal is negative one that means the
    call to getrlimit() has failed
    */

    if (retVal == SYS_FAIL || rlimStruct.rlim_cur == RLIM_INFINITY)
    {
        /*
        if no soft limit given, use a stack
        size of 8 MB*
        */

        MB_8 = STACK_SIZE;

        /*
        make sure stack size is a multiple of
        memory page size
        */

        if ((MB_8 % pageSize) != EVEN_PAGE)
        {
            multBy = MB_8 / pageSize;
            howBig = pageSize * (multBy);
        }
        else
        {
            howBig = MB_8;
        }
    }
    else
    {
        /*
        make sure soft resource limit is a multiple
        of the page size
        */

        if ((rlimStruct.rlim_cur % pageSize) != EVEN_PAGE)
        {
            multBy = rlimStruct.rlim_cur / pageSize;
            howBig = pageSize * multBy;
        }
        else
        {
            howBig = rlimStruct.rlim_cur;
        }
    }

    return howBig;
}

void add_thread_to_pool(thread newThread)
{
    /*Add thread to thread pool or initialize head of list if it's NULL*/
    thread currThread;

    if (threadPool == NULL)
    {
        threadPool = newThread;
        newThread->lib_one = NULL; //ED added this line
        newThread->lib_two = NULL;
    }
    else
    {
        currThread = threadPool;
        while (currThread->lib_one != NULL)
        {
            currThread = currThread->lib_one;
        }
        currThread->lib_one = newThread;
        newThread->lib_two = currThread;
    }

    /*Regardless if thread is added to pool or initializing the pool
    set its next pointer to NULL*/

    newThread->lib_one = NULL;
}

int remove_thread_from_pool(thread victim)
{
    /*Remove thread from thread pool or
    return -1 if it doesn't exist*/
    thread currThread;

    if (threadPool == NULL)
    {
        return SYS_FAIL;
    }

    /*If the only thread in the pool is the victim, set pool to NULL*/

    //ED note I don't think this implies that the victim thread is the only
    //...thread, only that it is the first thread in the pool
    if (threadPool == victim)
    {
        /*
        if(threadPool->lib_one == NULL){
            threadPool = NULL
            reutrn EXIT_SUCCESS
        }
        else{
            threadPool = threadPool->lib_one;
            threadPool->lib_two = NULL;
        }
        */
        threadPool = NULL;
        return EXIT_SUCCESS;
    }

    /*Search for the victim*/

    currThread = threadPool;
    while (currThread->lib_one != victim)
    {
        currThread = currThread->lib_one;

        /*If you run out of threads without finding the victim return -1*/
        if (currThread == NULL)
        {
            return SYS_FAIL;
        }
    }

    /*If we're the last thread, just update the prev thread's next pointer*/

    if (currThread->lib_one == NULL)
    {
        currThread->lib_two->lib_one = NULL;
        return EXIT_SUCCESS;
    }

    /*Otherwise we're between two threads and need to update each*/

    currThread->lib_two->lib_one = currThread->lib_one;
    currThread->lib_one->lib_two = currThread->lib_two;

    return EXIT_SUCCESS;
}

void lwp_wrap(lwpfun fun, void *arg)
{
    /* Call the given lwpfunction with the given argument.
     * Calls lwp exit() with its return value
     */

    int rval;
    rval = fun(arg);
    lwp_exit(rval);
}

tid_t lwp_create(lwpfun fun, void *arg)
{
    size_t howBig;
    uintptr_t getBaseLoc;
    thread newThread;
    void *stackBase;
    unsigned long *baseLoc;

    /*create the threads context and stack*/
    /*calculate howBig the stack size will be*/

    howBig = create_stackSizeHelper();
    newThread = (thread)malloc(sizeof(thread));

    /*Check if malloc failed*/

    if (newThread == NULL)
    {
        return NO_THREAD;
    }

    stackBase = mmap(NULL, howBig, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, NO_FD, NO_OFF);

    /*check if mmap failed*/

    if (stackBase == MAP_FAILED)
    {
        free(newThread);
        return NO_THREAD;
    }

    /*Store the address for lwp_wrap at the base of the stack
    so that when swaprfiles is called it returns to wrap

    Index into stack to set rdi and rsi to fun and arg so
    wrap gets called with the right arguments*/

    /*initialize the context*/

    newThread->tid = threadIdCounter++;
    newThread->stack = stackBase;
    newThread->stacksize = howBig;
    newThread->status = LWP_LIVE;
    newThread->state.rdi = (unsigned long)fun;
    newThread->state.rsi = (unsigned long)arg;
    newThread->state.fxsave = FPU_INIT;

    /*initialize the stack*/

    getBaseLoc = (uintptr_t)newThread->stack;
    getBaseLoc += howBig - (ADDRESS_SIZE * 2);

    /*"Push" the address of lwp_wrap to the top of the
    stack so that when ret happens, it pops this address
    and returns to it to execute
    Also "Push" the sbp of the stack allocated by mmap
    so it returns to the appropriate stack frame ASK NICO*/

    newThread->stack[howBig - PREV] = lwp_wrap;
    newThread->stack[howBig - (PREV * 2)] = getBaseLoc;

    /*Set rbp to the address of the top of the stack for
    wrap to use once it is returned to*/

    newThread->state.rbp = (unsigned long)(getBaseLoc);

    /*Add thread to thread pool*/

    add_thread_to_pool(newThread);

    /*Admit the new thread to the scheduler*/

    currentScheduler->admit(newThread);
}

void lwp_start(void)
{
    thread systemThread;

    /*transform calling thread(the system thread)
    into a LWP. Set up its context and admit() it
    to the scheduler. Don't allocate stack!
    IMPORTANT: DON'T DEALLOCATE THIS LWP!!!! */
    systemThread = (thread)malloc(sizeof(thread));
    if (systemThread == NULL)
    {
        errno = ENOMEM;
        perror("lwp_start() malloc() for thread context failed");
    }

    /*Initialize context. Stack attribtues are NULL
    to indicate this is the original system thread*/

    systemThread->tid = threadIdCounter++;
    systemThread->stack = NULL;
    systemThread->stacksize = NULL;
    systemThread->status = LWP_LIVE; /*ASK NICO*/

    /*Add thread to pool*/

    add_thread_to_pool(systemThread);

    /*Admit thread to scheduler*/

    currentScheduler->admit(systemThread);

    /*Set callingThread*/

    callingThread = systemThread;

    /*Yield control to thread picked by scheduler*/

    lwp_yield();
}

void lwp_yield(void)
{
    thread nextThread;
    /*Yield control to the next LWP in the schedule*/

    /*Get the next thread from the scheduler*/

    nextThread = currentScheduler->next();

    /*If there is no next thread, terminate the program*/

    if (nextThread == NULL)
    {
        exit(callingThread->status);
    }

    /*Otherwise, swap contexts*/

    swap_rfiles(&(callingThread->state), &(nextThread->state));
}

void lwp_exit(int exitval)
{
    /*Terminate the calling thread*/
    /*Not entirely sure if this counts as terminating it or
    if there's more to do. ASK NICO*/

    callingThread->status = LWP_TERM;

    /*Exit status of calling thread becomes low 8 bits of exitval*/
    /*This might be wrong spec seems confusing ASK NICO*/

    callingThread->status = MKTERMSTAT(callingThread->status, exitval);

    /*Yield control to next thread*/

    lwp_yield();
}

tid_t lwp_wait(int *status)
{
    thread currentThread;
    int waitingCounter, threadCounter;

    /*Search for terminated threads in the threadPool*/
    currentThread = threadPool;

    /*If there are no threads, return NO_THREAD*/

    if (currentThread == NULL)
    {
        return NO_THREAD;
    }

    waitingCounter, threadCounter = 0;
    while (!LWPTERMINATED(currentThread->status))
    {
        currentThread = threadPool->lib_one;

        /*If we've reached the end of the pool, block
        aka go back to the front and start again*/

        if (currentThread == NULL)
        {
            currentThread = threadPool;
        }
    }

    /*Not entirely sure the right direction to go
    with the rest of this one. There's no good way
    for us to see which threads are waiting currently.
    We can either create a global linked list for waiting
    processes, and then maybe another for terminated ones,
    or get rid of the global thread pool that uses both the
    library pointers in the thread contexts and use them for
    waiting or terminated threads. ASK NICO*/
}

scheduler lwp_set_scheduler(scheduler sched){
    scheduler oldScheduler = currentScheduler;
    currentScheduler = sched; 
    if(sched->init != NULL){
        sched->init();
    }   


    for(thread nxtThread = oldScheduler->next();
        nxtThread != NULL; nxtThread = oldScheduler->next()){
        
        oldScheduler->remove(nxtThread);
        sched->admit(nxtThread);
    }

    if(oldScheduler->shutdown != NULL){
        oldScheduler->shutdown();
    }
}

scheduler lwp_get_scheduler(){
    return currentScheduler;
}