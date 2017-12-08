#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "threads/palloc.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  char *ret_ptr, *next_ptr;
  bool success;
  struct inode *inode;
  disk_sector_t inode_sector = 0;
  struct thread *tcurrent = thread_current ();

  ret_ptr = palloc_get_page (PAL_ZERO);
  if (!ret_ptr) return false;
  strlcpy (ret_ptr, name, PGSIZE);

  struct dir *dir;
  if (name[0] == '/') // absolute path
    dir = dir_open_root ();
  else // relative path
    dir = dir_open (inode_open (tcurrent->current_dir));
  if (dir == NULL)
  {
    palloc_free_page (ret_ptr);
    return false;
  }
  success = false;
#ifdef INODE_PRINT
  printf ("filesys_create - name : %s, %p, size : %d" \
      ", ret_ptr : %p, next_ptr : %p...\n",\
      name, name, initial_size, ret_ptr, next_ptr);
#endif

  for (ret_ptr = strtok_r (ret_ptr , "/", &next_ptr);
      ret_ptr != NULL;
      ret_ptr = strtok_r (NULL, "/", &next_ptr))
  {
#ifdef INODE_PRINT
    printf ("ret_ptr : %s, %p, next_ptr : %s, %p...\n",
        ret_ptr, ret_ptr, next_ptr, next_ptr);
#endif
    if (next_ptr[0] != '\0')
    {
      if (!dir_lookup (dir, ret_ptr, &inode))
      {
        /* 중간인데 못찾으면 false return */
        success = false;
        break;
      }
      dir_close (dir);
      dir = dir_open (inode);
    }
    if (next_ptr[0] == '\0')
    {
      if (dir_lookup (dir, ret_ptr, &inode))
      {
        /* 마지막인데 이미 존재한 경우 return false */
        success = false;
        break;
      }
      else
      {
        success = true;
        break;
      }
    }
  }
  if (!success)
  {
    dir_close (dir);
    palloc_free_page (pg_round_down (ret_ptr));
    return false;
  }
#ifdef INODE_PRINT
  printf ("filesys_create - dir : %d, ret_ptr : %s\n",\
      inode_get_inumber (dir_get_inode (dir)), ret_ptr);
#endif
  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, ret_ptr, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  buffer_cache_write_back ();
  dir_close (dir);

  palloc_free_page (pg_round_down (ret_ptr));
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  char *ret_ptr, *next_ptr;
  struct inode *inode = NULL;
  disk_sector_t inode_sector = 0;
  struct dir *dir;
  struct thread *tcurrent = thread_current ();

#ifdef SUBDIR_DEBUG
  ret_ptr = palloc_get_page (PAL_ZERO);
  if (!ret_ptr) return NULL;
  strlcpy (ret_ptr, name, PGSIZE);

  if (name[0] == '/') // absolute path
    dir = dir_open_root ();
  else // relative path
    dir = dir_open (inode_open (tcurrent->current_dir));
  if (dir == NULL)
  {
    palloc_free_page (ret_ptr);
    return NULL;
  }

#ifdef INODE_PRINT
  printf ("filesys_open - name : %s, ret_ptr : %s, %p\n",\
      name, ret_ptr, ret_ptr);
#endif
  for (ret_ptr = strtok_r (ret_ptr , "/", &next_ptr);
      ret_ptr != NULL;
      ret_ptr = strtok_r (NULL, "/", &next_ptr))
  {
#ifdef INODE_PRINT
  printf ("filesys_open - ret_ptr : %s, %p...\n",\
      ret_ptr, ret_ptr);
#endif
    if (!dir_lookup (dir, ret_ptr, &inode))
    {
      /* 중간인데 못찾으면 return NULL*/
      dir_close (dir);
      palloc_free_page (pg_round_down (ret_ptr));
      return NULL;
    }
    dir_close (dir);
    if (next_ptr[0] != '\0')
      dir = dir_open (inode);
  }
 
  if (inode == NULL && name[0] == '/')
    inode = inode_open (ROOT_DIR_SECTOR);

#ifdef INODE_PRINT
  printf ("filesys_open - inode : %p, open_cnt : %d\n", \
      inode, inode_open_cnt (inode));
#endif
  palloc_free_page (pg_round_down (ret_ptr));
#else
  inode = NULL;
  dir = dir_open_root ();
  if (dir != NULL)
    dir_lookup (dir, name, &inode);
#endif

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false;
  char *ret_ptr, *next_ptr;
  struct inode *inode;
  disk_sector_t inode_sector = 0;
  struct thread *tcurrent = thread_current ();
  struct dir *dir;

#ifdef SUBDIR_DEBUG
  ret_ptr = palloc_get_page (PAL_ZERO);
  if (!ret_ptr) return false;
  strlcpy (ret_ptr, name, PGSIZE);

  if (name[0] == '/') // absolute path
    dir = dir_open_root ();
  else // relative path
    dir = dir_open (inode_open (tcurrent->current_dir));
  if (dir == NULL)
  {
    palloc_free_page (ret_ptr);
    return NULL;
  }

  for (ret_ptr = strtok_r (ret_ptr , "/", &next_ptr);
      ret_ptr != NULL;
      ret_ptr = strtok_r (NULL, "/", &next_ptr))
  {
    if (next_ptr[0] == '\0')
    {
      success= true;
      break;
    }
    if (!dir_lookup (dir, ret_ptr, &inode))
    {
      /* 중간인데 못찾으면 return NULL*/
      success = false;
      break;
    }
    dir_close (dir);
    dir = dir_open (inode);
  }

  /* 디렉토리에 뭐 있으면 안됨 */

  if (success)
    success = dir_remove (dir, ret_ptr);
  dir_close (dir); 
  palloc_free_page (pg_round_down (ret_ptr));
#else
  dir = dir_open_root ();
  success = dir_remove (dir, name);
#endif

  return dir != NULL && success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
