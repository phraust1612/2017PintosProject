#include "filesys/cache.h"

struct file_cache
{
  bool allocated;
  uint32_t sector_no;
  bool accessed;
  bool dirty;
  uint8_t data[DISK_SECTOR_SIZE];
};

static struct file_cache buffer_cache[BUFFER_CACHE_SIZE];
static struct lock buffer_cache_lock;
static uint32_t lookup_start_index;

void
buffer_cache_init (void)
{
  lock_init (&buffer_cache_lock);
  uint32_t i = 0;
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    buffer_cache[i].sector_no = 0;
    buffer_cache[i].allocated = false;
  }
  lookup_start_index = 0;
}

struct file_cache *
buffer_cache_release (disk_sector_t sec_no)
{
  uint32_t i = 0;
  struct file_cache* ans;
  lock_acquire (&buffer_cache_lock);
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (buffer_cache[i].sector_no == sec_no && buffer_cache[i].allocated)
    {
      ans = &buffer_cache[i];
      ans->allocated = false;
      lock_release (&buffer_cache_lock);
      return ans;
    }
  }
  ans = NULL;
  lock_release (&buffer_cache_lock);
  return ans;
}

/* return the index of new victim from buffer_cache */
uint32_t
buffer_cache_find_victim (void)
{
  uint32_t ans, i;
  lock_acquire (&buffer_cache_lock);
  // 먼저 빈 캐시가 있는지부터 찾고 있으면 그 인덱스를 리턴
  for (i = 0; i < BUFFER_CACHE_SIZE; i++)
  {
    if (!buffer_cache[i].allocated)
    {
      ans = i;
      lock_release (&buffer_cache_lock);
      return ans;
    }
  }

  i = lookup_start_index;
  while (true)
  {
    if (buffer_cache[i].accessed)
    {
      buffer_cache[i].accessed = false;
      i = (i + 1) % BUFFER_CACHE_SIZE;
    }
    else
    {
      lookup_start_index = (i + 1) % BUFFER_CACHE_SIZE;
      ans = i;
      // swapping out to file disk
      if (buffer_cache[i].dirty)
        disk_write (filesys_disk, buffer_cache[i].sector_no, &buffer_cache[i].data);
      buffer_cache[ans].allocated = false;
      break;
    }
  }

  lock_release (&buffer_cache_lock);
  return ans;
}

void
buffer_cache_read (disk_sector_t sec_no, void *buffer, off_t size, off_t offset)
{
  uint32_t i;
  int ans = -1;
  lock_acquire (&buffer_cache_lock);
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
    lock_release (&buffer_cache_lock);
    ans = buffer_cache_find_victim ();
    lock_acquire (&buffer_cache_lock);
    disk_read (filesys_disk, sec_no, &buffer_cache[ans].data);
    buffer_cache[ans].allocated = true;
    buffer_cache[ans].accessed = true;
    buffer_cache[ans].sector_no = sec_no;
  }

  memcpy (buffer, (uint8_t*) &buffer_cache[ans].data + offset, size);
  lock_release (&buffer_cache_lock);
}

void
buffer_cache_write (disk_sector_t sec_no, void *buffer, off_t size, off_t offset)
{
  uint32_t i;
  int ans = -1;
  lock_acquire (&buffer_cache_lock);
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
    lock_release (&buffer_cache_lock);
    ans = buffer_cache_find_victim ();
    lock_acquire (&buffer_cache_lock);
    disk_read (filesys_disk, sec_no, &buffer_cache[ans].data);
    buffer_cache[ans].allocated = true;
    buffer_cache[ans].accessed = true;
    buffer_cache[ans].sector_no = sec_no;
  }

  memcpy ((uint8_t*) &buffer_cache[ans].data + offset, buffer, size);
  buffer_cache[ans].dirty = true;
  lock_release (&buffer_cache_lock);
}

