#ifndef __MYFIFO_H__
#define __MYFIFO_H__

#include <stdlib.h>

typedef struct {
  int wp;
  int rp;
  int n;
  int qd;
  void **queue;
} myfifo_t;


inline void
myfifo_init(myfifo_t *myfifo, int qd)
{
  myfifo->wp = 0;
  myfifo->rp = 0;
  myfifo->n = 0;
  myfifo->qd = qd;
  myfifo->queue = (void *)malloc(qd * sizeof(void *));
}

inline int
myfifo_push(myfifo_t *myfifo, void *entry)
{
  int ret = 0;
  do {
    if (myfifo->n == myfifo->qd) {
      return 0;
    } else {
      int old_wp = myfifo->wp;
      int new_wp = (old_wp + 1) % myfifo->qd;
      ret = __sync_bool_compare_and_swap(&myfifo->wp, old_wp, new_wp);
      if (ret) {
	myfifo->queue[old_wp] = entry;
	__sync_fetch_and_add(&myfifo->n, 1);
      }
    }
  } while (ret == 0);
  return 1;
}

inline void *
myfifo_pop(myfifo_t *myfifo)
{
  while (1) {
    if (myfifo->n == 0) {
      return (void *)NULL;
    } else {
      int old_rp = myfifo->rp;
      int new_rp = (old_rp + 1) % myfifo->qd;
      int ret = __sync_bool_compare_and_swap(&myfifo->rp, old_rp, new_rp);
      if (ret) {
	__sync_fetch_and_add(&myfifo->n, -1);
	return myfifo->queue[old_rp];
      }
    }
  }
}


#endif 
