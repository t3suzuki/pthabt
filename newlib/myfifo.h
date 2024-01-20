#ifndef __MYFIFO_H__
#define __MYFIFO_H__

#include <stdlib.h>

static int wp;
static int rp;
static int n;

static myfifo_entry_t queue[MYFIFO_QD];

void
myfifo_init()
{
  wp = 0;
  rp = 0;
  n = 0;
}

int
myfifo_push(myfifo_entry_t entry)
{
  int ret = 0;
  do {
    if (n == MYFIFO_QD) {
      return 0;
    } else {
      int old_wp = wp;
      int new_wp = (old_wp + 1) % MYFIFO_QD;
      ret = __sync_bool_compare_and_swap(&wp, old_wp, new_wp);
      if (ret) {
	queue[old_wp] = entry;
	__sync_fetch_and_add(&n, 1);
      }
    }
  } while (ret == 0);
  return 1;
}

myfifo_entry_t
myfifo_pop()
{
  while (1) {
    if (n == 0) {
      return (myfifo_entry_t)NULL;
    } else {
      int old_rp = rp;
      int new_rp = (old_rp + 1) % MYFIFO_QD;
      int ret = __sync_bool_compare_and_swap(&rp, old_rp, new_rp);
      if (ret) {
	__sync_fetch_and_add(&n, -1);
	return queue[old_rp];
      }
    }
  }
}


#endif 
