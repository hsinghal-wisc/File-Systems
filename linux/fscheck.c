#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint.h>

#define T_DIR       1  
#define T_FILE      2  
#define T_DEV       3  
#define ROOTINO 1 
#define BS 512  
#define NDIR 12
#define NINDIRECT (BS / sizeof(uint))
#define MAXFILE (NDIR + NINDIRECT)
#define IPB           (BS / sizeof(struct dinode))
#define BPB           (BS*8)
#define DIRSIZ 14
struct superblock {
  uint size;        
  uint nblocks;   
  uint ninodes;   
};

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

struct dinode {
  short type;           
  short major;         
  short minor;         
  short nlink;         
  uint size;          
  uint addrs[NDIR+1]; 
};

void *img_ptr;
struct superblock *sb;
struct dinode *dip;
char *bitmap;
void *db;

uint get_addr(uint off, struct dinode *current_dip, int indirect_flag) {
    if (off / BS <= NDIR && !indirect_flag) {
      return current_dip->addrs[off / BS];
    } else {
      return *((uint*) (img_ptr + current_dip->addrs[NDIR] * BS) + off / BS - NDIR);
    }
  }

void addr_check(char *new_bitmap, uint addr) {
  if (addr == 0) {
    return;
  }
  
  if (addr < (sb->ninodes/IPB + sb->nblocks/BPB + 4) 
      || addr >= (sb->ninodes/IPB + sb->nblocks/BPB + 4 + sb->nblocks)) {
    fprintf(stderr, "ERROR: bad address in inode.\n");
    exit(1);
  }
  
  char byte = *(bitmap + addr / 8);
  if (!((byte >> (addr % 8)) & 0x01)) {
    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
    exit(1);
  }
}

int bitmap_cmp(char *bm1, char *bm2, int size) {
  int i;
  for (i = 0; i < size; ++i) {
    if (*(bm1++) != *(bm2++)) {
      return 1;
    }
  }

  return 0;
}

void dfs(int *inode_ref, char* new_bitmap, int inum, int parent_inum) {
  struct dinode *current_dip = dip + inum;
    
  if (current_dip->type == 0) {
    fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
    exit(1);
  }
  
  if (current_dip->type == T_DIR && current_dip->size == 0) {
    fprintf(stderr, "ERROR: directory not properly formatted.\n");
  }
  
  inode_ref[inum]++;
  if (inode_ref[inum] > 1 && current_dip->type == T_DIR) {
    fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
    exit(1);
  }
  
  int off;
  int indirect_flag = 0;
  
  for (off = 0; off < current_dip->size; off += BS) {
    uint addr = get_addr(off, current_dip, indirect_flag);
    addr_check(new_bitmap, addr);

    if (off / BS == NDIR && !indirect_flag) {
      off -= BS;
      indirect_flag = 1;
    }
    
    if (inode_ref[inum] == 1) {
      char byte = *(new_bitmap + addr / 8);
      if ((byte >> (addr % 8)) & 0x01) {
        fprintf(stderr, "ERROR: address used more than once.\n");
        exit(1);
      } else {
        byte = byte | (0x01 << (addr % 8));
        *(new_bitmap + addr / 8) = byte;
      }
    }
    
    if (current_dip->type == T_DIR) {
      struct dirent *de = (struct dirent *) (img_ptr + addr * BS);

      if (off == 0) {
        if (strcmp(de->name, ".")) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");          
          exit(1);
        }
        if (strcmp((de + 1)->name, "..")) {
          fprintf(stderr, "ERROR: directory not properly formatted.\n");
          exit(1);
        }
      
        if ((de + 1)->inum != parent_inum) {
          if (inum == ROOTINO) {
            fprintf(stderr, "ERROR: root directory does not exist.\n");
          } else {
            fprintf(stderr, "ERROR: parent directory mismatch.\n");
          }
          
          exit(1);
        }
        
        de += 2;
      }

      for (; de < (struct dirent *)(ulong)(img_ptr + (addr + 1) * BS); de++) {
        if (de->inum == 0) {
          continue;
        }

        dfs(inode_ref, new_bitmap, de->inum, inum);
      }
    }
  }
}

int main(int argc, char** argv) 
{
  int rc;
  struct stat sbuf;
  int fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "image not found.\n");
    exit(1);
  }

  rc = fstat(fd, &sbuf);
  assert(rc == 0);
  
  img_ptr = mmap(NULL, sbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  assert(img_ptr != MAP_FAILED);
  
  sb = (struct superblock *) (img_ptr + BS);

  int i;
  dip = (struct dinode *) (img_ptr + 2 * BS);
  bitmap = (char *) (img_ptr + (sb->ninodes / IPB + 3) * BS);
  db = (void *) (img_ptr + (sb->ninodes/IPB + sb->nblocks/BPB + 4) * BS);
  
  int bitmap_size = (sb->nblocks + sb->ninodes/IPB + sb->nblocks/BPB + 4) / 8;
  int data_offset = sb->ninodes/IPB + sb->nblocks/BPB + 4;
  int inode_ref[sb->ninodes + 1];
  memset(inode_ref, 0, (sb->ninodes + 1) * sizeof(int));
  char new_bitmap[bitmap_size];

  memset(new_bitmap, 0, bitmap_size);
  memset(new_bitmap, 0xFF, data_offset / 8);
  char last = 0x00;
  for (i = 0; i < data_offset % 8; ++i) {
    last = (last << 1) | 0x01;
  }
  new_bitmap[data_offset / 8] = last;
  

  if (!(dip + ROOTINO) || (dip + ROOTINO)->type != T_DIR) {
    fprintf(stderr, "ERROR: root directory does not exist.\n");
    exit(1);
  }
  
  dfs(inode_ref, new_bitmap, ROOTINO, ROOTINO);
  
  struct dinode *current_dip = dip;
  
  for (i = 1; i < sb->ninodes; i++) {
    current_dip++;

    if (current_dip->type == 0) {
      continue;
    }
    

    if (current_dip->type != T_FILE 
        && current_dip->type != T_DIR 
        && current_dip->type != T_DEV) {
      fprintf(stderr, "ERROR: bad inode.\n");
      exit(1);
    }
    
  
    if (inode_ref[i] == 0) {
      fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
      exit(1);
    }
    
    if (inode_ref[i] != current_dip->nlink) {
      fprintf(stderr, "ERROR: bad reference count for file.\n");
      exit(1);
    }
    
  
    if (current_dip->type == T_DIR && current_dip->nlink > 1) {
      fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
      exit(1);
    }
  }


  if (bitmap_cmp(bitmap, new_bitmap, bitmap_size)) {
    fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
    exit(1);
  }
  
  return 0;
}
