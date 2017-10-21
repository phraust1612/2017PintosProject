#ifndef __VM_SWAP_H
#define __VM_SWAP_H
#include "lib/kernel/bitmap.h"
#include "lib/kernel/list.h"
#include "devices/disk.h"
#include "threads/pte.h"
#include "threads/synch.h"

struct frame_elem
{
  struct list_elem elem;
  uint32_t *pd;
  void* vaddr; /* corresponding page pointer */
};

void swap_table_bitmap_init (void);
void swap_table_bitmap_set (size_t idx, bool toset);
size_t swap_table_scan_and_flip (void);

void frame_table_init (void);
void frame_table_push_back (struct frame_elem* e);
struct frame_elem* frame_table_find_victim (void);
void frame_table_delete (uint32_t pd);

#endif
