#include <stdio.h>
#include <unistd.h>

extern "C" {
void
__debug_print(int id, int a, int tid)
{
  //sleep(1);
  switch (id) {
  case 1:
    printf("req_helper %d @ tid %d\n", a, tid);
    break;
  case 2:
    printf("helper done %d @ tid %d\n", a, tid);
    break;
  }
}

}
