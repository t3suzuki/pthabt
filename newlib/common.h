#ifndef __COMMON_H__
#define __COMMON_H__

//#define N_TH (1)
#define N_ULT (512)
#define ULT_N_TH (N_ULT*N_TH)

#define BLKSZ (4096)

#define MAX(x,y) ((x > y) ? x : y)
#define MIN(x,y) ((x < y) ? x : y)

extern void (*debug_print)(int, int, int);

#include <abt.h>

static inline int get_tid()
{
  int rank;
  ABT_xstream_self_rank(&rank);
  return rank;
}

#define INACTIVE_BLOCK (-1)
#define JUST_ALLOCATED (-2)

void disable_preempt_ult();
void enable_preempt_ult();


#endif 
