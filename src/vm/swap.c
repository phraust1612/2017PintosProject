#include "vm/swap.h"

struct swappp
{
  struct bitmap *bb;
};

static struct list frame_table;
static struct swappp swap_table;
static struct lock frame_lock;

void swap_table_bitmap_init (void)
{
  struct disk* d = disk_get(1,1);
  disk_sector_t ss = disk_size(d);
  ss /= 8;
  swap_table.bb = bitmap_create(1024);
}

void
swap_table_bitmap_set (size_t idx, bool toset)
{
  bitmap_set(swap_table.bb, idx, toset);
}

size_t
swap_table_scan_and_flip (void)
{
  return bitmap_scan_and_flip (swap_table.bb, 0, 1, false);
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
  for(totest = list_pop_front (&frame_table);
      pagedir_is_accessed (totest->pd, totest->vaddr);
      totest = list_pop_front (&frame_table))
  {
    pagedir_set_accessed (totest->pd, totest->vaddr, false);
    list_push_back (&frame_table, totest);
  }

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
