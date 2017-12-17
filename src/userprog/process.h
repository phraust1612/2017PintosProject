#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#ifdef USERPROG
#include "threads/palloc.h"
#endif
#ifdef PRJ3
#include "vm/page.h"
#endif

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
