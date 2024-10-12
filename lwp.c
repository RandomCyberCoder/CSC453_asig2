#include "lwp.h"
#include "fp.h"
#include "roundRobin.h"
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

unsigned long threadIdCounter = 1;


size_t create_stackSizeHelper(){
    /*stacks should be a multiple of the page size*/
    size_t pageSize = _SC_PAGESIZE;
    /*get the soft & hard resource limits*/
    struct rlimit rlimStruct;
    int retVal = getrlimit(RLIMIT_STACK, &rlimStruct);
    /*
    check if the RLIMIT_STACK exists or if the 
    soft constraint is set to RLIM_INFINITY; if the 
    value of retVal is negative one that means the 
    call to getrlimit() has failed
    */
    size_t howBig = 0;
    if(retVal == -1 || rlimStruct.rlim_cur == RLIM_INFINITY){
        /*
        if no soft limit given, use a stack
        size of 8 MB*
        */
        size_t MB_8 = 8388608;
        /*
        make sure stack size is a multiple of 
        memory page size
        */
        if((MB_8 % pageSize) != 0){
            size_t multBy = MB_8 / pageSize;
            howBig = pageSize * (multBy);
        }
        else{
            howBig = MB_8;
        }
    }
    else{
        /*
        make sure soft resource limit is a multiple
        of the page size
        */
        if((rlimStruct.rlim_cur % pageSize) != 0){
            size_t multBy = rlimStruct.rlim_cur / pageSize;
            howBig = pageSize * multBy;
        }
        else{
            howBig = rlimStruct.rlim_cur;
        }
    }

    return howBig;
}

tid_t lwp_create(lwpfun fun,void *arg){
    /*create the threads context and stack*/
    /*calculate howBig the stack size will be*/
    size_t howBig = create_stackSizeHelper();
    thread newThread = malloc(sizeof(thread));
    void *stackBase = mmap(NULL, howBig, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    /*check if mmap failed*/
    if(stackBase == ((void *)-1)){
        return NO_THREAD;
    }

    /*initialize the context*/
    newThread->tid = threadIdCounter;
    threadIdCounter += 1;
    /*initialize the stack*/

}