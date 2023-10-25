#ifndef __MYFS_H__
#define __MYFS_H__


void myfs_mount(char *myfs_superblock);
int myfs_open(const char *filename);
int64_t myfs_get_lba(int i, uint64_t offset, int write);
void myfs_umount(void);
void myfs_close(void);
uint64_t myfs_get_size(int i);

#endif 
