#include "param.h"

#define MAXT NPROC //maximum nº of tickets allowed per-process
#define MINT 1 //minimum nº of tickets allowed per-process
#define SYST MAXT / 2 //system's process tickets

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

unsigned int rand(void);
