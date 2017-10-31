#ifndef __VM_PAGE_H
#define __VM_PAGE_H
#include "threads/thread.h"
#include "lib/stdint.h"
#include "threads/vaddr.h"

#ifndef __LIB_KERNEL_HASH_H
#include "lib/kernel/hash.h"
#endif

struct page
{
  struct hash_elem elem;
  uint32_t load_vaddr;  /* user virtual addr */
  struct file* f;
  uint32_t load_filepos;
  uint32_t load_read_bytes;
  uint32_t load_zero_bytes;
  bool writable;
  bool swap_outed;  /* if true, find this from swap disk */
  bool mmaped;
  uint32_t swap_index;
};

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct page * page_lookup (const void *address, struct thread* tcurrent);
bool page_swap_out_index (const void *address, struct thread* tcurrent, bool new_swap_outed, uint32_t new_index);
void remove_page (struct hash_elem* target_elem, void *aux UNUSED);
void set_new_dirty_page (void* new_esp, struct thread* t);

#endif
