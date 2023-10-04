#define _GNU_SOURCE
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#define MYFS_BLOCK_SIZE (2*1024*1024)

#define MYFS_MAX_BLOCKS_PER_FILE (1024*32)
#define MYFS_MAX_NAMELEN (1024)
#define MYFS_MAX_FILES   (1024)

typedef struct {
  char name[MYFS_MAX_NAMELEN];
  int32_t block[MYFS_MAX_BLOCKS_PER_FILE];
  int32_t total_size;
} file_t;

typedef struct {
  uint64_t magic;
  file_t file[MYFS_MAX_FILES];
  int block_wp;
} superblock_t;


static superblock_t *superblock;
static int superblock_fd = -1;

#define MAGIC (0xdeadcafebabefaceULL)

static void
myfs_init()
{
  int i, j;
  printf("%s\n", __func__);
  for (i=0; i<MYFS_MAX_FILES; i++) {
    superblock->file[i].name[0] = '\0';
    for (j=0; j<MYFS_MAX_BLOCKS_PER_FILE; j++) {
      superblock->file[i].block[j] = -1;
    }
  }
  superblock->magic = MAGIC;
  superblock->block_wp = 0;
}

void
myfs_set_size(int i, int32_t sz) {
  superblock->file[i].total_size = sz;
}

int
myfs_get_size(int i) {
  return superblock->file[i].total_size;
}

int
myfs_get_size2(char *filename) {
  int i;
  for (i=0; i<MYFS_MAX_FILES; i++) {
    if (strncmp(filename, superblock->file[i].name, strlen(filename)) == 0) {
      return superblock->file[i].total_size;
    }
  }
  return -1;
}

void
myfs_mount(char *myfs_superblock)
{
  int superblock_fd = open(myfs_superblock, O_RDWR);
  int new_flag = 0;
  if (superblock_fd < 0) {
    if (errno == ENOENT) {
      printf("Failed to open superblock file. Then, create new superblock file.\n");
      superblock_fd = open(myfs_superblock, O_RDWR|O_CREAT, 0666);
      new_flag = 1;
    } else {
      perror("open");
    }
  }
  size_t page_size = getpagesize();
  //superblock = (superblock_t *)mmap(0, sizeof(superblock_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  superblock = (superblock_t *)mmap(0, page_size*4096, PROT_READ|PROT_WRITE, MAP_SHARED, superblock_fd, 0);
  if (superblock == MAP_FAILED)
    perror("mmap superblock file");

  if (superblock->magic != MAGIC) {
    myfs_init();
  }
}

int
myfs_open(char *filename)
{
  int i;
  int empty_i = -1;
  for (i=0; i<MYFS_MAX_FILES; i++) {
    if (strncmp(filename, superblock->file[i].name, strlen(filename)) == 0) {
      //printf("%s found %s %s\n", __func__, filename, superblock->file[i].name);
      //printf("%s found %s fileid=%d\n", __func__, filename, i);
      return i;
    }
    if ((empty_i == -1) && (superblock->file[i].name[0] == '\0')) {
      empty_i = i;
    }
  }
  //printf("%s not found. fileid=%d\n", __func__, empty_i);
  strncpy(superblock->file[empty_i].name, filename, strlen(filename));
  return empty_i;
}

int
myfs_get_lba(int i, uint64_t offset, int write) {
  int i_block = offset / MYFS_BLOCK_SIZE;
  if (write) {
    if (superblock->file[i].block[i_block] == -1) {
      superblock->file[i].block[i_block] = superblock->block_wp++;
      //printf("assign new block %d\n", superblock->file[i].block[i_block]);
    }
  }
  //printf("fileid=%d i_block %d block %d offset %ld\n", i, i_block, superblock->file[i].block[i_block], (uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE);
  return ((uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE + (offset % MYFS_BLOCK_SIZE)) / 512;
}

void
myfs_umount()
{
  fsync(superblock_fd);
  printf("%s %d\n", __func__, __LINE__);
}


#if 0
int main()
{
  //myfs_init();
  myfs_mount("myfs_superblock");
  int find0 = myfs_open("file0");
  int find1 = myfs_open("file2");
  printf("%d\n", myfs_get_lba(find1, 0, 1));
  printf("%d\n", myfs_get_lba(find0, 0, 1));
  printf("%d\n", myfs_get_lba(find0, 1024, 1));
  myfs_umount();
  //myfs_deinit();
}
#endif
