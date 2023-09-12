#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
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
static volatile cqe_t *acq;
static volatile sqe_t *asq;
static volatile uint32_t *regs32;
static volatile uint64_t *regs64;
static volatile cqe_t *cq;
static volatile sqe_t *sq;
static uint32_t sqp = 0;
static uint32_t cqp = 0;
static volatile void *buf;

void
sync_cmd(int *sqp, int *cqp)
{
  *sqp = (*sqp + 1) % ASQD;
  regs32[0x1000 / sizeof(uint32_t)] = *sqp;
  
  while (1) {
    if (acq[*cqp].SF.P)
      break;
    sleep(1);
  }
  *cqp = (*cqp + 1) % ACQD;
  printf("cmd done\n");
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
  regs32 = mmap(0, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  regs64 = (volatile uint64_t *)regs32;
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

  acq = (volatile cqe_t *)(aq + 0x0);
  asq = (volatile sqe_t *)(aq + 0x1000);

  regs64[0x30 / sizeof(uint64_t)] = v2p((size_t)acq); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = v2p((size_t)asq); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = (ACQD << 16) | ASQD; // Admin Queue Entry Num

  
  printf("CQ phyaddr %p %lx\n", acq, regs64[0x30 / sizeof(uint64_t)]);
  printf("SQ phyaddr %p %lx\n", asq, regs64[0x28 / sizeof(uint64_t)]);

  // enable controller.
  cc = 1;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc enable
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  assert(csts == 1);

  volatile void *queue = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  cq = (volatile cqe_t *)(queue + 0x0);
  sq = (volatile sqe_t *)(queue + 0x1000);
  bzero((void*)queue, sz);

  buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
	     MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);

  // identity
  {
    printf("identity cmd...\n");
    volatile sqe_t *sqe = &asq[asqp];
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x6; // identity
    sqe->PRP1 = (uint64_t) v2p((size_t)buf);
    sqe->CDW10 = 0;
    sync_cmd(&asqp, &acqp);
  }

  // CQ create
  {
    volatile sqe_t *sqe = &asq[asqp];
    int qid = 0;
    int size = 8;
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x5; // create SQ
    sqe->PRP1 = (uint64_t) v2p((size_t)cq);
    sqe->CDW10 = ((size-1) << 16) | qid;
    sqe->CDW11 = (qid << 16) | 1;
    sync_cmd(&asqp, &acqp);
    printf("CQ create done\n");
  }

  // SQ create
  {
    volatile sqe_t *sqe = &asq[asqp];
    int qid = 0;
    int size = 8;
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x1; // create SQ
    sqe->PRP1 = (uint64_t) v2p((size_t)sq);
    sqe->CDW10 = ((size-1) << 16) | qid;
    sqe->CDW11 = 1; // physically contiguous
    sync_cmd(&asqp, &acqp);
    printf("SQ create done\n");
  }

  // get namespaces
  {
    volatile sqe_t *sqe = &asq[asqp];
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x6; // identity
    bzero((void*)buf, 4096);
    sqe->PRP1 = (uint64_t) v2p((size_t)buf);
    sqe->NSID = 0;
    sqe->CDW10 = 2;
    sync_cmd(&asqp, &acqp);

    uint32_t *id_list = (uint32_t *)buf;
    int i;
    for (i=0; i<1024; i++) {
      if (id_list[i] == 0) break;
      printf("namespace %d\n", id_list[i]);
    }
  }
  
}

void
nvme_read(uint64_t lba, int num_blk)
{
  volatile sqe_t *sqe = &sq[sqp];
  bzero((void*)sqe, sizeof(sqe_t));
  bzero((void*)buf, 4096);
  sqe->CDW0.OPC = 0x2; // read
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(&sqp, &cqp);
}

void
nvme_write(uint64_t lba, int num_blk)
{
  volatile sqe_t *sqe = &sq[sqp];
  bzero((void*)sqe, sizeof(sqe_t));
  memset((void *)buf, 0x5a, 4096);
  sqe->CDW0.OPC = 0x1; // write
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(&sqp, &cqp);
}

int
main()
{
  init();
  nvme_write(0, 1);
  nvme_read(0, 1);
}
