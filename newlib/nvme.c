#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
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


typedef struct {
  struct {
    unsigned OPC : 8;
    unsigned FUSE : 2;
    unsigned Reserved0 : 4;
    unsigned PSDT : 2;
    unsigned CID : 16;
  } CDW0;
  uint32_t NSID;
  uint64_t Reserved0;
  uint64_t MPTR;
  uint64_t PRP1;
  uint64_t PRP2;
  uint32_t CDW10;
  uint32_t CDW11;
  uint32_t CDW12;
  uint32_t CDW13;
  uint32_t CDW14;
  uint32_t CDW15;
} sqe_t;

typedef struct {
  uint32_t DW0;
  uint32_t DW1;
  uint16_t SQHD;
  uint16_t SQID;
  struct {
    unsigned CID : 16;
    unsigned P : 1;
    unsigned SC : 8;
    unsigned SCT : 3;
    unsigned Reserved0 : 2;
    unsigned M : 1;
    unsigned DNR : 1;
  } SF;
} cqe_t;

#define ASQD (8)
#define ACQD (8)

static uint32_t asqp = 0;
static uint32_t acqp = 0;

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
  printf("%u\n", csts);
  cc = 0;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc disable
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  
  assert(csts == 0);

  const int sz = 2*1024*1024;
  volatile void *aq = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  bzero((void*)aq, sz);

  volatile cqe_t *acq = (volatile cqe_t *)(aq + 0x0);
  volatile sqe_t *asq = (volatile sqe_t *)(aq + 0x1000);

  regs64[0x30 / sizeof(uint64_t)] = v2p((size_t)acq); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = v2p((size_t)asq); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = (ACQD << 16) | ASQD; // Admin Queue Entry Num

  cc = 1;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc enable
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  assert(csts == 1);

  volatile void *queue = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  bzero((void*)queue, sz);

  volatile void *buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  
  volatile sqe_t *sqe = &asq[asqp];
  int qid = 1;
  int size = 8;
  bzero((void*)sqe, sizeof(sqe_t));
  sqe->CDW0.OPC = 0x1; // create SQ
  sqe->CDW0.CID = asqp;
  sqe->PRP1 = (uint64_t) buf;
  sqe->CDW10 = (size << 16) | qid;
  sqe->CDW11 = 1; // physically contiguous
  
  asqp = (asqp + 1) % ASQD;
  regs32[0] = asqp;

  while (1) {
    if (acq[acqp].SF.P)
      break;
  }
}

int
main()
{
  init();
}
