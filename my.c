#include <stdio.h>
#include <unistd.h>

extern "C" {
void
__debug_print(int a)
{
  //sleep(1);
  printf("debug %d\n", a);
}

}
