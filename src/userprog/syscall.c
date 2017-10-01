#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
bool check_valid_pointer (void* pointer);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int arg;
  const char* buffer;
  int size;
  struct thread* tcurrent = thread_current();

  if(!check_valid_pointer(f->esp))
  {
    thread_exit();
    ASSERT(false);
    return;
  }
  int syscall_no = * (int*)f->esp ;
  switch(syscall_no)
  {
    case SYS_WRITE:
      /*
      arg = *((int*)f->esp + 1);  // fd
      buffer = (const char*)* ((int*)f->esp + 2);
      if(!check_valid_pointer(buffer))
      {
        thread_exit();
        ASSERT(false);
        return;
      }
      size = (int) *((int*)f->esp + 3);
      putbuf((const char*) buffer, (size_t) size);
      break;
      */
    case SYS_HALT:
    case SYS_EXIT:
      /*
      arg = *((int*)f->esp + 1);
      buffer = (const char*) tcurrent->name;
      printf("%s: exit(%d)\n", buffer, arg);
      thread_exit();
      */
    case SYS_EXEC:
    case SYS_WAIT:
      arg = *((int*)f->esp + 1);  // pid
      process_wait((tid_t) arg);
      break;
    case SYS_CREATE:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_READ:
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
    default:
      printf ("system call!\n");
      thread_exit ();
      return;
  }
}

// false면 free/release해야할 상황이 있을 수 있음.
bool
check_valid_pointer (void* pointer)
{
  struct thread* tcurrent = thread_current();
  uint32_t *pd = tcurrent->pagedir;
  if (!is_user_vaddr (pointer)) return false;

  void* get_page = pagedir_get_page(pd, pointer);
  if (!get_page) return false;

  return true;
}
