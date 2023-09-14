#ifndef __NVME_H__
#define __NVME_H__


int nvme_read_req(uint32_t lba, int num_blk, int qid);
int nvme_read_check(int qid, int cid, int len, char *buf);
int nvme_init();

#endif __NVME_H__
