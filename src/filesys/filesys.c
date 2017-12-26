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
#include "threads/vaddr.h"

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
#ifdef PRJ4
  buffer_cache_write_back ();
#endif
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  disk_sector_t inode_sector = 0;
  struct dir *dir;
  bool success;
#ifdef PRJ4
  char *ret_ptr, *next_ptr;
  struct inode *inode;
  struct thread *tcurrent = thread_current ();

  ret_ptr = palloc_get_page (PAL_ZERO);
  char** ptpt = malloc (sizeof (char**));
  memcpy (ptpt, &ret_ptr, sizeof (char**));
  if (!ret_ptr) return false;
  strlcpy (ret_ptr, name, PGSIZE);

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

  for (ret_ptr = strtok_r (ret_ptr , "/", &next_ptr);
      ret_ptr != NULL;
      ret_ptr = strtok_r (NULL, "/", &next_ptr))
  {
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
      if (!dir) 
        goto done1;
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
    palloc_free_page (*ptpt);
    free (ptpt);
    return false;
  }
  success = false;
  if (dir == NULL) goto done1;
  if (inode_get_level (dir_get_inode (dir)) > 212) goto done1;
  if (!free_map_allocate (1, &inode_sector)) goto done1;
  if (!inode_create (inode_sector, initial_size, 0))
  {
    free_map_release (inode_sector, 1);
    goto done1;
  }
  if (!dir_add (dir, ret_ptr, inode_sector))
  {
    release_inode_disk (DIV_ROUND_UP (initial_size, DISK_SECTOR_SIZE), inode_sector);
    free_map_release (inode_sector, 1);
    goto done1;
  }
  success = true;

done1:
  buffer_cache_write_back ();
  dir_close (dir);
  palloc_free_page (*ptpt);
  free (ptpt);
#else
  dir = dir_open_root ();
  success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  dir_close (dir);
#endif
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
  struct dir *dir;
  struct inode *inode = NULL;
#ifdef PRJ4
  char *ret_ptr, *next_ptr;
  disk_sector_t inode_sector = 0;
  struct thread *tcurrent = thread_current ();

  ret_ptr = palloc_get_page (PAL_ZERO);
  if (!ret_ptr)
    return NULL;
  char** ptpt = malloc (sizeof (char**));
  memcpy (ptpt, &ret_ptr, sizeof (char**));
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
    if (!dir_lookup (dir, ret_ptr, &inode))
    {
      /* 중간인데 못찾으면 return NULL*/
      dir_close (dir);
      palloc_free_page (*ptpt);
      free (ptpt);
      return NULL;
    }
    dir_close (dir);
    if (next_ptr[0] != '\0')
    {
      dir = dir_open (inode);
      if (!dir)
      {
        palloc_free_page (*ptpt);
        free (ptpt);
        return NULL;
      }
    }
  }
 
  if (inode == NULL && name[0] == '/')
    inode = inode_open (ROOT_DIR_SECTOR);

  palloc_free_page (*ptpt);
  free (ptpt);
#else
  dir = dir_open_root ();
  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);
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
  struct dir *dir;
  bool success = false;
#ifdef PRJ4
  char *ret_ptr, *next_ptr;
  struct inode *inode;
  disk_sector_t inode_sector = 0;
  struct thread *tcurrent = thread_current ();

  ret_ptr = palloc_get_page (PAL_ZERO);
  if (!ret_ptr) return false;
  char** ptpt = malloc (sizeof (char**));
  memcpy (ptpt, &ret_ptr, sizeof (char**));
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
    if (!dir)
    {
      palloc_free_page (*ptpt);
      free (ptpt);
      return false;
    }

  }

  /* 디렉토리에 뭐 있으면 안됨 */

  if (success)
    success = dir_remove (dir, ret_ptr);
  dir_close (dir); 
  palloc_free_page (*ptpt);
  free (ptpt);
#else
  dir = dir_open_root ();
  success = dir_remove (dir, name);
  dir_close (dir); 
#endif

  return dir != NULL && success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
#ifdef PRJ4
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16))
#else
  if (!dir_create (ROOT_DIR_SECTOR, 16))
#endif
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
