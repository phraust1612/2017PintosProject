#ifndef __FILESYS_CACHE_H
#define __FILESYS_CACHE_H
#define BUFFER_CACHE_SIZE 64
#include "devices/disk.h"
#include "threads/synch.h"
#include "devices/disk.h"
#include "filesys/off_t.h"
#include "filesys/filesys.h"

void buffer_cache_init (void);
struct file_cache* buffer_cache_lookup (disk_sector_t sec_no);
uint32_t buffer_cache_find_victim (void);
void buffer_cache_read (disk_sector_t sec_no, void *buffer, off_t size, off_t offset);
void buffer_cache_write (disk_sector_t sec_no, void *buffer, off_t size, off_t offset);
#endif
