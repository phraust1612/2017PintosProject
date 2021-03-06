#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
#ifdef PRJ3
#include "filesys/file.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
static struct lock ready_lock;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

#ifdef PRJ4
/* write back kernel thread, which repeats for every 
 * WRITE_BACK_PERIOD */
static struct thread *write_back_thread;
#endif

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

#ifdef USERPROG
/* lock used for open, close, etc */
static struct lock file_rw_lock;
#endif

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
#ifdef PRJ4
static void repeat_write_back (void *aux UNUSED);
#endif
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static bool is_thread (struct thread *) UNUSED;

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  lock_init (&ready_lock);
  list_init (&ready_list);
#ifdef USERPROG
  lock_init (&file_rw_lock);
#endif
#ifdef PRJ3
  frame_table_init ();
#endif

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

#ifdef PRJ4
/* make write back thread */
void
write_back_start (void)
{
  struct semaphore write_back_started;
  sema_init (&write_back_started, 0);
  thread_create ("write_back_thread",\
      PRI_DEFAULT, repeat_write_back, &write_back_started);

  intr_enable ();
  sema_down (&write_back_started);
}
#endif

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;

  /* Add to run queue. */
  thread_unblock (t);

  specific_thread_set_priority (thread_current()->priority, thread_current());

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  // deny if this function was called by interruption
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  // 
  list_insert_ordered(&ready_list, &t->elem, (list_less_func *) &higher_priority, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Just set our status to dying and schedule another process.
     We will be destroyed during the call to schedule_tail(). */
  intr_disable ();
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *curr = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (curr != idle_thread) 
    list_insert_ordered (&ready_list, &curr->elem, (list_less_func *) &higher_priority, NULL);
  curr->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

void
recalculate_priority()
{
  enum intr_level old_level = intr_disable();
  struct thread* tcurrent = thread_current();
  // release되는 락의 owner 스레드가 더 갖고 있는 락이 없으면
  // 도네이션 받은 priority를 원래대로 돌려놓는다.
  if(list_empty(&tcurrent->lock_own_list))
  {
    intr_set_level(old_level);
    specific_thread_set_priority(tcurrent->origin_priority, tcurrent);
  }
  // release되는 락의 owner가 아직 더 락이 있으면
  // 갖고 있는 락중에 가장 큰 priority를 갖는 것을 찾아
  // 도네이션 받은 priority에서 뭘로 돌아갈지 선택해서 돌아간다.
  else
  {
    int find_priority = 0;
    int comparing_priority;
    struct list_elem* i = list_begin(&tcurrent->lock_own_list);
    struct list_elem* endpoint = list_end(&tcurrent->lock_own_list);
    struct lock* plock;
    struct list_elem* tmpelem;
    struct thread* tmpthread;
    while(i != endpoint)
    {
      plock = list_entry(i, struct lock, own_elem);
      tmpelem = list_begin(&plock->semaphore.waiters);
      tmpthread = list_entry(tmpelem, struct thread, elem);
      comparing_priority = tmpthread->priority;
      if(find_priority< comparing_priority)
        find_priority = comparing_priority;
      i = list_next(i);
    }

    if(find_priority > tcurrent->origin_priority)
    {
      intr_set_level(old_level);
      specific_thread_set_priority(find_priority, tcurrent);
    }
    else
    {
      intr_set_level(old_level);
      specific_thread_set_priority(tcurrent->origin_priority, tcurrent);
    }
  }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  enum intr_level old_level = intr_disable();
  struct thread* tcurrent = thread_current();
  tcurrent->origin_priority = new_priority;
  intr_set_level(old_level);
  specific_thread_set_priority(new_priority, tcurrent);
  recalculate_priority();
}

void
specific_thread_set_priority(int new_priority, struct thread* new_t)
{
  enum intr_level old_level = intr_disable();
  new_t->priority = new_priority;

  if(list_empty (&ready_list)) return;
  list_sort(&ready_list, (list_less_func*) &higher_priority, NULL);
  struct list_elem* first_elem = list_begin(&ready_list);
  struct thread* tfirst = list_entry(first_elem, struct thread, elem);

  if(tfirst->priority >= thread_current()->priority && !intr_context())
    thread_yield();
  intr_set_level(old_level);
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

#ifdef PRJ4
static void
repeat_write_back (void *write_back_started_ UNUSED)
{
  struct semaphore *write_back_started = write_back_started_;
  write_back_thread = thread_current ();
  sema_up (write_back_started);

  for (;;)
  {
    timer_sleep (WRITE_BACK_PERIOD);
    buffer_cache_write_back ();
  }
}
#endif

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);
                    
  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Since `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->origin_priority = priority;
  t->magic = THREAD_MAGIC;
  t->plock_acq = NULL;
  list_init(&t->lock_own_list);
  initial_thread->wakeup_tick = 0;
#ifdef USERPROG
  t->tparent = running_thread();
  if(!is_thread(t->tparent)) t->tparent = t;
  t->ttmpchild = NULL;
  // recursively set current thread's level
  t->next_fd = 2;
  t->child_success = false;
  t->exec_file = NULL;
  sema_init(&t->creation_sema,0);
  list_init(&t->file_list);
  list_init(&t->child_list);
  lock_init(&t->child_list_lock);
  lock_init (&t->file_list_lock);
#endif
#ifdef PRJ3
  t->next_mid = 0;
  t->user_esp = 0xc0000000 - 1;
  lock_init (&t->supplementary_page_lock);
  list_init (&t->mmap_list);
#endif
#ifdef PRJ4
  t->current_dir = 1;
#endif
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
schedule_tail (struct thread *prev) 
{
  struct thread *curr = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  curr->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != curr);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.
   
   It's not safe to call printf() until schedule_tail() has
   completed. */
static void
schedule (void) 
{
  struct thread *curr = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (curr->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (curr != next)
    prev = switch_threads (curr, next);
  schedule_tail (prev); 
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

bool less_wakeup_tick(struct list_elem* x, struct list_elem* y, void* aux UNUSED)
{
  struct thread* tmpx = list_entry(x, struct thread, elem);
  struct thread* tmpy = list_entry(y, struct thread, elem);
  if (tmpx->wakeup_tick < tmpy->wakeup_tick) return true;
  else return false;
}

bool higher_priority (struct list_elem* x, struct list_elem* y, void* aux UNUSED)
{
  struct thread* tmpx = list_entry(x, struct thread, elem);
  struct thread* tmpy = list_entry(y, struct thread, elem);
  if (tmpx->priority > tmpy->priority) return true;
  else return false;
}

void
lock_release_all (struct thread* tcurrent)
{
  enum intr_level old_level = intr_disable();
  struct lock* i = NULL;
  struct list_elem *elem_pointer = list_begin (&tcurrent->lock_own_list);
  while (elem_pointer != list_end (&tcurrent->lock_own_list))
  {
    i = list_entry (elem_pointer, struct lock, own_elem);
    elem_pointer = list_next (elem_pointer);
    if (i->holder == thread_current ())
      lock_release (i);
    else
      list_remove (&tcurrent->elem);
    list_remove (&i->own_elem);
  }
  intr_set_level(old_level);
}

struct thread *
thread_find (tid_t tid)
{
  struct thread* i;
  lock_acquire (&ready_lock);
  struct list_elem *elem_pointer = list_begin (&ready_list);
  while (elem_pointer != list_end (&ready_list))
  {
    i = list_entry (elem_pointer , struct thread, elem);
    if (i->tid == tid)
    {
      lock_release (&ready_list);
      return i;
    }
    elem_pointer = list_next(elem_pointer);
  }
  lock_release (&ready_lock);
  return NULL;
}

#ifdef USERPROG
/* return file_elem* with given fd */
struct file_elem*
find_file (int fd)
{
  struct list_elem* elem_pointer = NULL;
  struct file_elem* i = NULL;
  struct thread* tcurrent = thread_current();

  lock_acquire (&tcurrent->file_list_lock);
  elem_pointer = list_begin(&(tcurrent->file_list));
  while (elem_pointer != list_end(&tcurrent->file_list))
  {
    i = list_entry(elem_pointer, struct file_elem, elem);
    elem_pointer = list_next(elem_pointer);
    if (i->fd == fd)
    {
      lock_release (&tcurrent->file_list_lock);
      return i;
    }
  }
  lock_release (&tcurrent->file_list_lock);
  return NULL;
}

void
file_lock_acquire(void)
{
  lock_acquire(&file_rw_lock);
}

void
file_lock_release(void)
{
  lock_release(&file_rw_lock);
}

/* need to acuire lock before call this func */
struct child_elem*
find_child (tid_t tid, struct thread* t)
{
  struct list_elem* elem_pointer = NULL;
  struct child_elem* i = NULL;

  elem_pointer = list_begin(&t->child_list);
  while (elem_pointer != list_end(&t->child_list))
  {
    i = list_entry(elem_pointer, struct child_elem, elem);
    if (i->child_tid == tid)
      return i;
    elem_pointer = list_next(elem_pointer);
  }
  return NULL;
}
#endif

#ifdef PRJ3
void supplementary_lock_acquire(struct thread* t)
{
  lock_acquire(&t->supplementary_page_lock);
}

void supplementary_lock_release(struct thread* t)
{
  lock_release(&t->supplementary_page_lock);
}

void
munmap_list (mapid_t target_mid)
{
  size_t real_read_bytes;
  uint32_t count;
  off_t prev_off;
  struct thread* tcurrent = thread_current ();
  struct file* f;
  struct mmap_elem* mi;
  struct page* pp;
  struct list_elem* elem_pointer = list_begin (&tcurrent->mmap_list);
  while (elem_pointer != list_end (&tcurrent->mmap_list))
  {
    mi = list_entry (elem_pointer, struct mmap_elem, elem);
    if (mi->mid == target_mid)
    {
      count = 0;
      void* buffer = mi->start_vaddr;
      size_t read_bytes = mi->read_bytes;
      prev_off = file_tell (mi->f);
      while (read_bytes > 0)
      {
        // write back to file disk
        real_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        if (pagedir_is_dirty (tcurrent->pagedir, buffer + count*PGSIZE))
        {
          file_seek (mi->f, count*PGSIZE);
          file_write (mi->f, buffer+count*PGSIZE, real_read_bytes);
        }

        // frame table이랑 supplementary page table에서 지우고
        frame_elem_delete (buffer + count*PGSIZE, tcurrent->pagedir);
        pp = page_lookup (buffer + count*PGSIZE, tcurrent);
        if (pp != NULL)
        {
          supplementary_lock_acquire (tcurrent);
          hash_delete (&tcurrent->supplementary_page_table, &pp->elem);
          free (pp);
          supplementary_lock_release (tcurrent);
        }
        read_bytes -= real_read_bytes;
        count++;
      }

      // tcurrent->mmap_list 에서 지운다.
      elem_pointer = list_remove (elem_pointer);
      file_close (mi->f);
      free (mi);

      return;
    }
    else elem_pointer = list_next (elem_pointer);
  }
}

bool
exist_mmap_elem (int fd, struct thread* tcurrent)
{
  struct mmap_elem* mi;
  struct list_elem* elem_pointer = list_begin (&tcurrent->mmap_list);
  while (elem_pointer != list_end (&tcurrent->mmap_list))
  {
    mi = list_entry (elem_pointer, struct mmap_elem, elem);
    if (mi->fd == fd) return true;
    elem_pointer = list_next (elem_pointer);
  }
  return false;
}
#endif

#ifdef PRJ4
void
print_all_filelist (void)
{
  struct thread *tcurrent = thread_current ();
  printf ("thread : %d, total filelist : %d\n",\
      tcurrent->tid, list_size (&tcurrent->file_list));
  struct file_elem* fi;
  struct list_elem* elem_pointer = list_begin (&tcurrent->file_list);
  while (elem_pointer != list_end (&tcurrent->file_list))
  {
    fi = list_entry (elem_pointer, struct file_elem, elem);
    printf ("fd : %d, f : %p, d : %p\n",\
        fi->fd, fi->f, fi->d);
    elem_pointer = list_next (elem_pointer);
  }
}
#endif

#ifdef PRJ3
void
print_all_pages (void)
{
  struct thread *tcurrent = thread_current ();
  printf ("thread : %d, total pages : %d\n",\
      tcurrent->tid, hash_size (&tcurrent->supplementary_page_table));
}
#endif
