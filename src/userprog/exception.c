#include "userprog/exception.h"
#include <inttypes.h>
#include <stdio.h>
#include "userprog/gdt.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#define STACK_BASE 0xb8000000

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void) 
{
  /* These exceptions can be raised explicitly by a user program,
     e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
     we set DPL==3, meaning that user programs are allowed to
     invoke them via these instructions. */
  intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
  intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
  intr_register_int (5, 3, INTR_ON, kill,
                     "#BR BOUND Range Exceeded Exception");

  /* These exceptions have DPL==0, preventing user processes from
     invoking them via the INT instruction.  They can still be
     caused indirectly, e.g. #DE can be caused by dividing by
     0.  */
  intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
  intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
  intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
  intr_register_int (7, 0, INTR_ON, kill,
                     "#NM Device Not Available Exception");
  intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
  intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
  intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
  intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
  intr_register_int (19, 0, INTR_ON, kill,
                     "#XF SIMD Floating-Point Exception");

  /* Most exceptions can be handled with interrupts turned on.
     We need to disable interrupts for page faults because the
     fault address is stored in CR2 and needs to be preserved. */
  intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void) 
{
  printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f) 
{
  /* This interrupt is one (probably) caused by a user process.
     For example, the process might have tried to access unmapped
     virtual memory (a page fault).  For now, we simply kill the
     user process.  Later, we'll want to handle page faults in
     the kernel.  Real Unix-like operating systems pass most
     exceptions back to the process via signals, but we don't
     implement them. */
     
  /* The interrupt frame's code segment value tells us where the
     exception originated. */
  switch (f->cs)
    {
#ifndef PRJ3
    case SEL_KCSEG:
      /* Kernel's code segment, which indicates a kernel bug.
         Kernel code shouldn't throw exceptions.  (Page faults
         may cause kernel exceptions--but they shouldn't arrive
         here.)  Panic the kernel to make the point.  */
      intr_dump_frame (f);
      PANIC ("Kernel bug - unexpected interrupt in kernel"); 
    case SEL_UCSEG:
#else
    case SEL_UCSEG:
    case SEL_KCSEG:
#endif
      /* User's code segment, so it's a user exception, as we
         expected.  Kill the user process.  */
#ifdef PRINT_PF
      printf ("%s: dying due to interrupt %#04x (%s).\n", \
              thread_name (), f->vec_no, intr_name (f->vec_no));
      intr_dump_frame (f);
#endif
      printf("%s: exit(%d)\n", thread_current()->name, -1);
      thread_exit (); 

    default:
      /* Some other code segment?  Shouldn't happen.  Panic the
         kernel. */
      printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
             f->vec_no, intr_name (f->vec_no), f->cs);
      thread_exit ();
    }
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to project 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f) 
{
  bool not_present;  /* True: not-present page, false: writing r/o page. */
  bool write;        /* True: access was write, false: access was read. */
  bool user;         /* True: access by user, false: access by kernel. */
  void *fault_addr;  /* Fault address. */

  /* Obtain faulting address, the virtual address that was
     accessed to cause the fault.  It may point to code or to
     data.  It is not necessarily the address of the instruction
     that caused the fault (that's f->eip).
     See [IA32-v2a] "MOV--Move to/from Control Registers" and
     [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
     (#PF)". */
  asm ("movl %%cr2, %0" : "=r" (fault_addr));

  /* Turn interrupts back on (they were only off so that we could
     be assured of reading CR2 before it changed). */
  intr_enable ();

  /* Count page faults. */
  page_fault_cnt++;

  /* Determine cause. */
  not_present = (f->error_code & PF_P) == 0;
  write = (f->error_code & PF_W) != 0;
  user = (f->error_code & PF_U) != 0;

  /* To implement virtual memory, delete the rest of the function
     body, and replace it with code that brings in the page to
     which fault_addr refers. */

#ifndef PRJ3
  printf ("Page fault at %p: %s error %s page in %s context.\n",
           fault_addr,
           not_present ? "not present" : "rights violation",
           write ? "writing" : "reading",
           user ? "user" : "kernel");
  kill (f);
#else
  struct thread* tcurrent = thread_current();
  if (is_user_vaddr (f->esp))
    set_new_dirty_page (f->esp, tcurrent);
  ASSERT (is_user_vaddr (tcurrent->user_esp));

  uint8_t *kpage = palloc_get_page (PAL_USER|PAL_ZERO);
  struct frame_elem* fr_elem;
  size_t swapping_index;
  disk_sector_t i;
  int count;
  if (kpage == NULL)
  {
    // swap out
    struct disk* d;
    swapping_index = swap_table_scan_and_flip();

    fr_elem = frame_table_find_victim();
    ASSERT(fr_elem != NULL);
    struct page* victim_page = page_lookup (fr_elem->vaddr, fr_elem->pd_thread);
    ASSERT (victim_page != NULL)
    uint8_t* victim_kvaddr = pagedir_get_page(fr_elem->pd, fr_elem->vaddr);
    pagedir_clear_page(fr_elem->pd, fr_elem->vaddr);

    if (!victim_page->mmaped)
    {
      d = disk_get(1,1);
      ASSERT(victim_kvaddr);

      ASSERT (page_swap_out_index (fr_elem->vaddr, fr_elem->pd_thread, true, swapping_index));
      count = 0;
      for (i = swapping_index*8; i<swapping_index*8+8; i++)
      {
        disk_write(d, i, victim_kvaddr + count*DISK_SECTOR_SIZE);
        count++;
      }
    }
    else
    {
      // mmap으로 메모리에 로드된건 스왑말고 파일디스크에 바로 돌려줌.
      if (pagedir_is_dirty (fr_elem->pd, fr_elem->vaddr))
      {
        pagedir_set_dirty (fr_elem->pd, fr_elem->vaddr, false);
        ASSERT (file_write_at (victim_page->f, victim_kvaddr, victim_page->load_read_bytes, victim_page->load_filepos) == victim_page->load_read_bytes);
      }
    }

    palloc_free_page(victim_kvaddr);
    free (fr_elem);

    kpage = palloc_get_page (PAL_USER|PAL_ZERO);
    swap_lock_release ();

    if (kpage == NULL) return;
  }

  uint8_t* upage = pg_round_down(fault_addr);
  // tcurrent안의 supplementary_page_table에서 미리
  // 저장된 upage에 대응하는 페이지를 찾는다.
  // (이는 lazy load segment할 때 저장해놓았을 것이다.)
  struct page* faulted_page = page_lookup (upage, tcurrent);
  if (faulted_page == NULL)
  {
    // do stack growth
    bool checkheuristic = (tcurrent->user_esp <= fault_addr \
        || fault_addr == tcurrent->user_esp - 4 \
        || fault_addr == tcurrent->user_esp - 32);
    if (fault_addr >= STACK_BASE
        && checkheuristic
        && is_user_vaddr(fault_addr))
    {
      struct page* pi = malloc (sizeof(struct page));
      if (pi == NULL)
        return false;
      pi->load_vaddr = upage;
      pi->load_filepos = 0;
      pi->load_read_bytes = 0;
      pi->load_zero_bytes = PGSIZE;
      pi->writable = true;
      pi->swap_outed = false;
      pi->swap_index = 0;
      pi->f = NULL;
      pi->mmaped = false;
      supplementary_lock_acquire(tcurrent);
      hash_replace (&tcurrent->supplementary_page_table, &pi->elem);
      supplementary_lock_release(tcurrent);

      fr_elem = malloc (sizeof(struct frame_elem));
      fr_elem->pd = tcurrent->pagedir;
      fr_elem->vaddr = upage;
      fr_elem->pd_thread = tcurrent;
      frame_table_push_back(fr_elem);

      /* Add the page to the process's address space. */
      if (pagedir_get_page (tcurrent->pagedir, upage) != NULL
        || !pagedir_set_page (tcurrent->pagedir, upage, kpage, true)) 
      {
        palloc_free_page (kpage);
        printf("whatthe 4\n");
      }
      return ; 
    }
    else
    {
#ifdef PRINT_PF
      printf ("not found from supplementary page table, \
        \norigin : %p, upage : %p, tcurrent_tid : %d, user_esp : %p...\n", \
        fault_addr, upage, tcurrent->tid, tcurrent->user_esp);
#endif
      kill(f);
      return;
    }
  }

  swap_lock_acquire ();
  struct file* ff = faulted_page->f;
  uint32_t filepos = faulted_page->load_filepos;
  uint32_t read_bytes = faulted_page->load_read_bytes;
  bool writable = faulted_page->writable;
  bool swap_out = faulted_page->swap_outed;
  uint32_t swap_index = faulted_page->swap_index;
  swap_lock_release ();
  if(filepos < 0 || read_bytes < 0)
  {
    return;
  }

  if(not_present)
  {
    // do lazy load
    if (!swap_out)
    {
      if (file_read_at (ff, kpage, read_bytes, filepos) != (int) read_bytes)
      {
        palloc_free_page (kpage);
        printf("%s: exit(%d)\n", thread_current()->name, -1);
        thread_exit (); 
        return ; 
      }

      /* Add the page to the process's address space. */
      if (pagedir_get_page (tcurrent->pagedir, upage) != NULL
        || !pagedir_set_page (tcurrent->pagedir, upage, kpage, writable)) 
      {
        palloc_free_page (kpage);
        printf("whatthe 2\n");
        return ; 
      }
      fr_elem = malloc (sizeof(struct frame_elem));
      fr_elem->pd = tcurrent->pagedir;
      fr_elem->vaddr = upage;
      fr_elem->pd_thread = tcurrent;
      frame_table_push_back(fr_elem);
    }
    else
    {
      // swap in
      struct disk* d = disk_get(1,1);
      count = 0;
      for (i = swap_index*8; i<swap_index*8+8; i++)
      {
        disk_read (d, i, kpage + count*DISK_SECTOR_SIZE);
        count++;
      }

      /* Add the page to the process's address space. */
      if (pagedir_get_page (tcurrent->pagedir, upage) != NULL
        || !pagedir_set_page (tcurrent->pagedir, upage, kpage, writable)) 
      {
        palloc_free_page (kpage);
        printf("whatthe 3\n");
        return ; 
      }

      ASSERT (page_swap_out_index (upage, tcurrent, false, 0));
      swap_table_bitmap_set (swap_index, false);

      fr_elem = malloc (sizeof(struct frame_elem));
      fr_elem->pd = tcurrent->pagedir;
      fr_elem->vaddr = upage;
      fr_elem->pd_thread = tcurrent;
      frame_table_push_back(fr_elem);
    }
  }
  else
  {
#ifdef PRINT_PF
    printf ("Page fault at %p: %s error %s page in %s context.\n",
          fault_addr,
          not_present ? "not present" : "rights violation",
          write ? "writing" : "reading",
          user ? "user" : "kernel");
#endif
    kill (f);
  }
#endif
}

