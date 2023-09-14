#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>


int
__debug_printf(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  int result = printf(format, ap);
  va_end(ap);
  return result;
}


void
__debug_print(int id, int a, int tid)
{
  if (id > 900) {
    printf("%d %d\n", id, a);
    return;
  }
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
    printf("Post-syscall call_id = %d   (%d)\n", a, tid);
    break;
  case 888:
    printf("time1 %d   (%f)\n", a, (double)tid * 1e-9);
    break;
  case 889:
    printf("time2 %d   (%f)\n", a, (double)tid * 1e-9);
    break;
  case 59:
    printf("exec %s\n", (char *)a);
    break;
  }
}
