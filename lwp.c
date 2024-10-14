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
schedule_one is the next thread in the list
schedule_two is the previous thread in the list
*/
/*
The thread at the start of the list is the next thread that
will run
*/

struct scheduler rr_publish = {NULL, NULL, rrAdmit, rrRemove, rrNext, rrqlen};
scheduler currentScheduler = &rr_publish;

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

    return howBig;
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
    newThread = malloc(sizeof(thread));

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
    newThread->state.rdi = (unsigned long)fun;
    newThread->state.rsi = (unsigned long)arg;

    /*initialize the stack*/

    getBaseLoc = (uintptr_t)newThread->stack;
    getBaseLoc += howBig - ADDRESS_SIZE;

    /*"Push" the address of lwp_wrap to the top of the
    stack so that when ret happens, it pops this address
    and returns to it to execute*/

    newThread->stack[howBig] = lwp_wrap;
    newThread->stack[howBig - PREV] = getBaseLoc;

    /*Set rbp to the address of the top of the stack for
    wrap to use once it is returned to*/

    newThread->state.rbp = (unsigned long)getBaseLoc;

    /*Admit the new thread to the scheduler*/

    currentScheduler->admit(newThread);
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
