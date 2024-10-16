#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "lwp.h"

#if defined (__x86_64)         /* X86 only code */
  #define WORDSIZE 8
  #define GetBP(bp)  asm("movq  %%rbp,%0": "=r" (bp) : )
  #define SetBP(bp)  asm("movq  %0,%%rbp":           : "r" (bp)  )
#elif defined(__i386)
  #define WORDSIZE 4
  #define GetBP(bp)  asm("movl  %%ebp,%0": "=r" (bp) : )
  #define SetBP(bp)  asm("movl  %0,%%ebp":           : "r" (bp)  )
#else /* END x86 only code */
  #error "This stack manipulation code can only be compiled on an x86"
#endif

#define ALIGNFACTOR 16

void aligncheck(void *whatever) {
  unsigned long bp;
  GetBP(bp);
  bp += 2*WORDSIZE;

  if ( bp%ALIGNFACTOR ) {
    printf("*** Stack frame does not appear to be properly aligned.\n");
    printf("    The beginning (lowest address) of the arguments region\n");
    printf("    of the frame must be evenly divisible by %d\n",ALIGNFACTOR);
    printf("Lowest arg addr: 0x%0lx%%%ld = %ld\n",bp,(long)ALIGNFACTOR,
           bp%ALIGNFACTOR);
  } else {
    printf ("Stack frame alignment appears correct.\n");
  }
  exit(0);
}

int main(int argc, char *argv[]){

  lwp_create((lwpfun)aligncheck,(void*)0);

  lwp_start();
  return 0;
}

