#include "param.h"

#define MAXT NPROC * NPROC //maximum nº of tickets allowed per-process
#define MINT 1 //minimum nº of tickets allowed per-process
#define DEFT MAXT / 2 //system's process tickets
#define CONS 10000
#define INF 18446744073709551615ull

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define left(p) ((p) << 1)
#define right(p) (left(p) + 1)

unsigned int rand(void);
typedef unsigned long long ull;
