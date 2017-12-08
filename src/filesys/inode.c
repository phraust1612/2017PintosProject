#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
#ifndef EXTENSE_DEBUG
    disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
#else
    disk_sector_t sector;               /* this inode_disk's sector */
    uint32_t info;                      /* 0 : file, 1 : dir */
    off_t length;                       /* File size in bytes. */
    int32_t direct[DIRECT_NO];
    int32_t indirect;
    unsigned magic;                     /* Magic number. */
#endif
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

static char zeros[DISK_SECTOR_SIZE];
static struct lock length_lock;

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
#ifdef SYNRW
    struct lock mutex;
    struct lock writer_lock;
    uint32_t readcount;
#endif
  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
#ifndef EXTENSE_DEBUG
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / DISK_SECTOR_SIZE;
  else
#endif
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init (&length_lock);
  buffer_cache_init ();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, uint32_t info)
{
#ifdef EXTENSE_DEBUG
  size_t sectors = bytes_to_sectors (length);
  return allocate_inode_disk (sectors, sector, length, 0, info);
#else
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start))
        {
          buffer_cache_write (sector, disk_inode, DISK_SECTOR_SIZE, 0);
          if (sectors > 0) 
            {
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                buffer_cache_write (disk_inode->start + i, zeros, DISK_SECTOR_SIZE, 0); 
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
#endif
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
#ifdef SYNRW
  lock_init (&inode->mutex);
  lock_init (&inode->writer_lock);
  inode->readcount = 0;
#endif
  buffer_cache_read (inode->sector, &inode->data, DISK_SECTOR_SIZE, 0);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

uint32_t
inode_get_info (struct inode *inode)
{
  return inode->data.info;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
#ifndef EXTENSE_DEBUG
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }

      free (inode); 
#else
      list_remove (&inode->elem);
      uint32_t sector_no = bytes_to_sectors (inode->data.length);
      if (inode->removed) 
      {
        release_inode_disk (sector_no, inode->sector);
        free_map_release (inode->sector, 1);
      }
      free (inode);
#ifdef INODE_PRINT
      printf ("inode_close - ends\n");
#endif
#endif
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint32_t length = inode->data.length;
#ifdef EXTENSE_DEBUG
#ifdef FILESIZE_PRINT
  printf ("tid : %d, length get : %d\n",\
      thread_current ()->tid, inode->data.length);
#endif
  if (offset >= length) return 0;
  struct inode_disk refer_inode_disk;
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
  uint32_t mok;
  uint32_t direct_idx = offset / DISK_SECTOR_SIZE % DIRECT_NO;
  for (mok = offset / (DISK_SECTOR_SIZE * DIRECT_NO);
        mok > 0;
        mok--)
    buffer_cache_read (refer_inode_disk.indirect, &refer_inode_disk,\
        DISK_SECTOR_SIZE, 0);
#endif
  int sector_ofs = offset % DISK_SECTOR_SIZE;
#ifdef INODE_PRINT
  printf("inode_read_at - before loop inode->data.sector : %d, "\
      "direct_idx : %d, direct[0] : %d, direct[1] : %d\n", \
      inode->data.sector, direct_idx,\
      refer_inode_disk.direct[0], refer_inode_disk.direct[1]);
#endif

  size = length - offset > size ? size : length - offset;
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
#ifndef EXTENSE_DEBUG
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
#else
      disk_sector_t sector_idx = refer_inode_disk.direct [direct_idx];
#endif
      if ((int) sector_idx < 0) break;

      uint32_t read_bytes = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;

      if (sector_ofs > 0)
      {
        read_bytes = DISK_SECTOR_SIZE - sector_ofs;
        read_bytes = read_bytes > size ? size : read_bytes;
      }

      if (read_bytes <= 0)
        break;

#ifdef INODE_PRINT
      printf("inode_read_at = sector idx : %d\n", sector_idx);
#endif
      buffer_cache_read (sector_idx, buffer + bytes_read, read_bytes, sector_ofs);
      /* zero bytes를 비워줄 수도 있다. */
      sector_ofs = 0;

      /* Advance. */
      size -= read_bytes;
      bytes_read += read_bytes;
#ifndef EXTENSE_DEBUG
      offset += read_bytes;
#else
      direct_idx++;
      if (direct_idx >= DIRECT_NO)
      {
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
        direct_idx = 0;
        // hex_dump(0, refer_inode_disk, 512, true);
      }
#endif
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
#ifdef EXTENSE_DEBUG
  uint32_t mok, info;
  info = inode_get_info (inode);
  struct inode_disk refer_inode_disk;
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
#ifdef INODE_PRINT
  printf ("inode_write_at - offset : %d, length : %d\n",\
      offset, inode->data.length);
#endif
  if (offset >= inode->data.length && size > 0)
  {
    /* finding refer_previous and start_direct_idx */
    int start_direct_idx = DIV_ROUND_UP (inode->data.length, DISK_SECTOR_SIZE);
    disk_sector_t refer_previous_sec_no = inode->data.sector;
#ifdef INODE_PRINT
    printf ("inode_write_at - start_direct_idx : %d, "\
        "refer_previous_sec_no : %d\n",\
        start_direct_idx, refer_previous_sec_no);
#endif
    while (start_direct_idx >= DIRECT_NO)
    {
      start_direct_idx -= DIRECT_NO;
      if (refer_inode_disk.indirect)
      {
        refer_previous_sec_no = refer_inode_disk.indirect;
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
      }
#ifdef INODE_PRINT
    printf ("inode_write_at - start_direct_idx : %d, "\
        "refer_previous_sec_no : %d, "\
        "refer_previous->direct[0] : %d\n",\
        start_direct_idx, refer_previous_sec_no,\
        refer_inode_disk.direct[0]);
#endif
    }

    /* 처음 쓰는 거면 새로 refer_previous의 indirect 는 아직
     * 할당이 안된 상태므로 해준다 */
    if (start_direct_idx == 0 && inode->data.length > 0)
    {
      free_map_allocate (1, &refer_previous_sec_no);
      refer_inode_disk.indirect = refer_previous_sec_no;
      buffer_cache_write (refer_inode_disk.sector,\
          &refer_inode_disk, DISK_SECTOR_SIZE, 0);
    }

    /* 총 필요한 direct 갯수 */
    uint32_t add_sector = bytes_to_sectors (offset + size) - \
      bytes_to_sectors (inode->data.length);

#ifdef INODE_PRINT
    printf ("inode_write_at - add_sector : %d\n",\
        add_sector);
#endif
    if (add_sector > 0)
      allocate_inode_disk (add_sector, refer_previous_sec_no, \
        offset + size, start_direct_idx, info);

    buffer_cache_read(inode->data.sector, &inode->data, DISK_SECTOR_SIZE, 0);
    inode->data.length = offset + size;
#ifdef FILESIZE_PRINT
    printf ("tid : %d, length set : %d\n",\
        thread_current ()->tid, offset + size);
#endif
    buffer_cache_write(inode->data.sector, &inode->data, DISK_SECTOR_SIZE, 0);
  }
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
  uint32_t direct_idx = offset / DISK_SECTOR_SIZE % DIRECT_NO;
  for (mok = offset / (DISK_SECTOR_SIZE * DIRECT_NO);
        mok > 0;
        mok--)
    buffer_cache_read (refer_inode_disk.indirect, &refer_inode_disk,\
        DISK_SECTOR_SIZE, 0);
#endif

  if (inode->deny_write_cnt)
    return 0;
  int sector_ofs = offset % DISK_SECTOR_SIZE;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
#ifndef EXTENSE_DEBUG
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
#else
      disk_sector_t sector_idx = refer_inode_disk.direct [direct_idx];
#endif
      if ((int) sector_idx < 0) break;

      uint32_t read_bytes = size > DISK_SECTOR_SIZE ? DISK_SECTOR_SIZE : size;

      if (sector_ofs > 0)
      {
        read_bytes = DISK_SECTOR_SIZE - sector_ofs;
        read_bytes = read_bytes > size ? size : read_bytes;
      }

      if (read_bytes <= 0)
        break;

      buffer_cache_write (sector_idx, buffer + bytes_written, read_bytes, sector_ofs);
      sector_ofs = 0;

      /* Advance. */
      size -= read_bytes;
      bytes_written += read_bytes;
#ifndef EXTENSE_DEBUG
      offset += read_bytes;
#else
      direct_idx++;
      if (direct_idx >= DIRECT_NO)
      {
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
        direct_idx = 0;
      }
#endif
    }

#ifdef FILESIZE_PRINT
  printf ("tid : %d inode_write_at ends\n",\
      thread_current ()->tid);
#endif
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

int
inode_open_cnt (struct inode *inode)
{
  return inode->open_cnt;
}

#ifdef EXTENSE_DEBUG
bool
allocate_inode_disk (uint32_t sectors, disk_sector_t inode_sector, off_t length, int start_direct_idx, uint32_t info)
{
  struct inode_disk *disk_inode = NULL;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return false;
  buffer_cache_read (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);

  disk_inode->length = length;
  disk_inode->sector = inode_sector;
  disk_inode->info = info;
  disk_inode->magic = INODE_MAGIC;
  int direct_alloc_num = sectors + start_direct_idx > DIRECT_NO ? DIRECT_NO : start_direct_idx + sectors;
  int i;
  for (i = start_direct_idx; i < direct_alloc_num; i++)
  {
    free_map_allocate (1, &disk_inode->direct[i]);
#ifdef INODE_PRINT
    printf("allocate_inode_disk - allocate direct index : %d, i : %d, "\
        "direct[0] : %d, inode_sector : %d\n", \
        disk_inode->direct[i], i, disk_inode->direct[0], \
        inode_sector);
#endif
    buffer_cache_write (disk_inode->direct[i], zeros, DISK_SECTOR_SIZE, 0);
  }

  if (sectors - i + start_direct_idx > 0)
  {
    disk_sector_t new_indirect_sector;
    free_map_allocate (1, &new_indirect_sector);
    disk_inode->indirect = new_indirect_sector;
    buffer_cache_write (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);
    free (disk_inode);
#ifdef INODE_PRINT
    printf("allocate_inode_disk - allocate indirect index : %d, i : %d, "\
        "direct[0] : %d, inode_sector : %d\n", \
        new_indirect_sector, i, disk_inode->direct[0], \
        inode_sector);
#endif
    return allocate_inode_disk (sectors - i + start_direct_idx, \
        new_indirect_sector, length, 0, info);
  }
  else
  {
    buffer_cache_write (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);
    free (disk_inode);
  }

  return true;
}

void
release_inode_disk (uint32_t sectors, disk_sector_t inode_sector)
{
  struct inode_disk *disk_inode = NULL;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);
  disk_inode = calloc (1, sizeof *disk_inode);

  buffer_cache_read (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);

  int direct_alloc_num = sectors > DIRECT_NO ? DIRECT_NO : sectors;
  int i;

  if (sectors - direct_alloc_num > 0)
  {
    release_inode_disk (sectors - direct_alloc_num, disk_inode->indirect);
    free_map_release (disk_inode->indirect, 1);
    buffer_cache_release (disk_inode->indirect);
    /* ??? */
  }

  for (i = 0; i < direct_alloc_num; i++)
  {
    free_map_release (disk_inode->direct[i], 1);
    buffer_cache_release (disk_inode->direct[i]);
  }

  free (disk_inode);
}
#endif

#ifdef SYNRW
void
inode_writer_lock_acquire (struct inode *inode)
{
  lock_acquire (&inode->writer_lock);
}

void
inode_mutex_acquire (struct inode *inode)
{
  lock_acquire (&inode->mutex);
}

void
inode_writer_lock_release (struct inode *inode)
{
  lock_release (&inode->writer_lock);
}

void
inode_mutex_release (struct inode *inode)
{
  lock_release (&inode->mutex);
}

uint32_t
inode_readcount (struct inode *inode)
{
  return inode->readcount;
}

void
inode_readcount_pp (struct inode *inode)
{
  inode->readcount++;
}

void
inode_readcount_mm (struct inode *inode)
{
  inode->readcount--;
}
#endif
