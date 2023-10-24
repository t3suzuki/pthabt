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
#include <assert.h>
#include "nvme.h"
#include "common.h"

#define MYFS_BLOCK_SIZE (2*1024*1024)

#define MYFS_MAX_BLOCKS_PER_FILE (1024*256)
#define MYFS_MAX_NAMELEN (1024)
#define MYFS_MAX_FILES   (1024*16)

typedef struct {
  char name[MYFS_MAX_NAMELEN];
  int32_t block[MYFS_MAX_BLOCKS_PER_FILE];
  uint64_t total_size;
  uint64_t tail_block;
  ABT_mutex abt_mutex;
} file_t;

typedef struct {
  uint64_t magic;
  uint64_t block_wp;
  file_t file[MYFS_MAX_FILES];
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
      superblock->file[i].block[j] = INACTIVE_BLOCK;
    }
  }
  superblock->magic = MAGIC;
  superblock->block_wp = 0;
}

uint64_t
myfs_get_size(int i) {
  printf("file %d get tail_block %ld\n", i, superblock->file[i].tail_block);
  return superblock->file[i].tail_block * MYFS_BLOCK_SIZE;
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
  superblock = (superblock_t *)mmap(0, page_size*16384*2, PROT_READ|PROT_WRITE, MAP_SHARED, superblock_fd, 0);
  if (superblock == MAP_FAILED)
    perror("mmap superblock file");

  if (superblock->magic != MAGIC) {
    myfs_init();
  }
  printf("%s %d  wp=%ld\n", __func__, __LINE__, superblock->block_wp);
}

int
myfs_open(char *filename)
{
  int i;
  int empty_i = -1;
  for (i=0; i<MYFS_MAX_FILES; i++) {
    //printf("%s check %s\n", __func__, superblock->file[i].name);
    if (strncmp(filename, superblock->file[i].name, strlen(filename)) == 0) {
      //printf("%s found %s fileid=%d\n", __func__, filename, i);
      return i;
    }
    if ((empty_i == -1) && (superblock->file[i].name[0] == '\0')) {
      empty_i = i;
    }
  }
  printf("%s not found. new fileid=%d for %s\n", __func__, empty_i, filename);
  strncpy(superblock->file[empty_i].name, filename, strlen(filename)+1);
  superblock->file[empty_i].total_size = 0;
  superblock->file[empty_i].tail_block = 0;
  return empty_i;
}


int64_t
myfs_get_lba(int i, uint64_t offset, int write) {
  int i_block = offset / MYFS_BLOCK_SIZE;
  //printf("%s %d offset=%ld write=%d block=%d tail=%d\n", __func__, i, offset, write, superblock->file[i].block[i_block], superblock->file[i].tail_block);
  if (write > 0) {
    ABT_mutex_lock(superblock->file[i].abt_mutex);
    if (superblock->file[i].block[i_block] == INACTIVE_BLOCK) {
      superblock->file[i].block[i_block] = superblock->block_wp++;

      if (i_block+1 > superblock->file[i].tail_block) {
	superblock->file[i].tail_block = i_block+1;
	//printf("file %d update tail_block %ld\n", i, i_block+1);
      }
    }
    ABT_mutex_unlock(superblock->file[i].abt_mutex);
  }
  //printf("%s fileid=%d i_block %d block %d offset %ld\n", __func__, i, i_block, superblock->file[i].block[i_block], (uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE);
  assert(superblock->file[i].block[i_block] != INACTIVE_BLOCK);
  int64_t lba = ((uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE + (offset % MYFS_BLOCK_SIZE)) / 512;
  return lba;
}

void
myfs_umount()
{
  fsync(superblock_fd);
  printf("%s %d  wp=%ld\n", __func__, __LINE__, superblock->block_wp);
}


