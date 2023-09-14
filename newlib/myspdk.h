#ifndef __MYSPDK_H__
#define __MYSPDK_H__


void myspdk_init();
int myspdk_read_req(int id, int fd, long count, long pos);
int myspdk_read_comp(int id, char *buf, long count);


#endif // __MYSPDK_H__
