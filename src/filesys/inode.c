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
#ifdef PRJ4
#define IS_DIRECTORY(INFO) (INFO & 0x00000001)
#define GET_LEVEL(INFO) (INFO >> 1)
#define SET_LEVEL(INFO, LEVEL) (INFO | (LEVEL << 1))
#endif

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
#ifndef PRJ4
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
#ifndef PRJ4
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
#ifdef PRJ4
  buffer_cache_init ();
#endif
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
#ifdef PRJ4
inode_create (disk_sector_t sector, off_t length, uint32_t info)
{
  size_t sectors = bytes_to_sectors (length);
  return allocate_inode_disk (sectors, sector, length, 0, info, sector, sectors);
}
#else
inode_create (disk_sector_t sector, off_t length)
{
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
          disk_write (filesys_disk, sector, disk_inode);
          if (sectors > 0) 
            {
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                disk_write (filesys_disk, disk_inode->start + i, zeros); 
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}
#endif

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
#ifdef PRJ4
  buffer_cache_read (inode->sector, &inode->data, DISK_SECTOR_SIZE, 0);
#else
  disk_read (filesys_disk, inode->sector, &inode->data);
#endif
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

#ifdef PRJ4
uint32_t
inode_get_info (struct inode *inode)
{
  return inode->data.info;
}

bool
inode_is_directory (struct inode *inode)
{
  return IS_DIRECTORY (inode->data.info);
}

uint32_t
inode_get_level (struct inode *inode)
{
  return GET_LEVEL (inode->data.info);
}

/* 새로 만든 info를 리턴함 알아서 딴데서 써야됨 */
uint32_t
inode_set_level (uint32_t old_info, uint32_t new_level)
{
  return SET_LEVEL (old_info, new_level);
}
#endif

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
#ifndef PRJ4
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

  uint32_t length = inode->data.length;
  size = length - offset > size ? size : length - offset;
#ifdef PRJ4
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
  int sector_ofs = offset % DISK_SECTOR_SIZE;
#else
  uint8_t *bounce = NULL;
#endif

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
#ifdef PRJ4
      disk_sector_t sector_idx = refer_inode_disk.direct [direct_idx];
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
      /* zero bytes를 비워줄 수도 있다. */
      sector_ofs = 0;

      /* Advance. */
      size -= read_bytes;
      bytes_read += read_bytes;
      direct_idx++;
      if (direct_idx >= DIRECT_NO)
      {
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
        direct_idx = 0;
      }
#else
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Read full sector directly into caller's buffer. */
          disk_read (filesys_disk, sector_idx, buffer + bytes_read); 
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          disk_read (filesys_disk, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

       /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
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
#ifdef PRJ4
  uint32_t mok, info;
  info = inode_get_info (inode);
  struct inode_disk refer_inode_disk;
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
  if (offset >= inode->data.length && size > 0)
  {
    /* finding refer_previous and start_direct_idx */
    int start_direct_idx = DIV_ROUND_UP (inode->data.length, DISK_SECTOR_SIZE);
    disk_sector_t refer_previous_sec_no = inode->data.sector;
    while (start_direct_idx >= DIRECT_NO)
    {
      start_direct_idx -= DIRECT_NO;
      if (refer_inode_disk.indirect)
      {
        refer_previous_sec_no = refer_inode_disk.indirect;
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
      }
    }

    /* 처음 쓰는 거면 새로 refer_previous의 indirect 는 아직
     * 할당이 안된 상태므로 해준다 */
    if (start_direct_idx == 0 && inode->data.length > 0)
    {
      if (!free_map_allocate (1, &refer_previous_sec_no))
        return -1;
      refer_inode_disk.indirect = refer_previous_sec_no;
      buffer_cache_write (refer_inode_disk.sector,\
          &refer_inode_disk, DISK_SECTOR_SIZE, 0);
    }

    /* 총 필요한 direct 갯수 */
    uint32_t add_sector = bytes_to_sectors (offset + size) - \
      bytes_to_sectors (inode->data.length);

    if (add_sector > 0)
      if (!allocate_inode_disk (add_sector, refer_previous_sec_no, \
        offset + size, start_direct_idx, info, refer_previous_sec_no, add_sector))
      {
        free_map_release (refer_previous_sec_no, 1);
        return -1;
      }

    buffer_cache_read(inode->data.sector, &inode->data, DISK_SECTOR_SIZE, 0);
    inode->data.length = offset + size;
    buffer_cache_write(inode->data.sector, &inode->data, DISK_SECTOR_SIZE, 0);
  }
  memcpy (&refer_inode_disk, &inode->data, sizeof (struct inode_disk));
  uint32_t direct_idx = offset / DISK_SECTOR_SIZE % DIRECT_NO;
  for (mok = offset / (DISK_SECTOR_SIZE * DIRECT_NO);
        mok > 0;
        mok--)
    buffer_cache_read (refer_inode_disk.indirect, &refer_inode_disk,\
        DISK_SECTOR_SIZE, 0);

  int sector_ofs = offset % DISK_SECTOR_SIZE;
#else
  uint8_t *bounce = NULL;
#endif

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
#ifdef PRJ4
      disk_sector_t sector_idx = refer_inode_disk.direct [direct_idx];

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
      direct_idx++;
      if (direct_idx >= DIRECT_NO)
      {
        buffer_cache_read (refer_inode_disk.indirect, \
            &refer_inode_disk, DISK_SECTOR_SIZE, 0);
        direct_idx = 0;
      }
#else
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Write full sector directly to disk. */
          disk_write (filesys_disk, sector_idx, buffer + bytes_written); 
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
 
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            disk_read (filesys_disk, sector_idx, bounce);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          disk_write (filesys_disk, sector_idx, bounce); 
        }

       /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
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

#ifdef PRJ4
int
inode_open_cnt (struct inode *inode)
{
  return inode->open_cnt;
}

bool
allocate_inode_disk (uint32_t sectors, disk_sector_t inode_sector, off_t length, int start_direct_idx, uint32_t info, uint32_t origin_sector, uint32_t origin_sectors)
{
  struct inode_disk *disk_inode = NULL;

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL)
  {
    release_inode_disk (origin_sector - sectors, origin_sector);
    return false;
  }
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
    buffer_cache_write (disk_inode->direct[i], zeros, DISK_SECTOR_SIZE, 0);
  }

  if (sectors - i + start_direct_idx > 0)
  {
    disk_sector_t new_indirect_sector;
    free_map_allocate (1, &new_indirect_sector);
    disk_inode->indirect = new_indirect_sector;
    buffer_cache_write (inode_sector, disk_inode, DISK_SECTOR_SIZE, 0);
    free (disk_inode);
    return allocate_inode_disk (sectors - i + start_direct_idx, \
        new_indirect_sector, length, 0, info, origin_sector, origin_sectors);
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

void
print_all_inodes (void)
{
  printf ("total openlist : %d\n", list_size (&open_inodes));
  struct inode * i = NULL;
  struct list_elem *elem_pointer = list_begin (&open_inodes);
  while (elem_pointer != list_end (&open_inodes))
  {
    i = list_entry (elem_pointer, struct inode, elem);
    printf ("inode : %d , open_cnt : %d\n",\
        i->sector, i->open_cnt);
    elem_pointer = list_next (elem_pointer);
  }
}
#endif
