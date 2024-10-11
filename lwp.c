#include "lwp.h"
#include "fp.h"
#include <stdlib.h>

/*
schedule_one is the next thread in the list
schedule_two is the previous thread in the list
*/

thread head = NULL;
thread tail = NULL;
unsigned long qLen = 0;

void rrInit(){
    //do we want to do anything here
}

void rrShtudown(){
    //do we want to do anything here?????
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
    //find the thread we want to remove
    if(victim == head){
        head = head->sched_one;
        qLen -= 1;
        return;
    }

    thread curThread = head;
    while(curThread != victim){

    }

    qLen -= 1;

}

thread rrNext(){
    //return the thread at the start of the list and append it to the end;
    //check for case of only 1 thread?
}

int rrqlen(){
    return qLen;
}