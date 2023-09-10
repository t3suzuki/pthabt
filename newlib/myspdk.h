#ifndef __MYSPDK_H__
#define __MYSPDK_H__



void myspdk_read_req(int id, int fd, char *buf, long count, long pos);
int myspdk_read_comp(int id);


#endif // __MYSPDK_H__
