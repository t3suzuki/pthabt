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
#include "common.h"
#include <time.h>

//extern void (*debug_print)(long, long, long);

extern "C" {
#include "nvme.h"
  
#define ND (2)
#define NQ (N_TH)
#define QD (N_ULT*2)
#define AQD (8)

#define RAID_FACTOR (4096 / 512)
#define N_2MB_PAGE MAX(QD * BLKSZ / (2*1024*1024), 1)

static uint64_t stat_read_count[NQ+1];
static double stat_read_lasttime[NQ+1];

void increment_read_count(int qid) {
  stat_read_count[qid]++;
  struct timespec tsc;
  if (stat_read_count[qid] % (1024*1024) == 0) {
    clock_gettime(CLOCK_MONOTONIC, &tsc);
    double cur = tsc.tv_sec + tsc.tv_nsec * 1e-9;
    double delta = cur - stat_read_lasttime[qid];
    printf("th=%d, delta=%f, %f KIOPS\n", qid, delta, 1024*1024/delta/1000);
    stat_read_lasttime[qid] = cur;
  }
}

  
static int enable_bus_master(int uio_index)
{
  char path[256];
  sprintf(path, "/sys/class/uio/uio%d/device/config", uio_index);
  int fd = open(path, O_RDWR);
  uint32_t val;
  int ret = pread(fd, &val, 4, 0x4);
  val = 0x06;
  ret = pwrite(fd, &val, 4, 0x4);
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

  typedef struct {
    int cid;
    int done;
    int len;
    char *rbuf;
  } req_t;

class QP {
public:
  int n_sqe;
  int n_cqe;
  const int sq_offset = 0x4000;
private:
  int sq_tail;
  //int sq_head;
  int cq_head;
  int cq_phase;
  char *sqcq;
  int done_flag[QD];
  volatile uint32_t *doorbell;
  char *buf4k[N_2MB_PAGE];
  uint64_t _buf4k_pa[N_2MB_PAGE];
  int _qid;
  volatile uint32_t *_regs32;
  volatile uint64_t *_regs64;
public:
  void *rbuf[QD];
  int len[QD];
  inline cqe_t *get_cqe(int index) {
    return (cqe_t *)(sqcq + 0x0) + index;
  }
  inline sqe_t *get_sqe(int index) {
    return (sqe_t *)(sqcq + sq_offset) + index;
  }
  QP(int qid, volatile uint32_t *regs32) {
    _regs32 = regs32;
    _regs64 = (volatile uint64_t*)regs32;
    _qid = qid;
    sq_tail = 0;
    //sq_head = 0;
    cq_head = 0;
    cq_phase = 1;
    n_sqe = (qid == 0) ? AQD : QD;
    n_cqe = (qid == 0) ? AQD : QD;
    sqcq = malloc_2MB();
    //printf("N_2MB_PAGE %d %d\n", N_2MB_PAGE, QD*BLKSZ, QD*BLKSZ/2);
    for (int i=0; i<N_2MB_PAGE; i++) {
      buf4k[i] = malloc_2MB();
      _buf4k_pa[i] = v2p((size_t)buf4k[i]);
    }
    for (int i=0; i<QD; i++) {
      done_flag[i] = 2;
    }
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
    int cid_upper = cid % N_2MB_PAGE;
    int cid_lower = cid / N_2MB_PAGE;
    return _buf4k_pa[cid_upper] + BLKSZ * cid_lower;
  }
  sqe_t *new_sqe(int *ret_cid = nullptr) {
    sqe_t *sqe = get_sqe(sq_tail);
    int new_sq_tail = (sq_tail + 1) % n_sqe;
    if (done_flag[sq_tail] == 0) {
      //printf("block %d %d\n", new_sq_tail, cq_head);
      while (done_flag[sq_tail] == 0) {
	check_cq();
      }
    }
    done_flag[sq_tail] = 0;
    if (ret_cid) {
      *ret_cid = sq_tail;
    }
    //printf("new_sqe %d %d\n", new_sq_tail, cq_head);
    sq_tail = new_sq_tail;
    return sqe;
  }
  void sq_doorbell() {
    asm volatile ("" : : : "memory");
    *(doorbell) = sq_tail;
  }
  char *get_buf4k(int cid) {
    int cid_upper = cid % N_2MB_PAGE;
    int cid_lower = cid / N_2MB_PAGE;
    return buf4k[cid_upper] + BLKSZ * cid_lower;
  }
  void check_cq() {
    /*
    if (cq_head < 0) {
      printf("%d %d %d %p\n", cq_head, sq_tail, cq_phase, sqcq);
      assert(0);
    }
    */
    volatile cqe_t *cqe = get_cqe(cq_head);
    if (cqe->SF.P == cq_phase) {
      do {
	int cid = cqe->SF.CID;
	if (0) {
	  printf("cmd done cid = %d sct=%d sc=%x flag %d\n", cid, cqe->SF.SCT, cqe->SF.SC, cqe->SF.P);
	  printf("buf4k=%p len=%d outbuf=%p\n", get_buf4k(cid), len[cid], rbuf[cid]);
	  /*
	  {
	    unsigned char *buf = (unsigned char *)get_buf4k(cid);
	    printf("buf = %p\n", buf);
	    int i;
	    for (i=0; i<512; i++) {
	      printf("%02x ", buf[i]);
	      if (i % 16 == 15)
		printf("\n");
	    }
	    printf("\n");
	  }
	  */
	}
	/*
	int tmp = cqe->SF.CID >> 8;
	printf("cmd done sqhd=%d %d\n",  cqe->SQHD, cqe->SQID);
	printf("buf4k(cid)[0]=%d\n", ((unsigned char *)get_buf4k(cid))[0]);
	printf("lba lower 8-bits %d\n", tmp);
	*/
	//sq_head = cqe->SQHD;
	if (rbuf[cid]) {
	  memcpy(rbuf[cid], get_buf4k(cid), len[cid]);
	  /* {
	    unsigned char *buf = (unsigned char *)rbuf[cid];
	    printf("buf = %p\n", buf);
	    int i;
	    for (i=0; i<512; i++) {
	      printf("%02x ", buf[i]);
	      if (i % 16 == 15)
		printf("\n");
	    }
	    printf("\n");
	    } */
	}
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
    return (done_flag[cid] == 1);
  }
};


QP *qps[ND][NQ+1]; // +1 is for admin queue.


void
create_qp(int did, int new_qid)
{
  // CQ create
  {
    int cid;
    volatile sqe_t *sqe = qps[did][0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x5; // create CQ
    sqe->PRP1 = qps[did][new_qid]->cq_pa();
    sqe->NSID = 0;
    sqe->CDW10 = ((qps[did][new_qid]->n_cqe - 1) << 16) | new_qid;
    sqe->CDW11 = 1;
    //printf("%p %d %p %x\n", sqe, cid, sqe->PRP1, sqe->CDW10);
    qps[did][0]->req_and_wait(cid);
  }
  // SQ create
  {
    int cid;
    volatile sqe_t *sqe = qps[did][0]->new_sqe(&cid);
    sqe->CDW0.OPC = 0x1; // create SQ
    sqe->PRP1 = qps[did][new_qid]->sq_pa();
    sqe->NSID = 0;
    sqe->CDW10 = ((qps[did][new_qid]->n_sqe - 1) << 16) | new_qid;
    sqe->CDW11 = (new_qid << 16) | 1; // physically contiguous
    qps[did][0]->req_and_wait(cid);
  }
  printf("CQ/SQ create done %d\n", new_qid);
}

int
nvme_init(int did, int uio_index)
{
  volatile uint32_t *regs32;
  volatile uint64_t *regs64;

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

  QP *adq = new QP(0, regs32);
  qps[did][0] = adq;  
  regs64[0x30 / sizeof(uint64_t)] = adq->cq_pa(); // Admin CQ phyaddr
  regs64[0x28 / sizeof(uint64_t)] = adq->sq_pa(); // Admin SQ phyaddr
  regs32[0x24 / sizeof(uint32_t)] = ((adq->n_cqe - 1) << 16) | (adq->n_sqe - 1); // Admin Queue Entry Num
  //printf("%p %lx %p %lx\n", adq->get_cqe(0), adq->cq_pa(), adq->get_sqe(0), adq->sq_pa());

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
    volatile sqe_t *sqe = adq->new_sqe(&cid);
    sqe->CDW0.OPC = 0x6; // identity
    sqe->NSID = 0xffffffff;
    sqe->CDW10 = 0x1;
    sqe->PRP1 = adq->buf4k_pa(cid);
    adq->req_and_wait(cid);
  }

  {
    int iq;
    for (iq=1; iq<=NQ; iq++) {
      qps[did][iq] = new QP(iq, regs32);
      create_qp(did, iq);
      //printf("%p %lx %p %lx\n", qps[iq]->get_cqe(0), qps[iq]->cq_pa(), qps[iq]->get_sqe(0), qps[iq]->sq_pa());
    }
  }


  return 0;
}

int
nvme_read_req(uint32_t lba, int num_blk, int qid, int len, char *buf)
{
  int cid;
  int did = (lba / RAID_FACTOR) % ND;

  QP *qp = qps[did][qid];  
  sqe_t *sqe = qp->new_sqe(&cid);
  //qps[qid]->get_buf4k(cid)[0] = 0xff;
  //bzero(sqe, sizeof(sqe_t));
  sqe->PRP1 = qp->buf4k_pa(cid);
  sqe->CDW0.OPC = 0x2; // read
  //sqe->NSID = 1;
  sqe->CDW10 = ((lba / RAID_FACTOR) / ND * RAID_FACTOR) + (lba % RAID_FACTOR);
  sqe->CDW12 = num_blk - 1;
  //sqe->CDW0.CID = ((lba & 0xff) << 8) | cid;
  //if (debug_print)
  //debug_print(520, lba, cid);
  //printf("read_req qid = %d cid = %d lba = %d\n", qid, cid, lba);
  /*
  bzero(qps[qid]->get_buf4k(cid), 512);
  {
    unsigned char *buf = (unsigned char *)qps[qid]->get_buf4k(cid);
    int i;
    for (i=0; i<512; i++) {
      printf("%02x ", buf[i]);
      if (i % 16 == 15)
	printf("\n");
    }
  }
  */
  
  //printf("read_req qid=%d cid=%d lba=%d buf4k=%p buf4k[0]=%02x\n", qid, cid, lba, qps[qid]->get_buf4k(cid), qps[qid]->get_buf4k(cid)[0]);
  qp->rbuf[cid] = buf;
  qp->len[cid] = len;
  qp->sq_doorbell();
  return cid;
}

int
nvme_read_check(int lba, int qid, int cid)
{
  int did = (lba / RAID_FACTOR) % ND;
  QP *qp = qps[did][qid];
  unsigned char c = qp->get_buf4k(cid)[0];
  qp->check_cq();
  if (qp->done(cid)) {
    //printf("read_cmp qid = %d cid = %d buf[0]=%d %d %p buf4k[0]=%d->%d\n", qid, cid, ((unsigned char*)buf)[0], len, qps[qid]->get_buf4k(cid), c, (unsigned char)(qps[qid]->get_buf4k(cid)[0]));
    //printf("read_cmp qid = %d cid = %d %d\n", qid, cid, len);    
    /*
    if (debug_print) {
      debug_print(521, cid, -1);
      debug_print(522, ((unsigned int*)buf)[0], ((unsigned int*)(qps[qid]->get_buf4k(cid)))[0]);
    }
    */
    increment_read_count(qid);
    return 1;
  }
  return 0;
}

int
nvme_write_req(uint32_t lba, int num_blk, int qid, int len, char *buf)
{
  int cid;
  int did = (lba / RAID_FACTOR) % ND;
  QP *qp = qps[did][qid];
  sqe_t *sqe = qp->new_sqe(&cid);
  memcpy(qp->get_buf4k(cid), buf, len);
  sqe->PRP1 = qp->buf4k_pa(cid);
  sqe->CDW0.OPC = 0x1; // write
  sqe->CDW10 = ((lba / RAID_FACTOR) / ND * RAID_FACTOR) + (lba % RAID_FACTOR);
  sqe->CDW12 = num_blk - 1;
  //printf("%s %d lba=%d num_blk=%d qid=%d len=%d cid=%d\n", __func__, __LINE__, lba, num_blk, qid, len, cid);
  qp->rbuf[cid] = NULL;
  qp->len[cid] = 0;
  qp->sq_doorbell();
  return cid;
}

int
nvme_write_check(int lba, int qid, int cid)
{
  int did = (lba / RAID_FACTOR) % ND;
  QP *qp = qps[did][qid];
  qp->check_cq();
  if (qp->done(cid)) {
    //printf("write ok %d\n", cid);
    return 1;
  }
  return 0;
}

  
#if 0
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
    
    cid = nvme_read_req(lba, 1, qid, len, rbuf);
    while (1) {
      if (nvme_read_check(qid, cid))
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
    cids[j] = nvme_read_req(lba, 1, qid, len, rbuf);
  }
  for (j=0; j<16; j++) {
    while (1) {
      if (nvme_read_check(qid, cids[j]))
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
