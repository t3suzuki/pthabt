#ifndef __MYFS_H__
#define __MYFS_H__


void myfs_mount(char *myfs_superblock);
int myfs_open(char *filename);
int myfs_get_lba(int i, uint64_t offset, int write);
void myfs_umount();

#endif 
