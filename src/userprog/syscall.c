#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);
bool check_valid_pointer (void* pointer, struct intr_frame* f);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  bool mode;
  int* arg;
  const char* buffer;
  int* size;
  disk_sector_t new_sector;
  char *ret_ptr, *next_ptr;
  struct page* pi;
  struct mmap_elem* mi;
  struct file_elem* f_elem;
  struct child_elem* t_elem;
  struct dir *dir;
  struct inode *inode;
  struct thread* tcurrent = thread_current();
  if (is_user_vaddr (f->esp))
    set_new_dirty_page (f->esp, tcurrent);

  if(!check_valid_pointer(f->esp, f))
  {
    buffer = (const char*) tcurrent->name;
    printf("%s: exit(%d)\n", buffer, -1);
    thread_exit();
    return;
  }
  int syscall_no = * (int*)f->esp ;
  switch(syscall_no)
  {
    case SYS_HALT:
    case SYS_EXIT:
      arg = (int*)f->esp + 1;
      buffer = (const char*) tcurrent->name;
      if(!check_valid_pointer(arg, f))
        printf("%s: exit(%d)\n", buffer, -1);
      else
      {
        t_elem = find_child(tcurrent->tid, tcurrent->tparent);
        t_elem->exit_status = *arg;
        printf("%s: exit(%d)\n", buffer, (int) *arg);
      }
      thread_exit();
      break;
    case SYS_EXEC:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer((void*) buffer, f))
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
      if(check_valid_pointer(arg, f))
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
      if(check_valid_pointer((void*)buffer, f) && check_valid_pointer(size, f))
      {
        file_lock_acquire();
        f->eax = filesys_create((const char*)buffer, (int) *size);
        file_lock_release();
      }
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_REMOVE:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer((void*) buffer, f))
      {
        file_lock_acquire();
        f->eax = filesys_remove ((const char*)buffer);
        file_lock_release();
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_OPEN:
      buffer = (const char*)* ((int*)f->esp + 1);
      if(check_valid_pointer((void*)buffer, f))
      {
        f_elem = palloc_get_page(PAL_ZERO);
        if(f_elem == NULL)
        {
          f->eax = -1;
          break;
        }
        file_lock_acquire();
        f_elem->f = filesys_open((const char*)buffer);
        file_lock_release();
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
      if(check_valid_pointer(arg, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          f->eax = (int) inode_length(file_get_inode(f_elem->f));
          file_lock_release();
        }
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
      if(check_valid_pointer(arg, f) && check_valid_pointer((void*)buffer, f)
          && check_valid_pointer(size, f))
      {
        if(*arg == 0)
          f->eax = input_getc();
        else
        {
          f_elem = find_file(*arg);
          if(f_elem != NULL/* && pagedir_is_writable(tcurrent->pagedir, buffer) */)
          {
            file_lock_acquire();
            f->eax = file_read(f_elem->f, (void*)buffer, (int) *size);
            file_lock_release();
          }
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
      if(check_valid_pointer(arg, f) && check_valid_pointer((void*)buffer, f)
          && check_valid_pointer(size, f))
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
          {
            file_lock_acquire();
            f->eax = file_write (f_elem->f, (void*) buffer, (int) *size);
            file_lock_release();
          }
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
      arg = (int*)f->esp + 1; // fd
      size = (int*)f->esp + 2; //position
      if(check_valid_pointer(arg, f) && check_valid_pointer(size, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          file_seek(f_elem->f, (uint32_t) *size);
          file_lock_release();
        }
      }
      else
      {
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_TELL:
      arg = (int*)f->esp + 1; // fd
      if(check_valid_pointer(arg, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          f->eax = file_tell(f_elem->f);
          file_lock_release();
        }
      }
      else
      {
        f->eax = 0;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_CLOSE:
      arg = (int*)f->esp + 1; // fd
      if(check_valid_pointer(arg, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          if (!exist_mmap_elem (f_elem->fd, tcurrent))
            file_close((struct file*) f_elem->f);
          list_remove(&f_elem->elem);
          palloc_free_page(f_elem);
          file_lock_release();
        }
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_MMAP:
      arg = (int*)f->esp + 1;  // fd
      buffer = (void*)* ((int*)f->esp + 2); // addr
      if (check_valid_pointer (arg, f) && is_user_vaddr (buffer))
      {
        f_elem = find_file (*arg);
        if (f_elem != NULL)
        {
          file_lock_acquire ();
          off_t mapped_size = file_length (f_elem->f);
          file_lock_release ();
          if (mapped_size <=0
            || buffer == NULL
            || pg_ofs (buffer) != 0)
          {
            f->eax = -1;
            return;
          }

          unsigned count = 0;
          int read_bytes = mapped_size;
          while (read_bytes > 0)
          {
            if (page_lookup (buffer + count*PGSIZE, tcurrent))
            {
              f->eax = -1;
              return;
            }
            read_bytes -= PGSIZE;
            count++;
          }

          mi = malloc (sizeof (struct mmap_elem));
          if (mi == NULL)
          {
            f->eax = -1;
            printf("%s: exit(%d)\n", tcurrent->name, -1);
            thread_exit();
          }
          mi->start_vaddr = buffer;
          mi->read_bytes = mapped_size;
          mi->fd = *arg;
          mi->f = f_elem->f;
          mi->mid = tcurrent->next_mid;
          tcurrent->next_mid++;

          f->eax = mi->mid;
          list_push_back (&tcurrent->mmap_list, &mi->elem);

          count = 0;
          read_bytes = mapped_size;
          while (read_bytes > 0)
          {
            pi = malloc (sizeof (struct page));
            if (pi == NULL)
            {
              free (mi);
              f->eax = -1;
              printf("%s: exit(%d)\n", tcurrent->name, -1);
              thread_exit();
            }
            pi->load_vaddr = buffer + count*PGSIZE;
            pi->load_filepos = PGSIZE * count;
            pi->load_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
            pi->load_zero_bytes = PGSIZE - pi->load_read_bytes;
            pi->writable = true;  // TODO: ???
            pi->swap_outed = false;
            pi->swap_index = 0;
            pi->f = f_elem->f;
            pi->mmaped = true;
            supplementary_lock_acquire(tcurrent);
            hash_replace (&tcurrent->supplementary_page_table, &pi->elem);
            supplementary_lock_release(tcurrent);
            read_bytes -= PGSIZE;
            count++;
          }
        }
        // wrong file descriptor or not found from file list
        else
        {
          f->eax = -1;
          printf("%s: exit(%d)\n", tcurrent->name, -1);
          thread_exit();
        }
      }
      else
      {
        f->eax = -1;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_MUNMAP:
      arg = (int*)f->esp + 1;  // mapid_t
      if (check_valid_pointer (arg, f))
      {
        // frame_table에서 lazy load된 mmap을 dealloc
        // supplementary에서 다 지우고
        // tcurrent에서 mmap_list에서 없애준다.
        munmap_list (*arg);
      }
      else
      {
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_CHDIR:
      buffer = (void*)* ((int*)f->esp + 1); // dir name
      if (check_valid_pointer (buffer, f))
      {
        if (buffer[0] == '/') // absolute path
          dir = dir_open_root ();
        else // relative path
          dir = dir_open (inode_open (tcurrent->current_dir));
        if (dir == NULL)
        {
          file_lock_release ();
          f->eax = false;
          printf("%s: exit(%d)\n", tcurrent->name, -1);
          thread_exit();
        }

        for (ret_ptr = strtok_r (buffer, "/", &next_ptr);
            ret_ptr != NULL;
            ret_ptr = strtok_r (NULL, "/", &next_ptr))
        {
          if (!dir_lookup (dir, ret_ptr, &inode))
          {
            /* 중간인데 못찾으면 false return */
            f->eax = false;
            break;
          }
          dir_close (dir);
          dir = dir_open (inode);
          if (next_ptr[0] == '\0')
          {
            f->eax = true;
            tcurrent->current_dir = \
                inode_get_inumber (dir_get_inode (dir));
          }
        }
        dir_close (dir);
      }
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_MKDIR:
      buffer = (void*)* ((int*)f->esp + 1); // dir name
      if (check_valid_pointer (buffer, f))
      {
        if (buffer[0] == '/') // absolute path
          dir = dir_open_root ();
        else // relative path
          dir = dir_open (inode_open (tcurrent->current_dir));
        if (dir == NULL)
        {
          file_lock_release ();
          f->eax = false;
          printf("%s: exit(%d)\n", tcurrent->name, -1);
          thread_exit();
        }

        for (ret_ptr = strtok_r (buffer, "/", &next_ptr);
            ret_ptr != NULL;
            ret_ptr = strtok_r (NULL, "/", &next_ptr))
        {
          if (next_ptr[0] != '\0')
          {
            if (!dir_lookup (dir, ret_ptr, &inode))
            {
              /* 중간인데 못찾으면 false return */
              f->eax = false;
              break;
            }
            dir_close (dir);
            dir = dir_open (inode);
          }
          if (next_ptr[0] == '\0')
          {
            if (dir_lookup (dir, ret_ptr, &inode))
              /* 마지막인데 이미 존재한 경우 return false */
              f->eax = false;
            else
            {
              free_map_allocate (1, &new_sector);
              dir_create (new_sector, inode_get_inumber (inode), 16);
              dir_add (dir, ret_ptr, new_sector);
              f->eax = true;
            }
          }
        }
        dir_close (dir);
      }
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_READDIR:
      arg = (int*)f->esp + 1;  // fd
      buffer = (void*)* ((int*)f->esp + 2); // readdir name
      if (check_valid_pointer (arg, f) && \
          check_valid_pointer (buffer, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          dir = dir_open (file_get_inode (f_elem->f));
          if (dir == NULL)
          {
            file_lock_release ();
            f->eax = false;
            printf("%s: exit(%d)\n", tcurrent->name, -1);
            thread_exit();
          }

          f->eax = dir_readdir (dir, buffer);
          free (dir);
          file_lock_release();
        }
        else f->eax = false;
      }
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_ISDIR:
      arg = (int*)f->esp + 1;  // fd
      if (check_valid_pointer (arg, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          f->eax = inode_is_dir (file_get_inode (f_elem->f));
          file_lock_release();
        }
        else f->eax = false;
      }
      else
      {
        f->eax = false;
        printf("%s: exit(%d)\n", tcurrent->name, -1);
        thread_exit();
      }
      break;
    case SYS_INUMBER:
      arg = (int*)f->esp + 1;  // fd
      if (check_valid_pointer (arg, f))
      {
        f_elem = find_file(*arg);
        if(f_elem != NULL)
        {
          file_lock_acquire();
          f->eax = inode_get_inumber (file_get_inode (f_elem->f));
          file_lock_release();
        }
        else f->eax = -1;
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
check_valid_pointer (void* pointer, struct intr_frame* f)
{
  struct thread* tcurrent = thread_current();
  uint32_t *pd = tcurrent->pagedir;
  if (!is_user_vaddr (pointer)) return false;

  void* get_page = pagedir_get_page(pd, pointer);
  if (!get_page)
  {
    if (page_lookup (pointer, tcurrent)) return true;
    else if (pointer > f->esp) return true; /* stack page에 있을 경우로 예상됨 */
    else return false;
  }

  return true;
}
