#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "devices/input.h"

void syscall_init (void);

#endif /* userprog/syscall.h */
