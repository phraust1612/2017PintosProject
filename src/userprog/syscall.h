#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#ifdef USERPROG
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "devices/input.h"
void file_lock_init(void);
#endif
#ifdef PRJ3
#include "vm/page.h"
#endif

void syscall_init (void);

#endif /* userprog/syscall.h */
