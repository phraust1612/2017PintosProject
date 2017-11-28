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

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
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
  buffer_cache_init ();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
#ifdef EXTENSE_DEBUG
  size_t sectors = bytes_to_sectors (length);
  return jeonduhwan (sectors, sector, length, 0);
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

bool
inode_is_dir (struct inode *inode)
{
  return inode->data.info == 1;
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
#ifdef EXTENSE_DEBUG
  if (offset >= inode->data.length) return 0;
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

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
#ifndef EXTENSE_DEBUG
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
#else
      disk_sector_t sector_idx = refer_inode_disk.direct [direct_idx];
#ifdef INODE_PRINT
      printf ("inode_read_at - direct_idx : %d, sector_idx : %d, "\
          "indirect : %d, inode_disk sector : %d...\n",\
          direct_idx, sector_idx, refer_inode_disk.indirect,\
          refer_inode_disk->sector);
#endif
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

      buffer_cache_read (sector_idx, buffer + bytes_read, read_bytes, sector_ofs);
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
#ifdef INODE_PRINT
      printf ("inode_read_at - direct_idx : %d, sector_idx : %d, "\
          "indirect : %d, inode_disk sector : %d...\n",\
          direct_idx, refer_inode_disk.direct[direct_idx], \
          refer_inode_disk.indirect,\
          refer_inode_disk->sector);
#endif
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
  uint32_t mok;
  struct inode_disk refer_inode_disk;
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
  if (offset >= inode->data.length && size > 0 && \
      inode->data.length != 0)
  {
    for (mok = inode->data.length / (DISK_SECTOR_SIZE * DIRECT_NO);
        mok >1; mok--)
      buffer_cache_read (refer_inode_disk.indirect, &refer_inode_disk,\
        DISK_SECTOR_SIZE, 0);
    disk_sector_t final_sector_no = refer_inode_disk.indirect;
    int add_direct_idx = inode->data.length /\
                         DISK_SECTOR_SIZE * DIRECT_NO;
    if (add_direct_idx == 0)
      free_map_allocate (1, &final_sector_no);

    uint32_t add_sector_no = bytes_to_sectors (offset + size) - \
      bytes_to_sectors (inode->data.length);

    inode->data.length = offset + size;
    jeonduhwan (add_sector_no, final_sector_no, \
        offset + size, add_direct_idx);
  }
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
#ifdef INODE_PRINT
      printf ("inode_write_at - direct_idx : %d, sector_idx : %d, "\
          "indirect : %d, inode_disk sector : %d...\n",\
          direct_idx, sector_idx, refer_inode_disk.indirect,\
          refer_inode_disk->sector);
#endif
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


#ifdef EXTENSE_DEBUG
bool
jeonduhwan (uint32_t sectors, disk_sector_t inode_sector, off_t length, int add_direct_idx)
{
#ifdef INODE_PRINT
  printf ("jeonduhwan - no : %d, inode_disk sector : %d, "\
      "start : %d\n", sectors, inode_sector, add_direct_idx);
#endif
  struct inode_disk *disk_inode = NULL;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return false;

  disk_inode->length = length;
  disk_inode->sector = inode_sector;
  disk_inode->info = 0;
  disk_inode->magic = INODE_MAGIC;
  int direct_alloc_num = sectors > DIRECT_NO ? DIRECT_NO : sectors;
  int i;
  for (i = add_direct_idx; i < direct_alloc_num; i++)
  {
    free_map_allocate (1, &disk_inode->direct[i]);
    buffer_cache_write (disk_inode->direct[i], zeros, DISK_SECTOR_SIZE, 0);
  }

  if (sectors - direct_alloc_num > 0)
  {
    disk_sector_t new_indirect_sector;
    free_map_allocate (1, &new_indirect_sector);
    disk_inode->indirect = new_indirect_sector;
    buffer_cache_write (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);
    free (disk_inode);
    return jeonduhwan (sectors - direct_alloc_num, new_indirect_sector, length, 0);
  }
  else
  {
#ifdef INODE_PRINT
    printf ("jeonduhwan - direct[0] : %d, direct[1] : %d\n",\
        disk_inode->direct[0], disk_inode->direct[1]);
#endif
    buffer_cache_write (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);
    free (disk_inode);
  }

  return true;
}

void
release_inode_disk (uint32_t sectors, disk_sector_t inode_sector)
{
#ifdef INODE_PRINT
  printf ("release_inode_disk - no : %d, inode_disk sec : %d\n",\
      sectors, inode_sector);
#endif
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
#ifdef INODE_PRINT
    printf ("recursively call...\n");
#endif
    release_inode_disk (sectors - direct_alloc_num, disk_inode->indirect);
    free_map_release (disk_inode->indirect, 1);
    buffer_cache_release (disk_inode->indirect);
  }

  for (i = 0; i < direct_alloc_num; i++)
  {
#ifdef INODE_PRINT
    printf ("release direct i : %d, inode_disk sec : %d\n",\
        i, inode_sector);
#endif
    free_map_release (disk_inode->direct[i], 1);
    buffer_cache_release (disk_inode->direct[i]);
  }

  free (disk_inode);
}
#endif
