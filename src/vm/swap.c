#include "vm/swap.h"


static struct list frame_table;
static struct bitmap* swap_table;
static struct lock frame_lock;
static struct lock swap_lock;

void swap_table_bitmap_init (void)
{
  struct disk* d = disk_get(1,1);
  disk_sector_t ss = disk_size(d);
  ss /= 8;
  swap_table = bitmap_create(ss);
  lock_init (&swap_lock);
}

void
swap_table_bitmap_set (size_t idx, bool toset)
{
  lock_acquire (&swap_lock);
  bitmap_set(swap_table, idx, toset);
  lock_release (&swap_lock);
}

size_t
swap_table_scan_and_flip (void)
{
  size_t ans;
  lock_acquire (&swap_lock);
  ans = bitmap_scan_and_flip (swap_table, 0, 1, false);
  return ans;
}

void
frame_table_init (void)
{
  lock_init (&frame_lock);
  list_init (&frame_table);
}

void
frame_table_push_back (struct frame_elem* e)
{
  lock_acquire (&frame_lock);
  list_push_back (&frame_table, e);
  lock_release (&frame_lock);
}

struct frame_elem*
frame_table_find_victim (void)
{
  lock_acquire (&frame_lock);
  struct frame_elem* totest;
  struct list_elem* elem_pointer;
  size_t next_size, prev_size = frame_table_size();
  if (list_empty (&frame_table))
  {
    lock_release (&frame_lock);
    return NULL;
  }

  elem_pointer = list_pop_front(&frame_table);
  totest = list_entry (elem_pointer, struct frame_elem, elem);
  while(pagedir_is_accessed (totest->pd, totest->vaddr) ||
      pagedir_is_stack (totest->pd, totest->vaddr))
  {
    pagedir_set_accessed (totest->pd, totest->vaddr, false);
    list_push_back (&frame_table, elem_pointer);
    elem_pointer = list_pop_front (&frame_table);
    totest = list_entry (elem_pointer, struct frame_elem, elem);
    //printf("totest : %p, totest->pd : %p, totest->vaddr : %p...\n",\
        totest, totest->pd, totest->vaddr);
  }
  next_size = frame_table_size();
  ASSERT (is_kernel_vaddr(totest));
  ASSERT (is_user_vaddr(totest->vaddr));
  ASSERT (next_size == prev_size - 1);
  ASSERT (totest->pd_thread != NULL);

  lock_release (&frame_lock);
  return totest;
}

void
frame_table_delete (uint32_t pd)
{
  struct frame_elem* i;
  lock_acquire (&frame_lock);
  struct list_elem* elem_pointer = list_begin(&frame_table);
  while (elem_pointer != list_end(&frame_table))
  {
    i = list_entry(elem_pointer , struct frame_elem, elem);
    if (i->pd == pd)
    {
      elem_pointer = list_remove (elem_pointer);
      free (i);
    }
    else
      elem_pointer = list_next(elem_pointer);
  }
  lock_release (&frame_lock);
}

void
frame_elem_delete (void* target_addr, uint32_t* target_pd)
{
  struct frame_elem* i;
  lock_acquire (&frame_lock);
  struct list_elem* elem_pointer = list_begin(&frame_table);
  while (elem_pointer != list_end(&frame_table))
  {
    i = list_entry(elem_pointer , struct frame_elem, elem);
    if (i->vaddr == target_addr && i->pd == target_pd)
    {
      elem_pointer = list_remove (elem_pointer);
      palloc_free_page(pagedir_get_page (i->pd, i->vaddr));
      pagedir_clear_page(i->pd, i->vaddr);
      free (i);
    }
    else
      elem_pointer = list_next(elem_pointer);
  }
  lock_release (&frame_lock);
}

size_t
frame_table_size (void)
{
  return list_size (&frame_table);
}

void
frame_lock_try_release (struct thread* t)
{
  if (frame_lock.holder == t)
    lock_release (&frame_lock);
}

void
swap_lock_release (void)
{
  lock_release (&swap_lock);
}

void
swap_lock_try_release (struct thread* t)
{
  if (swap_lock.holder == t)
    lock_release (&swap_lock);
}
