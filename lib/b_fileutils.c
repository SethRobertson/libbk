#if !defined(lint) && !defined(__INSIGHT__)
static char libbk__rcsid[] = "$Id: b_fileutils.c,v 1.6 2001/12/20 21:03:13 jtt Exp $";
static char libbk__copyright[] = "Copyright (c) 2001";
static char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

/**
 * @file
 * Random utilities relating to files and file descriptors.
 */

#include <libbk.h>
#include "libbk_internal.h"

#define MAXLINE 	1024

/**
 * Information on locked file to permit you unlock it.
 */
struct file_lock 
{
  bk_flags		fl_flags;		///< Everyone needs flags.
  char *		fl_path;		///< Path name of resource to be locked.
  char *		fl_lock_id;		///< Name of tmpname file.
  char *		fl_admin_ext;		///< Admin file name extension.
  char *		fl_lock_ext;		///< Lock file name extension.
};


struct file_lock_admin
{
  char *		fla_lock;		///< Name of lock file.
  char *		fla_admin;		///< Admin file name.
  char *		fla_tmpname;		///< Admin file name.
};


static struct file_lock_admin *lock_admin_file(bk_s B, const char *ipath, const char *iadmin_ext, const char *ilock_ext);
static void unlock_admin_file(bk_s B, struct file_lock_admin *fla);
static struct file_lock *fl_create(bk_s B);
static void fl_destroy(bk_s B, struct file_lock *fl);
static struct file_lock_admin *fla_create(bk_s B);
static void fla_destroy(bk_s B, struct file_lock_admin *fla);


/**
 * Add a flag (or set of flags) to a descriptor
 *
 *	@param B BAKA thread/global state.
 *	@param fd The descriptor to modify
 *	@param flags The flags to add.
 *	@param action What action to take (add/delete).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_fileutils_modify_fd_flags(bk_s B, int fd, long flags, bk_fileutils_modify_fd_flags_action_e action)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  long oflags = 0;

  if (action != BkFileutilsModifyFdFlagsActionSet && (oflags=fcntl(fd, F_GETFL)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not recover current flags: %s\n", strerror(errno));
    goto error;
  }

  switch (action)
  {
  case BkFileutilsModifyFdFlagsActionAdd:
    BK_FLAG_SET(oflags,flags);
    break;
  case BkFileutilsModifyFdFlagsActionDelete:
    BK_FLAG_CLEAR(oflags,flags);
    break;
  case BkFileutilsModifyFdFlagsActionSet:
    oflags=flags;
    break;
  default:
    bk_error_printf(B, BK_ERR_ERR, "Unknown fd flag action: %d\n", action);
    goto error;
    break;
  }

  if (fcntl(fd, F_SETFL, oflags))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not set new flags: %s\n", strerror(errno));
    goto error;
  }
  BK_RETURN(B,0);

 error:
  BK_RETURN(B,-1);
}



/**
 * Lock a resource in a safe way. This is in fileutils since it uses file
 * locking. but the resource it locks may be arbitrary -- as long as you
 * can provide a unique name for the resource, we can lock it.
 *
 * <WARNING>
 * These routines are not a secure as we'd like yet. we use mkstemp(3)
 * which is prett good on local unix FS, but not across NFS (eg). We really
 * need to create a unique file name (with some sort of md5 thing
 * presumably) and create that instead. This is not technically safer, but
 * is less likely to clash
 * </WARNING>
 * 
 *
 *	@param B BAKA thread/global state.
 *	@param resource The resource to lock.
 *	@param type Lcck type.
 *	@param admin_ext Optional extension to admin file.
 *	@param lock_ext Option extension to lock file.
 *	@param flags Flags for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
void *
bk_file_lock(bk_s B, const char *resource, bk_file_lock_type_e type, const char *admin_ext, const char *lock_ext, int *held, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  FILE *fp = NULL;
  char line[MAXLINE];
  struct file_lock *fl = NULL;
  struct file_lock_admin *fla = NULL;

  if (!resource)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (held) *held = 0;

  if (!(fl = fl_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create fl: %s\n", strerror(errno));
    goto error;
  }

  if (!(fla = lock_admin_file(B, resource, admin_ext, lock_ext)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not lock admin file\n");
    goto error;
  }

  if (!(fp = fopen(fla->fla_admin, "r+")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open admin file: %s\n", strerror(errno));
    goto error;
  }

  if (!fgets(line, sizeof(line), fp))
  {
    if (ferror(fp))
    {
      bk_error_printf(B, BK_ERR_ERR, "Error reading admin file: %s\n", strerror(errno));
      goto error;
    }
    // Must be EOF. Cool. That means we have lock. Write our lock typ and info and take off.
    switch(type)
    {
    case BkFileLockTypeShared:
      fprintf(fp,"%s\n", BK_FILE_LOCK_MODE_SHARED);
      break;
    case BkFileLockTypeExclusive:
      fprintf(fp,"%s\n", BK_FILE_LOCK_MODE_EXCLUSIVE);
      break;
      // No default so gcc can catch errors better.
    }
    fprintf(fp,"%s\n", fla->fla_tmpname);
  }
  else
  {
    bk_string_rip(B, line, NULL, 0);
    if (BK_STREQ(line, BK_FILE_LOCK_MODE_EXCLUSIVE))
    {
      // We are never compatiple with an exclusive lock.
      goto lock_held;
    }

    // At this point we are comparing what we want with SHARED (in file).
    switch (type)
    {
    case BkFileLockTypeShared:
      // Shared is compatiple with shared
      if (fseek(fp, 0, SEEK_END) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not seek to end admin file\n");
	goto error;
      }
      fprintf(fp,"%s\n", fla->fla_tmpname);
      break;

    case BkFileLockTypeExclusive:
      // Exclusive is not compatiple with shared.
      goto lock_held;
      break;
      // No default so gcc can catch errors better.
    }
  }

  if (!(fl->fl_path = strdup(resource)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup path name: %s\n", strerror(errno));
    goto error;
  }
  
  if (!(fl->fl_lock_id = strdup(fla->fla_tmpname)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup tmpname name: %s\n", strerror(errno));
    goto error;
  }
  
  if (admin_ext)
  {
    if (!(fl->fl_admin_ext = strdup(admin_ext)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup admin extension name: %s\n", strerror(errno));
      goto error;
    }
  }
  
  if (lock_ext)
  {
    if (!(fl->fl_lock_ext = strdup(lock_ext)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not strdup lock extension name: %s\n", strerror(errno));
      goto error;
    }
  }


  fl->fl_flags = flags;

  // Clean up
  fclose(fp);
  unlock_admin_file(B, fla);
  BK_RETURN(B,fl);  

 lock_held:
  if (held) *held = 1;

 error:
  if (fp) fclose(fp);
  if (fla) unlock_admin_file(B, fla);
  if (fl) fl_destroy(B, fl);
  BK_RETURN(B,NULL);
}



/**
 * Unlock a file locked with above function.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Data returned from @a bk_file_lock()
 *	@param flags Flags for the future.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_file_unlock(bk_s B, void *opaque, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct file_lock *fl = opaque;
  struct file_lock_admin *fla = NULL;
  FILE *fp = NULL;
  char *tmpbuf = NULL;
  struct stat sb;
  char *p;
  int line_cnt = 0;
  char line[MAXLINE];
  int found_lock = 0;
  int do_unlink = 0;
  

  if (!fl)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (!fl->fl_lock_id)
  {
    bk_error_printf(B, BK_ERR_ERR, "No lock id to search for set!\n");
    goto error;
  }

  if (!(fla = lock_admin_file(B, fl->fl_path, fl->fl_admin_ext, fl->fl_lock_ext)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not lock admin file\n");
    goto error;
  }
  
  if (!(fp = fopen(fla->fla_admin, "r+")))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open admin file: %s\n", strerror(errno));
    goto error;
  }

  if (fstat(fileno(fp), &sb) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not stat admin file: %s\n", strerror(errno));
    goto error;
  }

  if (!(BK_CALLOC_LEN(tmpbuf, sb.st_size)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate output buffer: %s\n", strerror(errno));
    goto error;
  }

  p = tmpbuf;
  line_cnt = 0;

  while(fgets(line, MAXLINE, fp))
  {
    bk_string_rip(B, line, NULL, 0);
    if (BK_STREQ(line, fl->fl_lock_id))
    {
      found_lock = 1;
      continue;
    }
    line_cnt++;
    sprintf(p, "%s\n", line);
    p += (strlen(line)+1);
  }

  if (!found_lock)
  {
    bk_error_printf(B, BK_ERR_ERR, "could not find lock to release\n");
  }

  // OK now rewrite the file.
  fflush(fp);
  if (ftruncate(fileno(fp), 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not truncate admin file. Lock system may be in incomplete state\n");
    goto error;
  }
  rewind(fp);

  // Only write back if we weren't the last lock.
  if (line_cnt > 1)
  {
    fwrite(tmpbuf, 1, strlen(tmpbuf), fp);
  }
  else
  {
    do_unlink = 1;
  }

  if (ferror(fp))
  {
    bk_error_printf(B, BK_ERR_ERR, "Error reading admin file: %s\n", strerror(errno));
    goto error;
  }

  fclose(fp);
  free(tmpbuf);
  if (do_unlink && (unlink(fla->fla_admin) < 0)) 
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not unlink: %s: %s\n", fla->fla_admin, strerror(errno));
    // Oh well forge on.
  }
  unlock_admin_file(B, fla);
  fl_destroy(B, fl);
  BK_RETURN(B,0);

 error:
  if (tmpbuf) free(tmpbuf);
  if (fp) fclose(fp);
  /*
   * NB: unlock_admin_file() does not actually reference the admin file, so
   * calling this after the admin file might be unlinked is just fine
   */
  if (fla) unlock_admin_file(B, fla);
  if (fl) fl_destroy(B, fl);
  BK_RETURN(B,-1);
}



/**
 * Lock administrative file.
 *	@param B BAKA thread/global state.
 *	@param path Name of <em>data</em> file we want lock (the admin file
 *		name is generated from this base).
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static struct file_lock_admin *
lock_admin_file(bk_s B, const char *ipath, const char *iadmin_ext, const char *ilock_ext)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");
  char admin[MAXNAMLEN];
  char lock[MAXNAMLEN];
  char tmpname[MAXNAMLEN];
  int len;
  struct stat sb;
  struct file_lock_admin *fla = NULL;
  int fd = -1;
  const char *admin_ext;
  const char *lock_ext;
  char path[MAXNAMLEN];
  char *p;

  if (!ipath)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  // make lock empty
  *lock = '\0';
  *tmpname = '\0';

  snprintf(path,sizeof(path), "%s", ipath);

  // <TODO> Isn't there a libc function which does this </TODO>
  // Convert all periods to hyphens to allow us to create an extension.
  for(p = path; *p; p++)
  {
    if (*p == '.')
      *p = '-';
  }

  admin_ext = (iadmin_ext?iadmin_ext:BK_FILE_LOCK_ADMIN_EXTENSION);
  lock_ext = (ilock_ext?ilock_ext:BK_FILE_LOCK_EXTENSION);

  // I use strlen alot in this function, so compute and chache early.
  len = strlen(path);

  // Generate admin file name.
  snprintf(admin,sizeof(admin), "%s.%s", path, admin_ext);

  /*
   * Make sure that file name you have just created is exactly what you
   * want. Otherwise you might be trashing something (like, for instance,
   * the file you are trying to lock!)
   */
  if (strlen(admin) != len + strlen(admin_ext) + 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create lock admin file name correctly\n");
    goto error;
  }

  if ((fd = open(admin, O_CREAT, 0600)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "could not create the lock admin file: %s\n", strerror(errno));
    goto error;
  }
  close(fd);
  fd = -1;
  
  // Generate admin file lock file name.
  snprintf(lock, sizeof(lock), "%s.%s", path, lock_ext);

  // Again check that we have the name that we want
  if (strlen(lock) != len + strlen(lock_ext) + 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create lock file name correctly\n");
    goto error;
  }

  // Generate a uniqe string as a "link" name.
  // <TODO> mkstemp(3) is not good for NFS. use bk_mkstemp() when written.</TODO>
  snprintf(tmpname, MAXNAMLEN, "%s.XXXXXX", path);
  
  if ((fd = mkstemp(tmpname)) < 0 )
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create unique lock file name correcly\n");
    goto error;
  }
  
  if (link(tmpname, lock) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create lock link: %s\n", strerror(errno));
    goto error;
  }

  if (fstat(fd, &sb) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not stat temporary file: %s\n", strerror(errno));
    goto error;
  }
    
  if (sb.st_nlink != 2)
  {
    bk_error_printf(B, BK_ERR_ERR, "Incorrect number of links. Is: %d (should be 2)\n", sb.st_nlink);
    goto error;
  }
  close(fd);
  fd = -1;

  if (!(fla = fla_create(B)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate fla\n");
    goto error;
  }

  if (!(fla->fla_lock = strdup(lock)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup lock name: %s\n", strerror(errno));
    goto error;
  }
  
  if (!(fla->fla_admin = strdup(admin)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup admin name: %s\n", strerror(errno));
    goto error;
  }
  
  if (!(fla->fla_tmpname = strdup(tmpname)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not strdup tmpname name: %s\n", strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,fla);

 error:
  // Do not remove administrative file, even if you have created it.
  if (fd != -1) close(fd);
  if (fla) fla_destroy(B, fla);
  BK_RETURN(B,NULL);
}



/**
 * Unlock the admin file.
 *	@param B BAKA thread/global state.
 *	@param fl Lock info to use (and destroy).
 */
static void
unlock_admin_file(bk_s B, struct file_lock_admin *fla)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");

  if (!fla)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (fla->fla_lock && (unlink(fla->fla_lock) < 0))
  {
    if (errno != ENOENT)
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink lock file\n");
  }

  if (fla->fla_tmpname && (unlink(fla->fla_tmpname)) < 0)
  {
    if (errno != ENOENT)
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink tmp file\n");
  }


  // Do NOT unlock admin file. It gets used over and over. 
  
  fla_destroy(B, fla);

  BK_VRETURN(B);
}




/**
 * Allocate a fl
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a file_lock on success.
 */
static struct file_lock *
fl_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");
  struct file_lock *fl;
  
  if (!(BK_CALLOC(fl)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate fl: %s\n", strerror(errno));
    BK_RETURN(B,NULL);
  }
  BK_RETURN(B,fl);
}



/**
 * Destroy a fl
 *	@param B BAKA thread/global state.
 *	@param fl The @a file_lock to destroy.
 */
static void
fl_destroy(bk_s B, struct file_lock *fl)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");

  if (!fl)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (fl->fl_path)
    free(fl->fl_path);

  if (fl->fl_admin_ext)
    free(fl->fl_admin_ext);

  if (fl->fl_lock_ext)
    free(fl->fl_lock_ext);

  if (fl->fl_lock_id)
    free(fl->fl_lock_id);

  free(fl);
  BK_VRETURN(B);
}



/**
 * Create a @a file_lock_admin
 *
 *	@param B BAKA thread/global state.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a file_lock_admin on success.
 */
static struct file_lock_admin *
fla_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");
  struct file_lock_admin *fla;

  if (!(BK_CALLOC(fla)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate fla: %s\n", strerror(errno));
    BK_RETURN(B,NULL);    
  }
  BK_RETURN(B,fla);
}




/**
 * Destroy a @a file_lock_admin
 *
 *	@param B BAKA thread/global state.
 *	@param fla The @a file_lock_admin to destroy.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
fla_destroy(bk_s B, struct file_lock_admin *fla)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "file-plugin");

  if (!fla)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (fla->fla_admin)
    free(fla->fla_admin);
  
  if (fla->fla_lock)
    free(fla->fla_lock);

  if (fla->fla_tmpname)
    free(fla->fla_tmpname);

  free(fla);
  BK_VRETURN(B);
}
