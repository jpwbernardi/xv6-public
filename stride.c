#include "stride.h"
#include "types.h"
#include "user.h"

//This function was already implemented in usertests, but we couldn't use
unsigned long randstate = 1;

unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}

void schedtest(){
  // int i;
  // for(i = 0; i < NPROC - 1; i++) fork(DEFT);
}
