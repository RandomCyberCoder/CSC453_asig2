#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include "lwp.h"

static void testfunc(uintptr_t num) {
  /* print the number num num times, indented by 5*num spaces
   * Not terribly interesting, but it is instructive.
   */
  printf("Greetings from Thread %d.  Goodbye\n",(int)num);
  exit(0);
}


int main(int argc, char *argv[]){
  lwp_create((lwpfun)testfunc,(void*)1234);
  printf("Well, we didn't crash\n");
  return 0;
}

