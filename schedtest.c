#include "types.h"
#include "user.h"
#include "param.h"

#define MAX 112345678
#define MAXT NPROC * NPROC

unsigned long randstate = -1;

unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}

int
main(void)
{
  int i, test, max = MAX;

  for(test = 0; test < 10; test++) {
    printf(1, "\nTest %d... Creating processes...\n", test + 1);
    for(i = 0; i < NPROC; i++){
      if(fork(NPROC - i) == 0){
        while(max--);
        // printf(1, "Process with %d tickets is done.\n", pid[i]);
        exit();
      }}
    while(wait() != -1);
  }
  exit();
}
