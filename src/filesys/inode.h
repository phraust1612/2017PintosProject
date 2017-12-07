#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t, uint32_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
uint32_t inode_get_info (struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
int inode_open_cnt (struct inode *);

// #define INODE_PRINT
#define EXTENSE_DEBUG
#define SUBDIR_DEBUG
#ifdef EXTENSE_DEBUG
#define DIRECT_NO 123
#include "filesys/cache.h"
bool jeonduhwan (uint32_t sectors, disk_sector_t inode_sector, off_t length, int add_direct_idx, uint32_t info);
void release_inode_disk (uint32_t sectors, disk_sector_t inode_sector);
#endif

#endif /* filesys/inode.h */
