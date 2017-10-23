#include "vm/page.h"

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, elem);
  return hash_bytes (&p->load_vaddr, sizeof p->load_vaddr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
    void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, elem);
  const struct page *b = hash_entry (b_, struct page, elem);
  return a->load_vaddr < b->load_vaddr;
}

/* Returns the page containing the given virtual address,
 * or a null pointer if no such page exists. */
struct page *
page_lookup (const void *address, struct thread* tcurrent)
{
  ASSERT (tcurrent != NULL);
  supplementary_lock_acquire(tcurrent);
  struct page *p = malloc (sizeof(struct page));
  struct hash_elem *e;
  if (p == NULL) return NULL;
  p->load_vaddr = pg_round_down(address);
  e = hash_find (&tcurrent->supplementary_page_table, &p->elem);
  struct page* ans = e != NULL ?  hash_entry (e, struct page, elem) : NULL;
  supplementary_lock_release(tcurrent);
  free(p);
  return ans;
}

bool
page_swap_out_index (const void *address, struct thread* tcurrent, bool new_swap_outed, uint32_t new_index)
{
  ASSERT (tcurrent != NULL);
  ASSERT (!hash_empty (&tcurrent->supplementary_page_table));
  supplementary_lock_acquire(tcurrent);
  struct page *p = malloc (sizeof(struct page));
  struct hash_elem *e;
  if (p == NULL) return false;
  p->load_vaddr = pg_round_down(address);
  e = hash_find (&tcurrent->supplementary_page_table, &p->elem);
  struct page* prev_p = e != NULL ?  hash_entry (e, struct page, elem) : NULL;
  if (prev_p)
  {
    p->load_filepos = prev_p->load_filepos;
    p->load_read_bytes = prev_p->load_read_bytes;
    p->load_zero_bytes = prev_p->load_zero_bytes;
    p->writable = prev_p->writable;
    p->swap_outed = new_swap_outed;
    p->swap_index = new_index;
    hash_replace (&tcurrent->supplementary_page_table, &p->elem);
    free (prev_p);
    supplementary_lock_release(tcurrent);
    return true;;
  }
  else
  {
    free(p);
    supplementary_lock_release(tcurrent);
    return false;
  }
}

void remove_page (struct hash_elem* target_elem, void *aux UNUSED)
{
  struct page* target = hash_entry (target_elem, struct page, elem);
  free(target);
}

int find_filepos(uint8_t* upage, struct thread* tcurrent)
{
  /*
  int i;
  for (i=0; i<tcurrent->load_max_count; i++)
  {
    if (tcurrent->load_start_vaddr[i] <= upage &&
        upage < tcurrent->load_start_vaddr[i] + tcurrent->load_read_bytes[i] + tcurrent->load_zero_bytes[i])
      return upage - tcurrent->load_start_vaddr[i] + tcurrent->load_start_filepos[i];

  }*/
  return -1;
}

int find_read_bytes(uint8_t* upage, struct thread* tcurrent)
{
  /*
  int i;
  uint32_t end_point;
  for (i=0; i<tcurrent->load_max_count; i++)
  {
    end_point = tcurrent->load_start_vaddr[i] + tcurrent->load_read_bytes[i] + tcurrent->load_zero_bytes[i];
    if (tcurrent->load_start_vaddr[i] <= upage &&
        upage < end_point)
    {
      if (upage >= end_point - PGSIZE) return PGSIZE;
      else return PGSIZE - tcurrent->load_zero_bytes[i];
    }

  }*/
  return -1;
}

