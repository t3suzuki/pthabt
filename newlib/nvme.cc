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

extern "C" {
#include "nvme.h"
  
#define NQ (8)

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

static volatile uint32_t *regs32;
static volatile uint64_t *regs64;

char *malloc_2MB()
{
  const int sz = 2*1024*1024;
  char *buf = (char *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
			   MAP_PRIVATE | MAP_HUGETLB | MAP_ANONYMOUS | MAP_POPULATE, 0, 0);
  if (buf == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }
  bzero(buf, sz);
  return buf;
}


#define QD (1024)
#define AQD (8)
class QP {
public:
  int n_sqe;
  int n_cqe;
  const int sq_offset = 0x4000;
  const int chunk_size = 512;
private:
  int sq_tail;
  int cq_head;
  int cq_phase;
  char *sqcq;
  int done_flag[QD];
  volatile uint32_t *doorbell;
  char *buf4k;
  uint64_t _buf4k_pa;
  int _qid;
public:
  inline cqe_t *get_cqe(int index) {
    return (cqe_t *)(sqcq + 0x0) + index;
  }
  inline sqe_t *get_sqe(int index) {
    return (sqe_t *)(sqcq + sq_offset) + index;
  }
  QP(int qid) {
    _qid = qid;
    sq_tail = 0;
    cq_head = 0;
    cq_phase = 1;
    n_sqe = (qid == 0) ? AQD : QD;
    n_cqe = (qid == 0) ? AQD : QD;
    sqcq = malloc_2MB();
    buf4k = malloc_2MB();
    _buf4k_pa = v2p((size_t)buf4k);
    for (int i=0; i<n_sqe; i++) {
      sqe_t *sqe = get_sqe(i);
      bzero((void*)sqe, sizeof(sqe_t));
      sqe->NSID = 1;
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
  uint64_t buf4k_pa(int cid) {
    return _buf4k_pa + chunk_size * cid;
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
    *(doorbell) = sq_tail;
  }
  char *get_buf4k(int cid) {
    return buf4k + chunk_size * cid;
  }
  void check_cq() {
    volatile cqe_t *cqe = get_cqe(cq_head);
    if (cqe->SF.P == cq_phase) {
      do {
	int cid = cqe->SF.CID;
	//printf("cmd done cid=%d sct=%d sc=%x flag %d\n", cqe->SF.CID, cqe->SF.SCT, cqe->SF.SC, cqe->SF.P);
	done_flag[cid] = 1;
	cq_head++;
	if (cq_head == n_cqe) {
	  cq_head = 0;
	  cq_phase ^= 1;
	}
	cqe = get_cqe(cq_head);
      } while (cqe->SF.P == cq_phase);
      *(doorbell+1) = cq_head;
    }
  }
  void req_and_wait(int cid) {
    sq_doorbell();
    while (1) {
      check_cq();
      if (done(cid))
	break;
      //sleep(1);
    }
  }
  int done(int cid) {
    return done_flag[cid];
  }
};


QP *qps[NQ];


void
create_qp(int new_qid)
{
  // CQ create
  {
    int cid;
    volatile sqe_t *sqe = qps[0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x5; // create CQ
    sqe->PRP1 = qps[new_qid]->cq_pa();
    sqe->NSID = 0;
    sqe->CDW10 = ((qps[new_qid]->n_cqe - 1) << 16) | new_qid;
    sqe->CDW11 = 1;
    //printf("%p %d %p %x\n", sqe, cid, sqe->PRP1, sqe->CDW10);
    qps[0]->req_and_wait(cid);
  }
  // SQ create
  {
    int cid;
    volatile sqe_t *sqe = qps[0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x1; // create SQ
    sqe->PRP1 = qps[new_qid]->sq_pa();
    sqe->NSID = 0;
    sqe->CDW10 = ((qps[new_qid]->n_sqe - 1) << 16) | new_qid;
    sqe->CDW11 = (new_qid << 16) | 1; // physically contiguous
    qps[0]->req_and_wait(cid);
  }
  printf("CQ/SQ create done %d\n", new_qid);
}

int
nvme_init()
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
  //csts = regs32[0x1c / sizeof(uint32_t)];
  //printf("csts = %u\n", csts);

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
  regs32[0x24 / sizeof(uint32_t)] = ((qps[0]->n_cqe - 1) << 16) | (qps[0]->n_sqe - 1); // Admin Queue Entry Num
  //printf("%p %lx %p %lx\n", qps[0]->get_cqe(0), qps[0]->cq_pa(), qps[0]->get_sqe(0), qps[0]->sq_pa());

  // enable controller.
  cc = 0x460001;
  regs32[0x14 / sizeof(uint32_t)] = cc; // cc enable
  sleep(3);
  csts = regs32[0x1c / sizeof(uint32_t)]; // check csts
  //printf("csts = %u\n", csts);
  assert(csts == 1);


  // identity
  {
    printf("identity cmd...\n");
    int cid;
    volatile sqe_t *sqe = qps[0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x6; // identity
    sqe->NSID = 0xffffffff;
    sqe->CDW10 = 0x1;
    sqe->PRP1 = qps[0]->buf4k_pa(cid);
    qps[0]->req_and_wait(cid);
  }

  {
    int iq;
    for (iq=1; iq<NQ; iq++) {
      qps[iq] = new QP(iq);
      create_qp(iq);
      //printf("%p %lx %p %lx\n", qps[iq]->get_cqe(0), qps[iq]->cq_pa(), qps[iq]->get_sqe(0), qps[iq]->sq_pa());
    }
  }


  return 0;
}

int
nvme_read_req(uint32_t lba, int num_blk, int qid)
{
  int cid;
  sqe_t *sqe = qps[qid]->new_sqe(&cid);
  sqe->PRP1 = qps[qid]->buf4k_pa(cid);
  sqe->CDW0.OPC = 0x2; // read
  sqe->CDW10 = lba;
  sqe->CDW12 = num_blk;
  qps[qid]->sq_doorbell();
  return cid;
}

int
nvme_write_req(uint32_t lba, int num_blk, int qid, int len, char *buf)
{
  int cid;
  sqe_t *sqe = qps[qid]->new_sqe(&cid);
  memcpy(qps[qid]->get_buf4k(cid), buf, len);
  sqe->PRP1 = qps[qid]->buf4k_pa(cid);
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
    memcpy(buf, qps[qid]->get_buf4k(cid), len);
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

  
#if 1
char wbuf[4096];
char rbuf[4096];
int
main()
{
  nvme_init();

  int qid = 1;
  int lba = 0;
  int cid;
  int len = 512;
  int i;
  int j;
  int cids[16];

  //nvme_write2(lba, 1);
  for (j=0; j<16; j++) {
    for (i=0; i<512; i++) {
      wbuf[i] = 0x5a + j;
    }
    cid = nvme_write_req(lba, 1, qid, len, wbuf);
    while (1) {
      if (nvme_write_check(qid, cid))
	break;
    }
    
    cid = nvme_read_req(lba, 1, qid);
    while (1) {
      if (nvme_read_check(qid, cid, len, rbuf))
	break;
    }
    printf("%x\n", rbuf[0]);
  }

  for (j=0; j<16; j++) {
    wbuf[0] = 0x5 + j;
    lba = j;
    cids[j] = nvme_write_req(lba, 1, qid, len, wbuf);
  }
  for (j=0; j<16; j++) {
    while (1) {
      if (nvme_write_check(qid, cids[j]))
	break;
    }
  }

  for (j=0; j<16; j++) {
    lba = j;
    cids[j] = nvme_read_req(lba, 1, qid);
  }
  for (j=0; j<16; j++) {
    while (1) {
      if (nvme_read_check(qid, cids[j], len, rbuf))
	break;
    }
    printf("%x\n", rbuf[0]);
  }

  //nvme_write(0, 1);
  //nvme_read(0, 1);
  return 0;
}
#endif

}
