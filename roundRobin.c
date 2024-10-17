#include "lwp.h"
#include "fp.h"
#include "roundRobin.h"
#include <stdlib.h>
#include <stdio.h>
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

void rrAdmit(thread new)
{
    if (head == NULL)
    {
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
    else
    {
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

void rrRemove(thread victim)
{
    thread curThread;
    thread prvThread;
    thread nxtThread;


    if (head == NULL)
    {
        return;
    }
    /*if the thread is the first victim*/
    if (victim == head)
    {
        head = head->sched_one;
        qLen -= 1;
        if (qLen == 0)
        {
            head = NULL;
            tail = NULL;
        }
        return;
    }

    /*find the victim thread*/
    curThread = head;
    while (curThread != victim)
    {
        curThread = curThread->sched_one;

        /*If you don't find the victim and reach
        the end of the list just return*/

        if (curThread == NULL)
        {
            return;
        }
    }
    /*once the victim thread has been found, remove it from the scheduler.
    and have the thread before it an after it point to one another.
    */
    prvThread = curThread->sched_two;
    nxtThread = curThread->sched_one;
    prvThread->sched_one = nxtThread;
    if (nxtThread != NULL)
    {
        /*if the thread removed was not the last one in the list*/
        nxtThread->sched_two = prvThread;
    }
    else
    {
        /*if the thread removed was the last one in the list, update
        the tail of the list*/
        tail = prvThread;
    }

    /*lower the thread count*/
    qLen -= 1;
}

thread rrNext()
{
    /*NOTE: the first thread in the list is the next thread that will run.*/

    thread oldTailThread, nextThread, newHeadThread, curr;

    

    if (qLen == 0)
    {
        /*if there are no threads return NULL*/
        return NULL;
    }
    else if (qLen == 1)
    {
        curr = head;
        /*if there is only one thread available*/
        return head;
    }
    else
    {
        curr = head;
        curr = curr->sched_one;
        while (curr != NULL)
        {
            curr = curr->sched_one;
        }

        /*thread we will return*/
        nextThread = head;

        oldTailThread = tail;
        newHeadThread = head->sched_one;

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

int rrqlen()
{
    return qLen;
}