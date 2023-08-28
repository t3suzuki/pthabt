#include <stdio.h>
#include <unistd.h>

void
__debug_print(int id, int a, int tid)
{
  //sleep(1);
  switch (id) {
  case 1:
    printf("req_helper %d @ tid %d\n", a, tid);
    break;
  case 2:
    printf("helper done %d @ ret %d\n", a, tid);
    break;
  case 3:
    //printf("Current tid %d\n", tid);
    break;
  case 5:
    printf("Pre-syscall call_id = %d   (%d)\n", a, tid);
    break;
  case 6:
    //printf("epoll arg = %d tid %d\n", a, tid);
    break;
  }
}
