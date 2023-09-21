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
__debug_print4(long id, long a2, long a3, long a4, long a5)
{
  switch (id) {
  case 7:
    printf("pwrite64 fd=%d, wbuf=%lx, write_sz=%ld, disk_pos=%ld\n", a2, a3, a4, a5);
    if (a4 != 1048576) {
      unsigned char *wbuf = (unsigned char *)a2;
      int i;
      for (i=0; i<512; i++) {
	printf("%02x ", wbuf[i]);
	if (i % 16 == 15)
	  printf("\n");
      }
    }
    break;
  }
}

void
__debug_print(long id, long a, long tid)
{
  if (id > 900) {
    printf("%ld %ld\n", id, a);
    return;
  }
  //sleep(1);
  switch (id) {
  case 1:
    //printf("req_helper %ld @ tid %ld\n", a, tid);
    break;
  case 2:
    printf("helper done %ld @ ret %ld\n", a, tid);
    break;
  case 3:
    //printf("Current tid %ld\n", tid);
    break;
  case 5:
    printf("Pre-syscall call_id = %ld   (%ld)\n", a, tid);
    break;
  case 6:
    printf("Post-syscall call_id = %ld   (%ld)\n", a, tid);
    break;
  case 874:
    printf("read64 = %ld %ld\n", a, tid);
    break;
  case 876:
    printf("read wait = %ld %ld\n", a, tid);
    break;
  case 886:
    printf("%c %ld\n", ((char *)a)[0], tid);
    break;
  case 885:
    printf("%ld %ld\n", a, tid);
    break;
  case 883:
    printf("read fd = %ld (pos = %d)\n", a, tid);
    break;
  case 882:
    //printf("read_done %ld\n", a);
    break;
  case 884:
    printf("hookfd = %ld %ld\n", a, tid);
    break;
  case 888:
    printf("time1 %ld   (%f)\n", a, (double)tid * 1e-9);
    break;
  case 870:
    printf("select %ld  %ld\n", a, tid);
    break;
  case 871:
    //printf("sleep %ld  %ld\n", a, tid);
    break;
  case 889:
    printf("time2 %ld   (%f)\n", a, (double)tid * 1e-9);
    break;
  case 520:
    printf("debug nvme req lba %d (%d) cid %d\n", a, (unsigned char)a & 0xff, tid);
    break;
  case 521:
    printf("debug nvme cmp %d %d\n", a, tid);
    break;
  case 522:
    printf("debug nvme cmp2 %d %d\n", a, tid);
    break;
  case 59:
    printf("exec %s\n", (char *)a);
    break;
  }
}
