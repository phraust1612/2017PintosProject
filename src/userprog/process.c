#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (PAL_ZERO);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
#ifdef USERPROG
  // file_name을 직접 strtok_r하면 코드섹션에서 왔을 수도
  // 있는 *file_name을 건드릴 수 있다.
  // 따라서 한 번 더 페이지를 할당해서 복사해서 쓴다.
  char *fn_copy2;
  fn_copy2 = palloc_get_page (PAL_ZERO);
  if (fn_copy2 == NULL)
  {
    palloc_free_page(fn_copy);
    return TID_ERROR;
  }
  strlcpy (fn_copy2, file_name, PGSIZE);

  char dummy[200];
  const char* delim = " ";
  file_name = strtok_r((char*) fn_copy2, delim, dummy);

  struct thread* tcurrent = thread_current();
  tcurrent->child_success = false;
  lock_acquire (&tcurrent->child_list_lock);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  sema_down(&tcurrent->creation_sema);
  palloc_free_page (fn_copy);
  palloc_free_page (fn_copy2);
 
  if (!tcurrent->child_success || tid == TID_ERROR)
  {
    lock_release (&tcurrent->child_list_lock);
    return -1;
  }
 
  // c_elem 를 여기서 지역변수로 선언하면 struct thread* 도중의 커널 스택 영역에 할당될 수 있어
  // 스택이 지나가면서 훼손될 우려가 있다. 따라서 별도의 페이지를 할당해서 만들어줘야 한다.
  struct child_elem* c_elem;
  c_elem = malloc (sizeof (struct child_elem));
  if (!c_elem)
  {
    lock_release (&tcurrent->child_list_lock);
    return -1;
  }

  if (!tcurrent->ttmpchild) c_elem->tchild = NULL;
  else
  {
    c_elem->tchild = malloc (sizeof (struct thread*));
    if (!c_elem->tchild)
    {
      lock_release (&tcurrent->child_list_lock);
      free (c_elem);
      return -1;
    }
    memcpy (&c_elem->tchild, &tcurrent->ttmpchild, sizeof (struct thread*));
    tcurrent->ttmpchild = NULL;
    ASSERT (c_elem->tchild->magic == 0xcd6abf4b);
  }
  c_elem->child_tid = tid;
  c_elem->exit_status = -1;
  sema_init(&c_elem->semaphore, 0);
  list_push_back(&tcurrent->child_list, &c_elem->elem);
  lock_release (&tcurrent->child_list_lock);

#else
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
#endif
  return tid;
}

/* A thread function that loads a user process and makes it start
   running. */
static void
start_process (void *f_name)
{
  char *file_name = f_name;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);
#ifdef USERPROG
  struct thread* tcurrent = thread_current();
  tcurrent->tparent->child_success = success;
  tcurrent->tparent->ttmpchild = tcurrent;
  sema_up(&tcurrent->tparent->creation_sema);
#else
  palloc_free_page (file_name);
#endif

  /* If load failed, quit. */
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
#ifdef USERPROG
  struct list_elem* elem_pointer = NULL;
  struct child_elem* i = NULL;
  int ans;
  struct thread* tcurrent = thread_current();

  lock_acquire(&tcurrent->child_list_lock);
  elem_pointer = list_begin(&(tcurrent->child_list));
  while (elem_pointer != list_end(&(tcurrent->child_list)))
  {
    i = list_entry(elem_pointer , struct child_elem, elem);
    elem_pointer = list_next (elem_pointer);
    if (i->child_tid == child_tid)
    {
      lock_release(&tcurrent->child_list_lock);
      sema_down(&i->semaphore);
      lock_acquire (&tcurrent->child_list_lock);
      ans = i->exit_status;
      // if (i->tchild != NULL) free (i->tchild);
      list_remove (&i->elem);
      free (i);
      lock_release(&tcurrent->child_list_lock);
      return ans;
    }
  }

  lock_release(&tcurrent->child_list_lock);
#endif
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
#ifdef USERPROG
  process_kill (thread_current ());
#else
  struct thread *tcurrent = thread_current ();
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  uint32_t *pd = tcurrent->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      tcurrent->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
#endif
}

#ifdef USERPROG
void
process_kill (struct thread *ttarget)
{
  /* ttarget이 망가졌으면 이미 죽은 자식이므로 바로 리턴하여
   * 그 부모가 더 기다리지 않게 한다 */
  if (!ttarget || ttarget->magic != 0xcd6abf4b) return;

  uint32_t *pd = ttarget->pagedir;
  struct semaphore *child_sema = NULL;
  struct list_elem* elem_pointer = NULL;
  struct child_elem* i = NULL;
  struct file_elem* fi =NULL;

  // 부모가 wait하고 있는 세마를 미리 찾아놓았다가 마지막에 sema_up
  if(ttarget->tparent->tid != ttarget->tid)
  {
    lock_acquire(&ttarget->tparent->child_list_lock);
    elem_pointer = list_begin(&ttarget->tparent->child_list);
    while (elem_pointer != list_end(&ttarget->tparent->child_list))
    {
      i = list_entry(elem_pointer , struct child_elem, elem);
      elem_pointer = list_next (elem_pointer);
      if (i->child_tid == ttarget->tid)
        child_sema = &i->semaphore;
    }
    lock_release(&ttarget->tparent->child_list_lock);
  }

  // 부모가 죽기전에 자식이 있다면 먼저 다 죽이거나 기다려야함
  lock_acquire(&ttarget->child_list_lock);
  elem_pointer = list_begin(&ttarget->child_list);
  while (elem_pointer != list_end(&ttarget->child_list))
  {
    i = list_entry(elem_pointer , struct child_elem, elem);
    elem_pointer = list_next (elem_pointer);
    lock_release(&ttarget->child_list_lock);
    process_kill (i->tchild);
    sema_down (&i->semaphore);
    lock_acquire(&ttarget->child_list_lock);
    // free (i->tchild);
    list_remove (&i->elem);
    free (i);
  }
  lock_release(&ttarget->child_list_lock);
  lock_release_all (ttarget);

  // struct thread의 file_list를 deallocate
  lock_acquire (&ttarget->file_list_lock);
  elem_pointer = list_begin(&(ttarget->file_list));
  while (elem_pointer != list_end(&(ttarget->file_list)))
  {
    fi = list_entry(elem_pointer , struct file_elem, elem);
    file_close(fi->f);
    elem_pointer = list_remove (elem_pointer);
    free (fi);
  }
  lock_release (&ttarget->file_list_lock);

#ifdef PRJ3
  /* mmap list 지움 */
  struct mmap_elem* mi = NULL;
  elem_pointer = list_begin (&ttarget->mmap_list);
  while (elem_pointer != list_end (&ttarget->mmap_list))
  {
    mi = list_entry (elem_pointer, struct mmap_elem, elem);
    elem_pointer = list_next (elem_pointer);
    munmap_list (mi->mid);
  }
#endif

  file_close (ttarget->exec_file);

#ifdef PRJ3
  // frame_table에서 이 프로세스에 해당하는 frame_elem deallocate
  frame_table_delete (ttarget->pagedir);

  // supplementary page table이 더 늦게 삭제되어야 한다.
  supplementary_lock_acquire(ttarget);
  if (!hash_empty (&ttarget->supplementary_page_table))
    hash_destroy(&ttarget->supplementary_page_table, (hash_action_func*) remove_page);
  supplementary_lock_release(ttarget);
#endif

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      ttarget->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  if (child_sema) sema_up (child_sema);
}
#endif

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

#ifdef USERPROG
static bool setup_stack (void **esp, char* arg);
#else
static bool setup_stack (void **esp);
#endif
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

#ifdef USERPROG
  char dummy[200];
  const char* delim = " ";
  char* fn_copy; 

  fn_copy = palloc_get_page (PAL_ZERO);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  file_name = strtok_r((char*) file_name, delim, dummy);
#endif
  /* Open executable file. */
  file = filesys_open (file_name);

  if (file == NULL) 
      goto done; 

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      goto done; 
    }

#ifdef PRJ3
  supplementary_lock_acquire(t);
  hash_init (&t->supplementary_page_table, page_hash, page_less, NULL);
  supplementary_lock_release(t);
#endif

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;

      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
      {
        goto done;
      }
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }

              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
              {
                goto done;
              }
            }
          else{
            goto done;
          }
          break;
        }
    }


  /* Set up stack. */
#ifdef USERPROG
  if (!setup_stack (esp, fn_copy))
#else
  if (!setup_stack (esp))
#endif
    goto done;

  palloc_free_page(fn_copy);
#ifdef USERPROG
  t->exec_file = file_reopen (file);
  file_deny_write(t->exec_file);
#endif
  success = true;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

 done:
  /* We arrive here whether the load is successful or not. */
  // file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

#ifdef PRJ3
  struct thread* tcurrent = thread_current();
#else
  file_seek (file, ofs);
#endif
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Do calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

#ifdef PRJ3
      struct page* pi = malloc (sizeof(struct page));
      if (pi == NULL)
        return false;
      pi->load_vaddr = upage;
      pi->load_filepos = ofs;
      pi->load_read_bytes = page_read_bytes;
      pi->load_zero_bytes = page_zero_bytes;
      pi->writable = writable;
      pi->swap_outed = false;
      pi->swap_index = 0;
      pi->f = file;
      pi->mmaped = false;
      supplementary_lock_acquire(tcurrent);
      hash_replace (&tcurrent->supplementary_page_table, &pi->elem);
      supplementary_lock_release(tcurrent);

      ofs += page_read_bytes;
#else
      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }
#endif

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
#ifdef USERPROG
setup_stack (void **esp, char* arg) 
#else
setup_stack (void **esp)
#endif
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
#ifdef PRJ3
  if (kpage == NULL)
  {
    // stack page할당이 실패하면 swap out해야함
    disk_sector_t i;
    int count = 0;
    struct disk* d = disk_get(1,1);
    size_t swapping_index = swap_table_scan_and_flip();
    struct frame_elem* victim_frame = frame_table_find_victim();
    ASSERT(victim_frame != NULL);
  
    uint8_t* victim_kvaddr = pagedir_get_page(victim_frame->pd, victim_frame->vaddr);
    ASSERT(is_kernel_vaddr (victim_kvaddr));

    ASSERT (page_swap_out_index (victim_frame->vaddr, victim_frame->pd_thread, true, swapping_index));
    for (i = swapping_index*8; i<swapping_index*8+8; i++)
    {
      disk_write(d, i, victim_kvaddr + count*DISK_SECTOR_SIZE);
      count++;
    }

    pagedir_clear_page(victim_frame->pd, victim_frame->vaddr);
    palloc_free_page(victim_kvaddr);
    free (victim_frame);

    kpage = palloc_get_page (PAL_USER|PAL_ZERO);
    swap_lock_release ();

    if (kpage == NULL)
    {
      printf("stack page allocation failed due to memory leak...\n");
      return false;
    }
  }

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (success)
    *esp = PHYS_BASE;
  else
  {
    palloc_free_page (kpage);
    return false;
  }

  struct thread* tcurrent = thread_current();
  pagedir_set_dirty (tcurrent->pagedir, ((uint8_t *) PHYS_BASE) - PGSIZE, true);

  struct page* pi = malloc (sizeof(struct page));
  if (pi == NULL)
    return false;
  pi->load_vaddr = ((uint8_t *) PHYS_BASE) - PGSIZE;
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

  struct frame_elem *fr_elem = malloc (sizeof(struct frame_elem));
  fr_elem->pd = tcurrent->pagedir;
  fr_elem->vaddr = ((uint8_t *) PHYS_BASE) - PGSIZE;
  fr_elem->pd_thread = tcurrent;
  frame_table_push_back(fr_elem);
#else
  if (kpage != NULL)
     {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage); 
     }
#endif

#ifdef USERPROG
  int tmp;
  int* ebp = (int*)*esp;
  int argc = 0;
  char* ret_ptr;
  char* next_ptr;

  char** pointer_tmp_stack = palloc_get_page (PAL_ZERO);
  if(pointer_tmp_stack == NULL)
  {
    palloc_free_page (kpage);
    return false;
  }

  // 스택에 arg 내용을 4바이트 단위씩 저장하고 각각의 포인터를 pointer_tmp_stack에도 저장(그게 argv)
  if( strchr(arg, ' ') == NULL)
  {
    tmp = (int) (strlen(arg) + 4)/4;
    tmp *= 4;
    *esp -= tmp;
    if(*esp < ebp - 0x1000)
    {
      palloc_free_page (kpage);
      palloc_free_page(pointer_tmp_stack);
      return false;
    }
    memcpy(*esp, arg, strlen(arg)+1);
    memcpy((char**)pointer_tmp_stack+argc, esp, sizeof(char*));
    argc += 1;
  }
  else{
  for (ret_ptr = strtok_r(arg, " ", &next_ptr); ret_ptr!= NULL; ret_ptr = strtok_r(NULL, " ", &next_ptr))
  {
    tmp = (int) (strlen(ret_ptr) + 4)/4;
    tmp *= 4;
    *esp -= tmp;
    if(*esp < ebp - 0x1000)
    {
      palloc_free_page (kpage);
      palloc_free_page(pointer_tmp_stack);
      return false;
    }
    memcpy(*esp, ret_ptr, strlen(ret_ptr)+1);
    memcpy((char**)pointer_tmp_stack+argc, esp, sizeof(char*));
    argc += 1;
  }
  }

  // 마지막 argv가 0이 되게 스택에 저장
  *esp -= sizeof(char*);

  // argv[] 를 스택에 저장
  *esp -= argc * sizeof(char*);
  if(*esp < ebp - 0x1000)
  {
    palloc_free_page (kpage);
    palloc_free_page(pointer_tmp_stack);
    return false;
  }
  memcpy(*esp, pointer_tmp_stack, argc*sizeof(char*));
  palloc_free_page (pointer_tmp_stack); 

  // argv 를 스택에 저장

  char* tmpp = *esp;
  *esp -= sizeof(char**);
  if(*esp < ebp - 0x1000)
    return false;
  memcpy(*esp, &tmpp, sizeof(char **));

  // argc 를 스택에 저장
  *esp -= sizeof(int);
  if(*esp < ebp - 0x1000)
    return false;
  memcpy(*esp, &argc, sizeof(int));

  // return address 크기만큼 스택에 저장.
  *esp -= sizeof(void*);
  if(*esp < ebp - 0x1000)
    return false;

#ifdef PRJ3
  set_new_dirty_page (*esp, tcurrent);
#endif
#endif

  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
