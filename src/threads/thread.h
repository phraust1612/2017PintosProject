#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#ifndef THREADS_SYNCH_H
#include "threads/synch.h"
#endif

#ifdef PRJ3
#include "vm/page.h"
#include "vm/swap.h"
#include "lib/kernel/hash.h"
typedef int mapid_t;
#endif

#ifdef PRJ4
#include "devices/disk.h"
#include "filesys/cache.h"
#endif


#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

#ifdef USERPROG
struct child_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
    struct thread* tchild;
    tid_t child_tid;
    int exit_status;
  };

struct file_elem
{
  struct list_elem elem;
  struct file* f;
#ifdef PRJ4
  struct dir *d;
#endif
  int fd;
};
#endif

#ifdef PRJ3
struct mmap_elem
{
  struct list_elem elem;
  uint32_t start_vaddr;
  uint32_t read_bytes;
  mapid_t mid;
  int fd;
  struct file* f;
};
#endif

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    
    int wakeup_tick;
    // lock_list : 이 스레드가 own하고 있는 모든 locks
    struct list lock_own_list;
    // lock_acq_list : 이 스레드가 acquire하고 싶어하는 'a' lock
    //struct list_elem* lock_acq_elem;
    struct lock* plock_acq;
    int origin_priority;

#ifdef USERPROG
    struct semaphore creation_sema;

    struct lock child_list_lock;
    struct list child_list;
    struct thread* tparent;
    bool child_success;
    struct thread* ttmpchild;

    // struct file* 와 대응하는 file descriptor(int)의 리스트
    struct list file_list;
    struct lock file_list_lock;
    // 다음 할당될 fd
    int next_fd;
    struct file* exec_file;

    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif
#ifdef PRJ3
    struct list mmap_list;
    int next_mid;
    struct hash supplementary_page_table;
    struct lock supplementary_page_lock;

    void* user_esp;
#endif
#ifdef PRJ4
    disk_sector_t current_dir;
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
struct thread *thread_find (tid_t tid);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

void recalculate_priority(void);
int thread_get_priority (void);
void thread_set_priority (int);
void specific_thread_set_priority (int new_priority, struct thread* new_t);
void lock_release_all (struct thread *);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

// less_wakeup_tick
// return true if x has less wakeup_tick than y
bool less_wakeup_tick(struct list_elem* x, struct list_elem* y, void* aux UNUSED);
bool higher_priority (struct list_elem* x, struct list_elem* y, void* aux UNUSED);

#ifdef USERPROG
struct file_elem* find_file(int fd);
struct child_elem* find_child(tid_t tid, struct thread* t);
void file_lock_acquire(void);
void file_lock_release(void);
#endif

#ifdef PRJ3
void supplementary_lock_acquire(struct thread* t);
void supplementary_lock_release(struct thread* t);
void munmap_list (mapid_t target_mid);
bool exist_mmap_elem (int fd, struct thread* tcurrent);
#endif

#ifdef PRJ4
void write_back_start (void);
void print_all_filelist (void);
void print_all_pages (void);
#endif

#endif /* threads/thread.h */
