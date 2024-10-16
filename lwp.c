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
#define EMPTY 0

/*
lib_one is a next pointer for the next thread in a list
lib_two is a prev pointer for the prev thread in a list
*/

/*Create RoundRobin scheduler and set it to currentScheduler by default*/

struct scheduler rr_publish = {NULL, NULL, rrAdmit, rrRemove, rrNext, rrqlen};
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

    currThread = threadPool;

    /*If the only thread in the pool is the victim, set pool to NULL*/

    if (threadPool == victim && threadPool->lib_one == NULL)
    {
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
    newThread->stack[howBig - (PREV * 2)] = (unsigned long)getBaseLoc;

    /*Set rbp to the address of the rbp reg on our stack
    so that when it is popped inside leave and ret, it pops
    our rbp filler value and then the address of lwp_wrap for ret*/

    newThread->state.rbp = (unsigned long)(getBaseLoc);

    /*Add thread to thread pool*/

    add_thread_to_pool(newThread);

    /*Admit the new thread to the scheduler*/

    currentScheduler->admit(newThread);

    return newThread->tid;
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

    /*Initialize context. Stack attributes are NULL
    to indicate this is the original system thread
    Register values of context don't matter because they will
    be saved to the correct values when yield is called*/

    systemThread->tid = threadIdCounter++;
    systemThread->stack = NULL;
    systemThread->stacksize = EMPTY;
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
    unsigned int status, retStatus;
    /*Yield control to the next LWP in the schedule*/

    /*Get the next thread from the scheduler*/

    nextThread = currentScheduler->next();

    /*If there is no next thread, terminate the program*/

    if (nextThread == NULL)
    {
        /*Save exit status of calling thread*/

        status = callingThread->status;

        /*Shutdown the scheduler if necessary*/

        if (currentScheduler->shutdown != NULL) 
        {
            currentScheduler->shutdown();
        }

        /*Remove calling thread from pool*/

        retStatus = remove_thread_from_pool(callingThread);

        /*Check if successful*/

        if (retStatus == SYS_FAIL)
        {
            perror("Failed to remove thread from pool");
        }

        /*Deallocate the threads resources*/

        /*If the thread is the system thread, 
        do nothing about a stack and 
        free the thread context*/

        if (callingThread->stack == NULL)
        {
            free(callingThread);

            /*Exit with syscall a*/

            exit(status);
        }

        /*Otherwise, unmap its memory region, free the 
        thread context, and exit*/

        retStatus = munmap(callingThread->stack, callingThread->stacksize);

        /*Check if munmap failed*/
        if (retStatus == SYS_FAIL)
        {
            perror("Failed to unmap region");
        }
        
        free(callingThread);

        /*Exit with syscall a*/

        exit(status);
    }

    /*Otherwise, set callingThread and swap contexts*/

    callingThread = nextThread;
    swap_rfiles(&(callingThread->state), &(nextThread->state));
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
    thread currThread;
    tid_t tid;
    int retStatus;

    /*Search for terminated threads in the terminated list*/

    currThread = terminated;

    /*If there are no terminated threads, add this 
    thread to the waiting thread list*/

    if (currThread == NULL)
    {
        /*Deschedule the calling thread first*/

        currentScheduler->remove(callingThread);

        /*Add to waiting thread list*/

        currThread = waiting;

        if (currThread == NULL)
        {
            /*If waiting list is empty, set head to caller*/

            waiting = callingThread;
        } else {
            /*Search for end of waiting thread list*/

            while (currThread->exited != NULL)
            {
                currThread = currThread->exited;
            }

            /*And add the calling thread to the end*/

            currThread->exited = callingThread;
        }

        /*Set next pointer of end of list to NULL*/

        callingThread->exited = NULL;

        /*If there are no more runnable threads return NO_THREAD*/

        if (currentScheduler->qlen() <= 1)
        {
            /*Remove the thread from the pool*/

            retStatus = remove_thread_from_pool(currThread);

            /*Check if successful*/

            if (retStatus == SYS_FAIL)
            {
                perror("Failed to remove thread from pool");
            }

            /*Populate status if non-null*/

            if (status != NULL)
            {
                *status = currThread->status;
            }

            /*Deallocate the threads resources*/

            /*If the thread is the system thread, 
            do nothing about a stack and return it's id
            after freeing the thread context*/

            if (currThread->stack == NULL)
            {
                free(currThread);
                return NO_THREAD;
            }

            /*Otherwise, unmap its memory region, free the 
            thread context, and return the id*/

            retStatus = munmap(currThread->stack, currThread->stacksize);

            /*Check if munmap failed*/
            if (retStatus == SYS_FAIL)
            {
                perror("Failed to unmap region");
            }
            
            free(currThread);
            /*Shutdown the scheduler if needed*/

            if(currentScheduler->shutdown != NULL)
            {
                currentScheduler->shutdown();
            }

            return NO_THREAD;
        }

        /*Otherwise "block" by yielding*/

        lwp_yield();

        /*Get the oldest terminated thread now that we're back*/

        currThread = terminated;
    }

    /*Get the id of the thread*/

    tid = currThread->tid;

    /*Remove the thread from the pool*/

    retStatus = remove_thread_from_pool(currThread);

    /*Check if successful*/

    if (retStatus == SYS_FAIL)
    {
        perror("Failed to remove thread from pool");
    }

    /*Update the terminated list*/

    terminated = terminated->exited;

    /*Populate status if non-null*/

    if (status != NULL)
    {
        *status = currThread->status;
    }

    /*Deallocate the threads resources*/

    /*If the thread is the system thread, 
    do nothing about a stack and return it's id
    after freeing the thread context*/

    if (currThread->stack == NULL)
    {
        free(currThread);
        return tid;
    }

    /*Otherwise, unmap its memory region, free the 
    thread context, and return the id*/

    retStatus = munmap(currThread->stack, currThread->stacksize);

    /*Check if munmap failed*/
    if (retStatus == SYS_FAIL)
    {
        perror("Failed to unmap region");
    }
    
    free(currThread);
    return tid;
}

thread tid2thread(tid_t tid)
{
    thread checkThread = threadPool;

    while(checkThread != NULL)
    {
        if(tid == checkThread->tid)
        {
            return checkThread;
        }
        else
        {
            checkThread = checkThread->lib_one;
        }
    }

    /*If the thread wasn't found return NULL*/

    return NULL;
}

tid_t lwp_gettid(void)
{
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

    if(fun == NULL && currentScheduler == &rr_publish)
    {
        return;
    }
    else if(fun == NULL && currentScheduler != &rr_publish){
        oldScheduler = currentScheduler;
        currentScheduler = &rr_publish;
    }
    else{
        oldScheduler = currentScheduler;
        currentScheduler = fun; 
    }


    if(currentScheduler->init != NULL)
    {
        currentScheduler->init();
    }   


    for(nxtThread = oldScheduler->next();
        nxtThread != NULL; nxtThread = oldScheduler->next())
    {
        
        oldScheduler->remove(nxtThread);
        currentScheduler->admit(nxtThread);
    }

    if(oldScheduler->shutdown != NULL)
    {
        oldScheduler->shutdown();
    }
    
}

scheduler lwp_get_scheduler()
{
    return currentScheduler;
}