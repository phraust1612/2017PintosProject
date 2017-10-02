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
  int* arg;
  const char* buffer;
  int* size;
  struct file_elem* f_elem;
  struct thread* tcurrent = thread_current();

  if(!check_valid_pointer(f->esp))
  {
    buffer = (const char*) tcurrent->name;
    printf("%s: exit(%d)\n", buffer, -1);
    thread_exit();
    ASSERT(false);
    return;
  }
  int syscall_no = * (int*)f->esp ;
  switch(syscall_no)
  {
    case SYS_HALT:
    case SYS_EXIT:
      arg = (int*)f->esp + 1;
      buffer = (const char*) tcurrent->name;
      if(!check_valid_pointer(arg))
        printf("%s: exit(%d)\n", buffer, -1);
      else
      {
        tcurrent->tparent->child_exit_status = *arg;
        printf("%s: exit(%d)\n", buffer, (int) *arg);
      }
      thread_exit();
      break;
    case SYS_EXEC:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer(buffer))
      {
        f->eax = process_execute(buffer);
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_WAIT:
      arg = (int*)f->esp + 1;  // 
      if(check_valid_pointer(arg))
        f->eax = process_wait((tid_t) *arg);
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_CREATE:
      buffer = (const char*)* ((int*)f->esp + 1);
      size = (int*)f->esp+2;
      // return 값은 eax로 넘겨준다.
      if(check_valid_pointer((void*)buffer) && check_valid_pointer(size))
        f->eax = filesys_create((const char*)buffer, (int) *size);
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_REMOVE:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer(buffer))
        f->eax = filesys_remove ((const char*)buffer);
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_OPEN:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer((void*)buffer))
      {
        f_elem = palloc_get_page(PAL_ZERO);
        f_elem->f = filesys_open((const char*)buffer);
        if(f_elem->f == NULL)
        {
          palloc_free_page(f_elem);
          f->eax = -1;
        }
        else
        {
          f_elem->fd = tcurrent->next_fd;
          tcurrent->next_fd++;
          list_push_back(&tcurrent->file_list, &f_elem->elem);
          f->eax = f_elem->fd;
        }
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_FILESIZE:
      arg = (int*)f->esp + 1; // fd
      if(check_valid_pointer(arg))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
          f->eax = (int) inode_length(file_get_inode(f_elem->f));
        else f->eax = 0;
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_READ:
      arg = (int*)f->esp + 1; // fd
      buffer = (const char*)* ((int*)f->esp + 2);
      size = (int*)f->esp+3;
      if(check_valid_pointer(arg) && check_valid_pointer((void*)buffer)
          && check_valid_pointer(size))
      {
        if(*arg == 0)
          f->eax = input_getc();
        else
        {
          f_elem = find_file(*arg);
          if(f_elem != NULL)
            f->eax = file_read(f_elem->f, (void*)buffer, (int) *size);
          else
            f->eax = -1;
        }
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_WRITE:
      arg = (int*)f->esp + 1;  // fd
      buffer = (const char*)* ((int*)f->esp + 2);
      size = (int*)f->esp+3;
      if(check_valid_pointer(arg) && check_valid_pointer((void*)buffer)
          && check_valid_pointer(size))
      {
        // fd 가 stdout(1) 의 경우
        if(*arg == 1)
        {
          putbuf((const char*) buffer, (size_t) *size);
          f->eax = *size;
        }
        // file에 직접 쓰는 경우.
        else
        {
          f_elem = find_file(*arg);
          if(f_elem == NULL)
            f->eax = 0;
          else
            f->eax = file_write (f_elem->f, (void*) buffer, (int) *size);
        }
      }
      // 올바르지 않은 argument를 받은 경우
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
      arg = (int*)f->esp + 1; // fd
      if(check_valid_pointer(arg))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_close((struct file*) f_elem->f);
          list_remove(&f_elem->elem);
          palloc_free_page(f_elem);
        }
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
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
