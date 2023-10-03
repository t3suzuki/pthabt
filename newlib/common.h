#ifndef __COMMON_H__
#define __COMMON_H__

#define N_TH (8)
#define N_ULT (512)
#define ULT_N_TH (N_ULT*N_TH)

#define BLKSZ (2048)

extern void (*debug_print)(int, int, int);

#endif 
