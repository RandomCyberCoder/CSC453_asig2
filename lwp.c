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
#define ALIGN_FACTOR 16
#define ADDRESS_SIZE 8
#define SYS_FAIL -1
#define NO_FD -1
#define NO_SHUTDOWN 0
#define EVEN_PAGE 0
#define NO_OFF 0
#define SHUTDOWN 1
#define INIT 1
#define NEXT 1
#define EMPTY 0

/*
lib_one is a next pointer for the next thread in a list
lib_two is a prev pointer for the previous thread in a list
*/

void terminateThread(thread victim, int *status, int shouldSD);

/*Create RoundRobin scheduler and set it to currentScheduler by default*/

struct scheduler rr_publish = {NULL,
                               NULL,
                               rrAdmit,
                               rrRemove,
                               rrNext,
                               rrqlen};
scheduler currentScheduler = &rr_publish;

/*for global linked list of threads*/

thread threadPool = NULL;
thread waiting = NULL;
thread terminated = NULL;
thread callingThread = NULL;

/*Create id counter*/

unsigned long threadIdCounter = INIT;

size_t create_stackSizeHelper()
{
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

    /*Ensure the size is also 16 byte aligned*/

    while (howBig % ALIGN_FACTOR)
    {
        howBig += pageSize;
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
        newThread->lib_one = NULL;
        newThread->lib_two = NULL;
    }
    else
    {
        /*Search for the end of the pool*/

        currThread = threadPool;
        while (currThread->lib_one != NULL)
        {
            currThread = currThread->lib_one;
        }

        /*Adjust pointers at the end of the pool*/
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

    currThread = threadPool;

    /*If we're the first thread, just update the threadpool head*/

    if (currThread == victim)
    {
        threadPool = threadPool->lib_one;
        return EXIT_SUCCESS;
    }

    /*Search for the victim*/

    while (currThread->lib_one != victim)
    {
        currThread = currThread->lib_one;

        /*If you run out of threads without finding the victim return -1*/
        if (currThread == NULL)
        {
            return SYS_FAIL;
        }
    }

    /*Set currThread to the victim*/

    currThread = currThread->lib_one;

    /*If we're the last thread, just update the NEXT thread's next pointer*/

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
    unsigned long *registerAddress;

    /*create the threads context and stack*/
    /*calculate howBig the stack size will be*/

    howBig = create_stackSizeHelper();
    newThread = (thread)malloc(sizeof(context));

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

    /*Pointer to the base of the stack*/

    getBaseLoc = (uintptr_t)newThread->stack;

    /*Increment to the top of the stack and subtract
    3 memory addresses worth so we can start writing
    our register values to the stack*/
    getBaseLoc += howBig - (ADDRESS_SIZE * 3);
    registerAddress = (unsigned long *)getBaseLoc;

    /*"Push" the sbp of the stack allocated by mmap
    so it returns to the appropriate stack frame*/

    registerAddress[0] = (unsigned long)(getBaseLoc +
                                         ADDRESS_SIZE * 2);

    /*Then "Push" the address of lwp_wrap to the top of the
    stack so that when ret happens, it pops this address
    and returns to it to execute*/
    registerAddress[NEXT] = (unsigned long)lwp_wrap;

    /*Then "Push" a dummy sbp for alignment purposes*/

    registerAddress[2 * NEXT] = (unsigned long)(getBaseLoc +
                                                ADDRESS_SIZE * 4);

    /*Set rbp to the address of the rbp reg on our stack
    so that when it is popped inside leave and ret, it pops
    our rbp filler value and then the address of lwp_wrap for ret*/

    newThread->state.rbp = (unsigned long)(getBaseLoc);

    /*Add thread to thread pool*/

    add_thread_to_pool(newThread);

    /*Admit the new thread to the scheduler*/

    currentScheduler->admit(newThread);

    /*Return its tid*/

    return newThread->tid;
}

void lwp_start(void)
{
    thread systemThread;

    /*transform calling thread(the system thread)
    into a LWP. Set up its context and admit() it
    to the scheduler. Don't allocate stack!
    IMPORTANT: DON'T DEALLOCATE THIS LWP!!!! */

    systemThread = (thread)malloc(sizeof(context));
    if (systemThread == NULL)
    {
        errno = ENOMEM;
        perror("lwp_start() malloc() for thread context failed");
        exit(EXIT_FAILURE);
    }

    /*Initialize context. Stack attributes are NULL
    to indicate this is the original system thread
    Register values of context don't matter because they will
    be saved to the correct values when yield is called*/

    systemThread->tid = threadIdCounter++;
    systemThread->stack = NULL;
    systemThread->stacksize = EMPTY;
    systemThread->status = LWP_LIVE;

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
    thread oldThread, nextThread;
    unsigned int status;
    /*Yield control to the next LWP in the schedule*/

    /*Get the next thread from the scheduler*/

    nextThread = currentScheduler->next();

    /*If there is no next thread, terminate the program*/

    if (nextThread == NULL)
    {
        /*Save exit status of calling thread*/

        status = callingThread->status;

        /*Terminate the calling thread and Exit*/

        terminateThread(callingThread, NULL, SHUTDOWN);
        exit(status);
    }

    /*Otherwise, set callingThread and swap contexts*/

    oldThread = callingThread;
    callingThread = nextThread;

    swap_rfiles(&(oldThread->state), &(nextThread->state));
}

void lwp_exit(int exitval)
{
    thread currThread;

    /*Terminate the calling thread*/

    callingThread->status = LWP_TERM;

    /*Exit status of calling thread becomes low 8 bits of exitval*/

    callingThread->status = MKTERMSTAT(callingThread->status, exitval);

    /*Remove the thread from the scheduler*/

    currentScheduler->remove(callingThread);

    /*Add it to the terminated list*/

    /*If there are no threads terminated, make it the head*/

    if (terminated == NULL)
    {
        terminated = callingThread;
    }
    else
    {
        /*Search for the end of the terminated list*/

        currThread = terminated;
        while (currThread->exited != NULL)
        {
            currThread = currThread->exited;
        }

        /*And add the calling thread to the end*/

        currThread->exited = callingThread;
    }

    /*Set next pointer at end of list to NULL*/

    callingThread->exited = NULL;

    /*Check if there's any waiting threads*/

    currThread = waiting;

    /*If there is a thread waiting, reschedule it*/

    if (currThread != NULL)
    {
        /*Remove it from the queue*/

        waiting = waiting->exited;

        /*Reschedule the waiting thread*/

        currentScheduler->admit(currThread);
    }

    /*Yield control to next thread*/

    lwp_yield();
}

tid_t lwp_wait(int *status)
{
    thread terminatedThread, waitingThread;
    tid_t tid;

    /*Search for terminated threads in the terminated list*/

    terminatedThread = terminated;

    /*If there are no terminated threads, add the
    calling thread to the waiting thread list*/

    if (terminatedThread == NULL)
    {
        /*Deschedule the calling thread first*/

        currentScheduler->remove(callingThread);

        /*Add it to waiting thread list*/

        waitingThread = waiting;

        if (waitingThread == NULL)
        {
            /*If waiting list is empty, set head to caller*/

            waiting = callingThread;
        }
        else
        {
            /*Search for end of waiting thread list*/

            while (waitingThread->exited != NULL)
            {
                waitingThread = waitingThread->exited;
            }

            /*And add the calling thread to the end*/

            waitingThread->exited = callingThread;
        }

        /*Set terminated/waiting pointer of caller to NULL*/

        callingThread->exited = NULL;

        /*If there are no more runnable threads return NO_THREAD*/

        if (currentScheduler->qlen() < 1)
        {
            /*Set status to terminated*/

            callingThread->status = LWP_TERM;

            /*Get the tid*/

            tid = callingThread->tid;

            /*Terminate the thread*/

            terminateThread(callingThread, status, SHUTDOWN);

            return tid;
        }

        /*Otherwise "block" by yielding*/

        lwp_yield();

        /*Get the oldest terminated thread now that we're back*/

        terminatedThread = terminated;
    }

    /*Get the id of the thread*/

    tid = terminatedThread->tid;

    /*Update the terminated list*/

    terminated = terminated->exited;

    /*Terminate the thread and deallocate resources*/

    terminateThread(terminatedThread, status, NO_SHUTDOWN);

    return tid;
}

thread tid2thread(tid_t tid)
{
    thread checkThread = threadPool;

    while (checkThread != NULL)
    {
        /*If we've found the desired thread
        return the thread pointer*/

        if (tid == checkThread->tid)
        {
            return checkThread;
        }

        /*Else check the next thread*/

        checkThread = checkThread->lib_one;
    }

    /*If the thread wasn't found return NULL*/

    return NULL;
}

tid_t lwp_gettid(void)
{
    /*Return calling thread id if called from
    inside an lwp and NO_THREAD otherwise*/

    if (callingThread != NULL)
    {
        return callingThread->tid;
    }
    return NO_THREAD;
}

void lwp_set_scheduler(scheduler fun)
{
    scheduler oldScheduler;
    thread nxtThread;

    /*If the desired scheduler is the same as the
    current scheduler don't do anything and avoid loop*/

    if (fun == currentScheduler)
    {
        return;
    }

    /*If NULL is passed and we are already on the rr scheduler
    do nothing and return*/

    if (fun == NULL && currentScheduler == &rr_publish)
    {
        return;
    }

    /*If NULL is passed and we are not on the rr scheduler
    set the current scheduler to rr and keep track of the old
    one for thread transfer*/

    if (fun == NULL && currentScheduler != &rr_publish)
    {
        oldScheduler = currentScheduler;
        currentScheduler = &rr_publish;
    }
    else
    {
        /*Else use the new scheduler*/

        oldScheduler = currentScheduler;
        currentScheduler = fun;
    }

    /*Call scheduler initialization if any*/

    if (currentScheduler->init != NULL)
    {
        currentScheduler->init();
    }

    /*For every thread in the current schedule,
    transfer it to the new schedule*/

    for (nxtThread = oldScheduler->next();
         nxtThread != NULL; nxtThread = oldScheduler->next())
    {

        oldScheduler->remove(nxtThread);
        currentScheduler->admit(nxtThread);
    }

    /*Shut down the old scheduler if necessary*/

    if (oldScheduler->shutdown != NULL)
    {
        oldScheduler->shutdown();
    }
}

scheduler lwp_get_scheduler()
{
    return currentScheduler;
}

void terminateThread(thread victim, int *status, int shouldSD)
{
    int retStatus;

    /*Shutdown the scheduler if needed and possible*/

    if (shouldSD)
    {
        if (currentScheduler->shutdown != NULL)
        {
            currentScheduler->shutdown();
        }
    }

    /*Remove the thread from the pool*/

    retStatus = remove_thread_from_pool(victim);

    /*Check if successful*/

    if (retStatus == SYS_FAIL)
    {
        perror("Failed to remove thread from pool");
        exit(EXIT_FAILURE);
    }

    /*Populate status if non-null*/

    if (status != NULL)
    {
        *status = victim->status;
    }

    /*Deallocate the threads resources*/

    /*If the thread is not the system thread,
    unmap its memory region, free the
    thread context,*/

    if (victim->stack != NULL)
    {
        retStatus = munmap(victim->stack, victim->stacksize);

        /*Check if munmap failed*/
        if (retStatus == SYS_FAIL)
        {
            perror("Failed to unmap region");
            exit(EXIT_FAILURE);
        }
    }

    /*Otherwise, do nothing about the stack
    and free the thread context*/

    free(victim);
}