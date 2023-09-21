#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define MYFS_BLOCK_SIZE (1024*1024)

#define MYFS_MAX_BLOCKS_PER_FILE (1024)
#define MYFS_MAX_NAMELEN (1024)
#define MYFS_MAX_FILES   (4096)

typedef struct {
  char name[MYFS_MAX_NAMELEN];
  int32_t block[MYFS_MAX_BLOCKS_PER_FILE];
} file_t;

static file_t file[MYFS_MAX_FILES];
static int block_wp = 0;

void
myfs_init()
{
  int i, j;
  for (i=0; i<MYFS_MAX_FILES; i++) {
    file[i].name[0] = '\0';
    for (j=0; j<MYFS_MAX_BLOCKS_PER_FILE; j++) {
      file[i].block[j] = -1;
    }
  }
}

int
myfs_open(char *filename)
{
  int i;
  int empty_i = -1;
  for (i=0; i<MYFS_MAX_FILES; i++) {
    if (strncmp(filename, file[i].name, strlen(filename)) == 0) {
      printf("%s found %s %s\n", __func__, filename, file[i].name);
      return i;
    }
    if ((empty_i == -1) && (file[i].name[0] == '\0')) {
      empty_i = i;
    }
  }
  printf("%s not found\n", __func__);
  strncpy(file[empty_i].name, filename, strlen(filename));
  return empty_i;
}

int
myfs_get_lba(int fd, uint64_t offset, int write) {
  int i_block = offset / MYFS_BLOCK_SIZE;
  if (write) {
    if (file[fd].block[i_block] == -1) {
      i_block = block_wp++;
    }
  }
  return (uint64_t)file[fd].block[i_block] * MYFS_BLOCK_SIZE + (offset % MYFS_BLOCK_SIZE) / 512;
}



int main()
{
  myfs_init();
  int fd0 = myfs_open("file0");
  int fd1 = myfs_open("file1");

}
