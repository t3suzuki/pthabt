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

static size_t v2p(size_t vaddr) {
  FILE *pagemap;
  size_t paddr = 0;
  ssize_t offset = (vaddr / sysconf(_SC_PAGESIZE)) * sizeof(uint64_t);
  uint64_t e;

  if ((pagemap = fopen("/proc/self/pagemap", "r"))) {
    if (lseek(fileno(pagemap), offset, SEEK_SET) == offset) {
      if (fread(&e, sizeof(uint64_t), 1, pagemap)) {
	if (e & (1ULL << 63)) {
	  paddr = e & ((1ULL << 55) - 1);
	  paddr = paddr * sysconf(_SC_PAGESIZE);
	  paddr = paddr | (vaddr & (sysconf(_SC_PAGESIZE) - 1));
	}
      }
    }
    fclose(pagemap);
  }
  return paddr;
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
  volatile uint32_t *regs32 = mmap(0, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  volatile uint64_t *regs64 = (volatile uint64_t *)regs32;
  if (regs32 == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  uint32_t csts;
  uint32_t cc;
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%lx\n", csts);
  cc = 0;
  regs32[0x14 / sizeof(uint32_t)] = cc;
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%lx\n", csts);
  
  assert(csts == 0);

  const int sz = 2*1024*1024;
  void *admin_queue = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  bzero(admin_queue, sz);

  regs64[0x30 / sizeof(uint64_t)] = v2p(admin_queue); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = v2p(admin_queue + 0x1000); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = (8 << 16) | 8; // Admin Queue Entry Num

  cc = 1;
  regs32[0x14 / sizeof(uint32_t)] = cc;
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%lx\n", csts);
  assert(csts == 1);

  
}

int
main()
{
  init();
}
