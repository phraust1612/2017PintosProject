#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
#ifdef PRJ4
bool inode_create (disk_sector_t, off_t, uint32_t);
#else
bool inode_create (disk_sector_t, off_t);
#endif
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#ifdef PRJ4
#define DIRECT_NO 123
#include "filesys/cache.h"
bool allocate_inode_disk (uint32_t sectors, disk_sector_t inode_sector, off_t length, int add_direct_idx, uint32_t info, disk_sector_t origin_sector, uint32_t origin_sectors);
void release_inode_disk (uint32_t sectors, disk_sector_t inode_sector);
int inode_open_cnt (struct inode *);
void print_all_inodes (void);
uint32_t inode_get_info (struct inode *);
bool inode_is_directory (struct inode *);
uint32_t inode_get_level (struct inode *);
uint32_t inode_set_level (uint32_t, uint32_t);
#endif

#endif /* filesys/inode.h */
