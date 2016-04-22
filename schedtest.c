#include "types.h"
#include "user.h"
#include "param.h"

#define MAX 112345678
#define MAXT NPROC * NPROC

unsigned long randstate = 27;

unsigned int
rand()
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}

int
main(void)
{
  int pid, i, ticket, max = MAX;
  printf(1, "Creating processes:\n");
  for(i = 0; i < NPROC; i++){
    pid = fork(ticket = (rand() % MAXT + 1));
    if(pid == 0){
      while(max--);
      printf(1, "Process with %d tickets is done.\n", ticket);
      exit();
    }}
  while(wait() != -1);
  exit();
}
