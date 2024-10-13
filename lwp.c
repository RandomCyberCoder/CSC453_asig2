#include "lwp.h"
#include "fp.h"
#include "roundRobin.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <bits/mman.h>

#define STACK_SIZE 8388608
#define MAP_ANONYMOUS 0x20

/*
schedule_one is the next thread in the list
schedule_two is the previous thread in the list
*/
/*
The thread at the start of the list is the next thread that
will run
*/

struct scheduler rr_publish = {NULL, NULL, rrAdmit, rrRemove, rrNext, rrqlen};
scheduler RoundRobin = &rr_publish;

unsigned long threadIdCounter = 1;

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
    howBig = 0;
    if (retVal == -1 || rlimStruct.rlim_cur == RLIM_INFINITY)
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
        if ((MB_8 % pageSize) != 0)
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
        if ((rlimStruct.rlim_cur % pageSize) != 0)
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

tid_t lwp_create(lwpfun fun, void *arg)
{
    size_t howBig;
    thread newThread;
    void *stackBase;

    /*create the threads context and stack*/
    /*calculate howBig the stack size will be*/
    howBig = create_stackSizeHelper();
    newThread = malloc(sizeof(thread));

    /*Check if malloc failed*/
    if (newThread == NULL)
    {
        return NO_THREAD;
    }

    stackBase = mmap(NULL, howBig, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
    newThread->tid = threadIdCounter;
    threadIdCounter += 1;
    newThread->stack = stackBase;
    newThread->stacksize = howBig;
    newThread->state.rdi = (unsigned long)fun;
    newThread->state.rsi = (unsigned long)arg;

    /*initialize the stack*/
    newThread->stack[0] = lwp_wrap;
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
