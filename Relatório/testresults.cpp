#include <stdio.h>

#define INF 112345678
#define NPROC 64
#define N 10

int procOrd[NPROC][N];
int tickets[NPROC * NPROC];
int proc[NPROC];

int main(void) {
  int n, i, j, t, menor, maior;
  char lixo[NPROC];
  for(i = 0; i < NPROC; i++) {
    scanf("%d: %d", &j, &t);
    tickets[t] = j; proc[j] = t;
  }
  for(n = 0; n < N; n++){
    getchar(); getchar();
    fgets(lixo, NPROC, stdin);
    for(i = 1; i <= 61; i++){
      scanf("%d", &t);
      procOrd[tickets[t]][n] = i;
    }}
  for(i = 0; i < NPROC; printf("\n"), i++) {
    for(printf("%d(%d): ", i, proc[i]), n = 0, maior = 0, menor = INF; n < N; n++){
      if(procOrd[i][n] > maior) maior = procOrd[i][n];
      if(procOrd[i][n] < menor) menor = procOrd[i][n];
    }
    printf("%d, %d ", menor, maior);
  }
  return 0;
}
