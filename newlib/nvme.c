#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>

static int enable_bus_master(int uio_index)
{
  char path[256];
  sprintf(path, "/sys/class/uio/uio%d/device/config", uio_index);
  int fd = open(path, O_RDWR);
  uint32_t val;
  pread(fd, &val, 4, 0x4);
  val = 0x06;
  pwrite(fd, &val, 4, 0x4);
  close(fd);
  return 0;
}

int
init()
{
  int uio_index = 0;
  
  enable_bus_master(uio_index);
  
  char path[256];
  sprintf(path, "/sys/class/uio/uio%d/device/resource0", uio_index);
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    perror("open");
    return -1;
  }
  volatile uint32_t *regs32 = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (regs32 == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  uint32_t csts;
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%lx\n", csts);
  uint32_t cc = 0;
  regs32[0x14 / sizeof(uint32_t)] = cc;
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%lx\n", csts);
  
  assert(csts == 0);
  
}

int
main()
{
  init();
}
