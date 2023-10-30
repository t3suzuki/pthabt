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
#include "ult.h"

#define MYFS_BLOCK_SIZE (2*1024*1024)
#define NUM_BLOCKS (4*1024*1024)

#define MYFS_MAX_BLOCKS_PER_FILE (1024*256)
#define MYFS_MAX_NAMELEN (1024)
#define MYFS_MAX_FILES   (768)

typedef struct {
  char name[MYFS_MAX_NAMELEN];
  int32_t block[MYFS_MAX_BLOCKS_PER_FILE];
  uint64_t n_block;
  ult_mutex mutex;
} file_t;

typedef struct {
  uint64_t magic;
  int32_t free_blocks[NUM_BLOCKS];
  int32_t free_blocks_rp;
  int32_t free_blocks_wp;
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
  for (i=0; i<NUM_BLOCKS-1; i++) {
    superblock->free_blocks[i] = i;
  }
  superblock->free_blocks_rp = 0;
  superblock->free_blocks_wp = NUM_BLOCKS - 1;
  superblock->magic = MAGIC;
}

uint64_t
myfs_get_size(int i) {
  printf("file %d get n_block %ld\n", i, superblock->file[i].n_block);
  return superblock->file[i].n_block * MYFS_BLOCK_SIZE;
}

static int
myfs_used_blocks()
{
  int rp = superblock->free_blocks_rp;
  int wp = superblock->free_blocks_wp;
  if (rp < wp) {
    return rp + NUM_BLOCKS - wp - 1;
  } else {
    return rp - wp - 1;
  }
}

void
myfs_mount(char *myfs_superblock)
{
  int superblock_fd = open(myfs_superblock, O_RDWR);
  if (superblock_fd < 0) {
    perror("open");
  }
  size_t page_size = getpagesize();
  uint64_t mapped_size = 1024ULL*1024*1024;
  printf("sizeof(superblock_t)=%ld, mmapped_size=%ld\n", sizeof(superblock_t), mapped_size);
  superblock = (superblock_t *)mmap(0, mapped_size, PROT_READ|PROT_WRITE, MAP_SHARED, superblock_fd, 0);
  if (superblock == MAP_FAILED)
    perror("mmap superblock file");

  if (superblock->magic != MAGIC) {
    myfs_init();
  }
}

int
myfs_open(const char *filename)
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
  superblock->file[empty_i].n_block = 0;
  return empty_i;
}


int64_t
myfs_get_lba(int i, uint64_t offset, int write) {
  int i_block = offset / MYFS_BLOCK_SIZE;
  //printf("%s %d offset=%ld write=%d block=%d n_block=%d\n", __func__, i, offset, write, superblock->file[i].block[i_block], superblock->file[i].n_block);
  if (write > 0) {
    ult_mutex_lock(superblock->file[i].mutex);
    //printf("a fd=%d, i_block %d, block=%d rp=%d wp=%d\n", i, i_block, superblock->file[i].block[i_block], superblock->free_blocks_rp, superblock->free_blocks_wp);
    if (superblock->file[i].block[i_block] == INACTIVE_BLOCK) {
      {
	int old_val;
	while (1) {
	  old_val = superblock->free_blocks_rp;
	  int new_val = old_val + 1;
	  if (__sync_bool_compare_and_swap(&superblock->free_blocks_rp, old_val, new_val))
	    break;
	}
	superblock->file[i].block[i_block] = superblock->free_blocks[old_val];
      }
      superblock->file[i].n_block++;
    }
    ult_mutex_unlock(superblock->file[i].mutex);
  }
  //printf("%s fileid=%d i_block %d block %d offset %ld\n", __func__, i, i_block, superblock->file[i].block[i_block], (uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE);
  assert(superblock->file[i].block[i_block] != INACTIVE_BLOCK);
  int64_t lba = ((uint64_t)superblock->file[i].block[i_block] * MYFS_BLOCK_SIZE + (offset % MYFS_BLOCK_SIZE)) / 512;
  return lba;
}

void
myfs_close()
{
  fsync(superblock_fd);
  printf("%s %d  used_blocks=%d\n", __func__, __LINE__, myfs_used_blocks());
}

void
myfs_umount()
{
  myfs_close();
}


