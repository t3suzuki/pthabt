#ifndef __NVME_H__
#define __NVME_H__


int nvme_read_req(uint32_t lba, int num_blk, int qid, int len, char *buf);
int nvme_read_check(int qid, int cid);
int nvme_write_req(uint32_t lba, int num_blk, int qid, int len, char *buf);
int nvme_write_check(int qid, int cid);

int nvme_init();

#endif __NVME_H__
