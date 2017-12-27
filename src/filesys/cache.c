#include "filesys/cache.h"
#ifdef PRJ4

struct file_cache
{
  bool allocated;
  uint32_t sector_no;
  bool accessed;
  bool dirty;
  struct lock buffer_lock;
  uint8_t data[DISK_SECTOR_SIZE];
};

static struct file_cache buffer_cache[BUFFER_CACHE_SIZE];
static uint32_t lookup_start_index;

void
buffer_cache_init (void)
{
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    buffer_cache[i].sector_no = 0;
    buffer_cache[i].allocated = false;
    lock_init (&buffer_cache[i].buffer_lock);
  }
  lookup_start_index = 0;
}

bool
buffer_cache_release (disk_sector_t sec_no)
{
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (buffer_cache[i].sector_no == sec_no && buffer_cache[i].allocated)
    {
      lock_acquire (&buffer_cache[i].buffer_lock);
      if (buffer_cache[i].dirty)
        disk_write (filesys_disk, buffer_cache[i].sector_no, &buffer_cache[i].data);
      buffer_cache[i].allocated = false;
      lock_release (&buffer_cache[i].buffer_lock);
      return true;
    }
  }
  return false;
}

/* return the index of new victim from buffer_cache */
uint32_t
buffer_cache_find_victim (void)
{
  uint32_t ans, i;
  // 먼저 빈 캐시가 있는지부터 찾고 있으면 그 인덱스를 리턴
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (!buffer_cache[i].allocated)
    {
      ans = i;
      return ans;
    }
  }

  i = lookup_start_index;
  while (true)
  {
    if (buffer_cache[i].accessed)
    {
      lock_acquire (&buffer_cache[i].buffer_lock);
      buffer_cache[i].accessed = false;
      lock_release (&buffer_cache[i].buffer_lock);
      i = (i + 1) % BUFFER_CACHE_SIZE;
    }
    else
    {
      lookup_start_index = (i + 1) % BUFFER_CACHE_SIZE;
      ans = i;
      lock_acquire (&buffer_cache[i].buffer_lock);
      // swapping out to file disk
      if (buffer_cache[i].dirty)
        disk_write (filesys_disk, buffer_cache[i].sector_no, &buffer_cache[i].data);
      buffer_cache[ans].allocated = false;
      lock_release (&buffer_cache[i].buffer_lock);
      break;
    }
  }

  return ans;
}

void
buffer_cache_read (disk_sector_t sec_no, void *buffer, off_t size, off_t offset)
{
  uint32_t i;
  int ans = -1;
  // buffer_cache에 먼저 불러온 것이 있는지 검사
  // 있으면 걍 그거로부터 읽음
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (buffer_cache[i].sector_no == sec_no && buffer_cache[i].allocated)
    {
      ans = i;
      break;
    }
  }

  if (ans < 0)
  {
    ans = buffer_cache_find_victim ();
    lock_acquire (&buffer_cache[ans].buffer_lock);
    disk_read (filesys_disk, sec_no, &buffer_cache[ans].data);
    buffer_cache[ans].allocated = true;
    buffer_cache[ans].accessed = true;
    buffer_cache[ans].sector_no = sec_no;
    lock_release (&buffer_cache[ans].buffer_lock);
  }

  memcpy (buffer, (uint8_t*) &buffer_cache[ans].data + offset, size);
}

void
buffer_cache_write (disk_sector_t sec_no, void *buffer, off_t size, off_t offset)
{
  uint32_t i;
  int ans = -1;
  // buffer_cache에 먼저 불러온 것이 있는지 검사
  // 있으면 걍 그거로부터 읽음
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (buffer_cache[i].sector_no == sec_no && buffer_cache[i].allocated)
    {
      ans = i;
      break;
    }
  }

  if (ans < 0)
  {
    ans = buffer_cache_find_victim ();
    lock_acquire (&buffer_cache[ans].buffer_lock);
    disk_read (filesys_disk, sec_no, &buffer_cache[ans].data);
    buffer_cache[ans].allocated = true;
    buffer_cache[ans].accessed = true;
    buffer_cache[ans].sector_no = sec_no;
    lock_release (&buffer_cache[ans].buffer_lock);
  }

  memcpy ((uint8_t*) &buffer_cache[ans].data + offset, buffer, size);
  buffer_cache[ans].dirty = true;
}

void
buffer_cache_write_back (void)
{
  int i;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
    if (buffer_cache[i].allocated && buffer_cache[i].dirty)
    {
      lock_acquire (&buffer_cache[i].buffer_lock);
      disk_write (filesys_disk, buffer_cache[i].sector_no, &buffer_cache[i].data);
      lock_release (&buffer_cache[i].buffer_lock);
    }
}
#endif
