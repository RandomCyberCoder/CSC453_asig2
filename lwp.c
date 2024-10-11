#include "lwp.h"

thread head = NULL
thread tail = NULL
unsigned long qLen = 0;

void rrInit(){
    //do we want to do anything here
}

void rrShtudown(){
    //do we want to do anything here?????
}

void rrAdmit(thread new){
    if(head == NULL){
        head = new;
        tail = new;
        new->sched_one = NULL;
        new->sched_two = new;
        new
        qLen = 1;
    }
    else{
        tail->sched_one = new;
        new->sched_one = NULL
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