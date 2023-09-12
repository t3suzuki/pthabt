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

#define NQ (2)

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

typedef struct {
  uint16_t VID;
  uint16_t SSVID;
  char SN[20];
  char MN[40];
  // +64
  char FR[8];
  uint8_t RAB;
  uint8_t IEEE[3];
  uint8_t CMIC;
  uint8_t MDTS;
  uint16_t CNTLID;
  uint32_t VER;
  uint32_t RTD3R;
  uint32_t RTD3E;
  uint32_t OAES;
  uint32_t CTRATT;
  uint8_t Reserved0[12];
  uint8_t FGUID[16];
  // +128
  uint8_t Reserved1[112];
  uint8_t NVMEMI[16];  // Refer to the NVMe Management Interface Spec.
  // +256
  uint16_t OACS;
  uint8_t ACL;
  uint8_t AERL;
  uint8_t FRMW;
  uint8_t LPA;
  uint8_t ELPE;
  uint8_t NPSS;
  uint8_t AVSCC;
  uint8_t APSTA;
  uint16_t WCTEMP;
  uint16_t CCTEMP;
  uint16_t MTFA;
  uint32_t HMPRE;
  uint32_t HMMIN;
  uint8_t TNVMCAP[16];
  uint8_t UNVMCAP[16];
  uint32_t RPMBS;
  uint16_t EDSTT;
  uint8_t DSTO;
  uint8_t FWUG;
  uint16_t KAS;
  uint16_t HCTMA;
  uint16_t MNTMT;
  uint16_t MXTMT;
  uint32_t SANICAP;
  uint8_t Reserved2[180];
  // +512
  uint8_t SQES;
  uint8_t CQES;
  uint16_t MAXCMD;
  uint32_t NN;
  uint16_t ONCS;
  uint16_t FUSES;
  uint8_t FNA;
  uint8_t VWC;
  uint16_t AWUN;
  uint16_t AWUPF;
  uint8_t NVSCC;
  uint8_t Reserved3;
  uint16_t ACWU;
  uint16_t Reserved4;
  uint32_t SGLS;
  uint8_t Reserved5[228];
  char SUBNQN[256];
  // +1024
  uint8_t Reserved6[768];
  uint8_t NVMOF[256];  // Refer to the NVMe over Fabrics spec.
  // +2048
  uint8_t PSD[32][32];
  // +3072
  uint8_t VENDSPEC[1024];
} IdentifyControllerData;

static volatile uint32_t *regs32;
static volatile uint64_t *regs64;
static volatile void *buf;

#define ASQD (8)
#define ACQD (8)
#define SQD (1024)
#define CQD (1024)
static uint32_t sqps[NQ];
static uint32_t cqps[NQ];
static volatile cqe_t *cqs[NQ];
static volatile sqe_t *sqs[NQ];
static int cqds[NQ];
static int sqds[NQ];


void
sync_cmd(int qid)
{
  sqps[qid] = (sqps[qid] + 1) % sqds[qid];
  asm volatile ("" : : : "memory");
  regs32[0x1000 / sizeof(uint32_t) + 2 * qid] = sqps[qid];

  printf("flag %d\n", cqs[qid][cqps[qid]].SF.P);
  while (1) {
    if (cqs[qid][cqps[qid]].SF.P)
      break;
    sleep(1);
  }
  printf("cmd done %d sc=%x flag %d\n", cqs[qid][cqps[qid]].SF.SCT, cqs[qid][cqps[qid]].SF.SC, cqs[qid][cqps[qid]].SF.P);
  cqps[qid] = (cqps[qid] + 1) % cqds[qid];
}

int
init()
{
  int uio_index = 16;
  
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
  printf("csts = %u\n", csts);

  /*
  cc = 0x2 << 14;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc shutdown
  sleep(4);
  csts = regs32[0x1c / sizeof(uint32_t)];
  printf("%u\n", csts);
  */
  
  cc = 0;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc disable
  sleep(1);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  
  assert(csts == 0);

  {
    int iq;
    for (iq=0; iq<NQ; iq++) {
      const int sz = 2*1024*1024;

      volatile void *q = mmap(NULL, sz, PROT_READ | PROT_WRITE,
			      MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
      
      assert(q != MAP_FAILED);
    
      bzero((void*)q, sz);
      
      cqs[iq] = (volatile cqe_t *)(q + 0x0);
      sqs[iq] = (volatile sqe_t *)(q + 0x4000);
      cqps[iq] = 0;
      sqps[iq] = 0;
      cqds[iq] = (iq == 0) ? ACQD : CQD;
      sqds[iq] = (iq == 0) ? ASQD : SQD;
      printf("%d cqphy=%lx sqphy=%lx\n", iq, v2p((size_t)cqs[iq]), v2p((size_t)sqs[iq]));
    }
  }
    
  regs64[0x30 / sizeof(uint64_t)] = v2p((size_t)cqs[0]); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = v2p((size_t)sqs[0]); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = (cqds[0] << 16) | sqds[0]; // Admin Queue Entry Num

  
  printf("CQ phyaddr %p %lx\n", cqs[0], regs64[0x30 / sizeof(uint64_t)]);
  printf("SQ phyaddr %p %lx\n", sqs[0], regs64[0x28 / sizeof(uint64_t)]);

  // enable controller.
  cc = 0x460001;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc enable
  sleep(3);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  assert(csts == 1);


  const int sz = 2*1024*1024;
  buf = mmap(NULL, sz, PROT_READ | PROT_WRITE,
	     MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);

  // identity
  {
    printf("identity cmd...\n");
    volatile sqe_t *sqe = &sqs[0][sqps[0]];
    printf("%p\n", sqe);
    bzero((void*)sqe, sizeof(sqe_t));
    bzero((void*)buf, 4096);
    sqe->CDW0.OPC = 0x6; // identity
    sqe->PRP1 = (uint64_t) v2p((size_t)buf);
    sqe->NSID = 0xffffffff;
    sqe->CDW10 = 0x1;
    sync_cmd(0);

    IdentifyControllerData *idata = (IdentifyControllerData *)buf;
    printf("  VID: %4X\n", idata->VID);
    printf("SSVID: %4X\n", idata->SSVID);
    printf("   SN: %.20s\n", idata->SN);
    printf("   MN: %.40s\n", idata->MN);
    printf("   FR: %.8s\n", idata->FR);    
  }

  // CQ create
  {
    volatile sqe_t *sqe = &sqs[0][sqps[0]];
    int qid = 1;
    bzero((void*)sqe, sizeof(sqe_t));
    printf("%p\n", sqe);
    sqe->CDW0.OPC = 0x5; // create CQ
    sqe->PRP1 = (uint64_t) v2p((size_t)cqs[qid]);
    sqe->CDW10 = (cqds[qid] << 16) | qid;
    sqe->CDW11 = 1;
    
    sync_cmd(0);
    printf("CQ create done %d\n", cqds[1]);
  }

  // SQ create
  {
    volatile sqe_t *sqe = &sqs[0][sqps[0]];
    int qid = 1;
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x1; // create SQ
    sqe->PRP1 = (uint64_t) v2p((size_t)sqs[qid]);
    sqe->CDW10 = (sqds[qid] << 16) | qid;
    sqe->CDW11 = (qid << 16) | 1; // physically contiguous
    sync_cmd(0);
    printf("SQ create done\n");
  }

  // get namespaces
  {
    volatile sqe_t *sqe = &sqs[0][sqps[0]];
    bzero((void*)sqe, sizeof(sqe_t));
    sqe->CDW0.OPC = 0x6; // identity
    bzero((void*)buf, 4096);
    sqe->PRP1 = (uint64_t) v2p((size_t)buf);
    sqe->NSID = 0;
    sqe->CDW10 = 2;
    sync_cmd(0);

    uint32_t *id_list = (uint32_t *)buf;
    int i;
    for (i=0; i<1024; i++) {
      if (id_list[i] == 0) break;
      printf("namespace %d\n", id_list[i]);
    }
  }
  
}


void
nvme_flush()
{
  int qid = 1;
  volatile sqe_t *sqe = &sqs[qid][sqps[qid]];
  bzero((void*)sqe, sizeof(sqe_t));
  sqe->CDW0.OPC = 0x0; // flush
  sqe->NSID = 1;
  sync_cmd(qid);
}

void
nvme_read(uint64_t lba, int num_blk)
{
  int qid = 1;
  volatile sqe_t *sqe = &sqs[qid][sqps[qid]];
  bzero((void*)sqe, sizeof(sqe_t));
  bzero((void*)buf, 4096);
  sqe->CDW0.OPC = 0x2; // read
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(qid);

  printf("%x\n", ((uint32_t*)buf)[0]);
}

void
nvme_write(uint64_t lba, int num_blk)
{
  volatile sqe_t *sqe = &sqs[1][sqps[1]];
  bzero((void*)sqe, sizeof(sqe_t));
  memset((void *)buf, 0x5a, 4096);
  sqe->CDW0.OPC = 0x1; // write
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(1);
}

int
main()
{
  init();
  //nvme_write(0, 1);
  nvme_read(0, 1);
}
