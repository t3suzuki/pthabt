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
#include <vector>

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
#define SQD (512)
#define CQD (512)
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
}


char *malloc_2MB()
{
  const int sz = 2*1024*1024;
  char *buf = (char *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  if (buf == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  return buf;
}


#define QD (512)
class QP {
public:
  int n_sqe;
  int n_cqe;
  const int sq_offset = 0x4000;
private:
  int sq_tail;
  int cq_tail;
  int cq_phase;
  char *sqcq;
  int done_flag[QD];
  inline cqe_t *get_cqe(int index) {
    return (cqe_t *)(sqcq + 0x0) + index;
  }
  inline sqe_t *get_sqe(int index) {
    return (sqe_t *)(sqcq + sq_offset) + index;
  }
  volatile uint32_t *doorbell;
  char *buf;
public:
  QP(int qid) {
    sq_tail = 0;
    cq_tail = 0;
    cq_phase = 1;
    n_sqe = (qid == 0) ? 8 : QD;
    n_cqe = (qid == 0) ? 8 : QD;
    sqcq = malloc_2MB();
    buf = malloc_2MB();
    uint64_t buf_pa = v2p((size_t)buf);
    for (int i=0; i<n_sqe; i++) {
      sqe_t *sqe = get_sqe(i);
      bzero((void*)sqe, sizeof(sqe_t));
      sqe->NSID = 1;
      sqe->PRP1 = buf_pa + 4096 * i;
      sqe->CDW11 = 0;
      sqe->CDW0.CID = i;
    }
    doorbell = &regs32[0x1000 / sizeof(uint32_t) + 2 * qid];
  }
  uint64_t cq_pa() {
    return v2p((size_t)sqcq);
  }
  uint64_t sq_pa() {
    return v2p((size_t)sqcq) + sq_offset;
  }
  
  sqe_t *new_sqe(int *ret_cid = nullptr) {
    sqe_t *sqe = get_sqe(sq_tail);
    done_flag[sq_tail] = 0;
    if (ret_cid) {
      *ret_cid = sq_tail;
    }
    sq_tail = (sq_tail + 1) % n_sqe;
    return sqe;
  }
  void sq_doorbell() {
    asm volatile ("" : : : "memory");
    *(doorbell+0) = sq_tail;
  }
  char *get_buf(int cid) {
    return &buf[4096 + cid];
  }
  void check_cq() {
    while (1) {
      cqe_t *cqe = get_cqe(cq_tail);
      if (cqe->SF.P != cq_phase)
	break;
      assert(cqe->SF.SC == 0);
      int cid = cqe->SF.CID;
      done_flag[cid] = 1;
      cq_tail++;
      if (cq_tail == n_cqe) {
	cq_tail = 0;
	cq_phase ^= 1;
      }
    }
    *(doorbell+1) = cq_tail;
  }
  void req_and_wait() {
    sq_doorbell();
    check_cq();
  }
  int done(int cid) {
    return done_flag[cid];
  }
};


QP *qps[NQ];




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
  regs32 = (volatile uint32_t *)mmap(0, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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
  printf("csts = %u\n", csts);
  
  assert(csts == 0);


  qps[0] = new QP(0);
  
  regs64[0x30 / sizeof(uint64_t)] = qps[0]->cq_pa(); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = qps[0]->sq_pa(); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = (qps[0]->n_cqe << 16) | qps[0]->n_sqe; // Admin Queue Entry Num
  
  printf("CQ phyaddr %lx\n", regs64[0x30 / sizeof(uint64_t)]);
  printf("SQ phyaddr %lx\n", regs64[0x28 / sizeof(uint64_t)]);

  exit(1);

  // enable controller.
  cc = 0x460001;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc enable
  sleep(3);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  printf("%u\n", csts);
  assert(csts == 1);

  {
    int iq;
    for (iq=1; iq<NQ; iq++) {
      qps[iq] = new QP(iq);
      /*
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
      */
    }
  }
    

  // identity
  {
    printf("identity cmd...\n");
    int cid;
    sqe_t *sqe = qps[0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x6; // identity
    sqe->NSID = 0xffffffff;
    sqe->CDW10 = 0x1;
    qps[0]->req_and_wait();

    IdentifyControllerData *idata = (IdentifyControllerData *)qps[0]->get_buf(0);
    printf("  VID: %4X\n", idata->VID);
    printf("SSVID: %4X\n", idata->SSVID);
    printf("   SN: %.20s\n", idata->SN);
    printf("   MN: %.40s\n", idata->MN);
    printf("   FR: %.8s\n", idata->FR);    
  }

#if 0
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
#endif
  return 0;
}

int
nvme_read_req(uint32_t lba, int num_blk, int qid)
{
  int ret_cid;
  sqe_t *sqe = qps[qid]->new_sqe(&ret_cid);
  sqe->CDW0.OPC = 0x2; // read
  sqe->CDW10 = lba;
  sqe->CDW12 = num_blk;
  qps[qid]->sq_doorbell();
  return ret_cid;
}

int
nvme_write_req(uint32_t lba, int num_blk, int qid, int len, char *buf)
{
  int cid;
  sqe_t *sqe = qps[qid]->new_sqe(&cid);
  memcpy(qps[qid]->get_buf(cid), buf, len);
  sqe->CDW0.OPC = 0x1; // write
  sqe->CDW10 = lba;
  sqe->CDW12 = num_blk;
  qps[qid]->sq_doorbell();
  return cid;
}

int
nvme_read_check(int qid, int cid, int len, char *buf)
{
  qps[qid]->check_cq();
  if (qps[qid]->done(cid)) {
    memcpy(buf, qps[qid]->get_buf(cid), len);
    return 1;
  }
  return 0;
}

int
nvme_write_check(int qid, int cid)
{
  qps[qid]->check_cq();
  if (qps[qid]->done(cid)) {
    return 1;
  }
  return 0;
}

  
void
nvme_read(int qid, uint64_t lba, int num_blk)
{
  volatile sqe_t *sqe = &sqs[qid][sqps[qid]];
  bzero((void*)sqe, sizeof(sqe_t));
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(qid);
}

void
nvme_write(uint64_t lba, int num_blk)
{
  volatile sqe_t *sqe = &sqs[1][sqps[1]];
  bzero((void*)sqe, sizeof(sqe_t));
  sqe->CDW0.OPC = 0x1; // write
  sqe->PRP1 = (uint64_t) v2p((size_t)buf);
  sqe->NSID = 1;
  sqe->CDW10 = lba & 0xffffffff;
  sqe->CDW11 = (lba >> 32);
  sqe->CDW12 = num_blk;
  sync_cmd(1);
}


char wbuf[4096];
char rbuf[4096];
int
main()
{
  init();

  int qid = 1;
  int lba = 0;
  int cid;
  int len = 512;
  cid = nvme_write_req(lba, 1, qid, len, wbuf);
  /*
  nvme_write_check(qid, cid);

  cid = nvme_read_req(lba, 1, qid);
  nvme_read_check(qid, cid, len, rbuf);
  */
  
  //nvme_write(0, 1);
  //nvme_read(0, 1);
  return 0;
}
