#if !defined(lint) && !defined(__INSIGHT__)
static const char libbk__rcsid[] = "$Id: b_ringdir.c,v 1.1 2004/04/06 21:15:48 jtt Exp $";
static const char libbk__copyright[] = "Copyright (c) 2003";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
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
 *
 *
 * Implements a "Ring Directory" which is a directory where files are
 * created and written to in a sequential manner until the configured
 * number of files have been exhausted whereupon it truncates and write to
 * the first file again.
 *
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * Internal state for managing ring directories.
 */
struct bk_ring_directory
{
  bk_flags				brd_flags; ///< Everyone needs flags.
  off_t					brd_rotate_size; ///< The maximum size of a file.
  u_int32_t				brd_max_num_files; ///< The maximum number of files in directory.
  const char *				brd_directory;	///< Directory name;
  const char *				brd_pattern;	///< Pattern on which to base the file names.
  const char *				brd_path;	///< Full path with pattern.
  u_int32_t				brd_cur_file_num; ///< Index of current file.
  const char *				brd_cur_filename; ///< The current file we are updating.
  void *				brd_opaque; ///< User data.
  struct bk_ringdir_callbacks	brd_brc; ///< Callback structure.
};



/**
 * Private state for the "standard" ring directory implementation
 */
struct bk_ringdir_standard
{
  bk_flags	brs_flags;			///< Everyone needs flags.
  int		brs_fd;				///< Currently active fd
  const char *	brs_chkpnt_filename;		///< Name of checkpoint file.
  const char *	brs_cur_filename;		///< Current filename (for sanity check).
};


static struct bk_ring_directory *brd_create(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *pattern, struct bk_ringdir_callbacks *callbacks, bk_flags flags);
static void brd_destroy(bk_s B, struct bk_ring_directory *brd);
static const char *create_file_name(bk_s B, const char *pattern, u_int32_t cnt, bk_flags flags);

static void *standard_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, bk_flags flags);
static void standard_destroy(bk_s B, void *opaque, const char *directory, bk_flags flags);
static off_t standard_get_size(bk_s B, void *opaque, const char *filename, bk_flags flags);
static int standard_open(bk_s B, void *opaque, const char *filename, bk_flags flags);
static int standard_close(bk_s B, void *opaque, const char *filename, bk_flags flags);
static int standard_unlink(bk_s B, void *opaque, const char *filename, bk_flags flags);
static int standard_chkpnt(bk_s B, void *opaque, enum bk_ringdir_chkpnt_actions action, const char *directory, const char *pattern, u_int32_t *valuep, bk_flags flags);



/**
 * Callback definition for "standard" ring directory implementation
 */
struct bk_ringdir_callbacks bk_ringdir_standard_callbacks = 
{
  standard_init,
  standard_destroy,
  standard_get_size,
  standard_open,
  standard_close,
  standard_unlink,
  standard_chkpnt,
};



/**
 * Create a @a bk_ring_directory
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static struct bk_ring_directory *
brd_create(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *pattern, struct bk_ringdir_callbacks *callbacks, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = NULL;
  int finished;

  if (!directory || !rotate_size || !max_num_files || !callbacks)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!BK_CALLOC(brd))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate state for bk ring directory: %s\n", strerror(errno));
    goto error;
  }

  brd->brd_rotate_size = rotate_size;
  brd->brd_max_num_files = max_num_files;
  brd->brd_brc = *callbacks;			// Structure copy.
  brd->brd_flags = flags;

  if (rotate_size == BK_RINGDIR_GET_SIZE_ERROR)
  {
    bk_error_printf(B, BK_ERR_WARN, "rotate_size is too large. Lowering limit slightly\n");
    rotate_size = BK_RINGDIR_GET_SIZE_MAX;
  }

  if (!brd->brd_brc.brc_get_size || 
      !brd->brd_brc.brc_open || 
      !brd->brd_brc.brc_close || 
      !brd->brd_brc.brc_unlink)
  {
    bk_error_printf(B, BK_ERR_ERR, "Callback list is missing at least one required callback.\n");
    goto error;
  }

  // If we don't have a ckpnt callback, turn off checkpointing.
  if (!(brd->brd_brc.brc_chkpnt))
  {
    if (BK_FLAG_ISCLEAR(brd->brd_flags, BK_RINGDIR_FLAG_NO_CHECKPOINT))
      bk_error_printf(B, BK_ERR_WARN, "No check callback specified. Turning off checkpointing\n");
      
    BK_FLAG_SET(brd->brd_flags, BK_RINGDIR_FLAG_NO_CHECKPOINT);
  }
  
  if (!(brd->brd_directory = strdup(directory)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy directory name: %s\n", strerror(errno));
    goto error;
  }

  if (!pattern)
    pattern = "%d";

  if (!(brd->brd_pattern = strdup(pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy file name pattern: %s\n", strerror(errno));
    goto error;
  }

  // Nuke all trailing slashes
  do
  {
    finished = 1;
    if (brd->brd_directory[strlen(directory)-1] != '\\')
    {
      ((char *)brd->brd_directory)[strlen(directory)-1] = '\0';
      finished = 0;
    }
  } while(!finished);

  if (!(brd->brd_path = bk_string_alloc_sprintf(B, 0, 0, "%s%s", brd->brd_directory, pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create file name pattern: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,brd);  

 error:
  if (brd)
    brd_destroy(B, brd);

  BK_RETURN(B,NULL);  
}



/**
 * Destroy a @a bk_ring_directory
 *
 *	@param B BAKA thread/global state.
 *	@param brd The @a bk_ring_directory to nuke.
 */
static void
brd_destroy(bk_s B, struct bk_ring_directory *brd)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (brd->brd_directory)
    free((char *)brd->brd_directory);
  
  if (brd->brd_pattern)
    free((char *)brd->brd_pattern);
  
  if (brd->brd_path)
    free((char *)brd->brd_path);
  
  if (brd->brd_cur_filename)
    free((char *)brd->brd_cur_filename);

  free(brd);

  BK_VRETURN(B);  
}




/**
 * Initialize a ring directory. Specify the directory name, the file name
 * pattern (if desired), and the maximum files in directory. If the
 * directory does not exist it will be created unless caller demurs.
 *
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return a new @a bk_rindir_t on success.
 */
bk_ringdir_t
bk_ringdir_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, struct bk_ringdir_callbacks *callbacks, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = NULL;

  if (!directory || !file_name_pattern || !rotate_size || !max_num_files || !callbacks)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(brd = brd_create(B, directory, rotate_size, max_num_files, file_name_pattern, callbacks, flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create bk ring directory state\n");
    goto error;
  }

  if (brd->brd_brc.brc_init && !(brd->brd_opaque = (*brd->brd_brc.brc_init)(B, directory, rotate_size, max_num_files, file_name_pattern, flags & BK_RINGDIR_FLAG_DO_NOT_CREATE)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create user state\n");
    goto error;
  }

  // Recover checkpoint unless caller sez no.
  if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_NO_CONTINUE) && brd->brd_brc.brc_chkpnt)
  {
    int ret;
    bk_flags ringdir_open_flags = 0;
    
    switch(ret = (*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionRecover, brd->brd_directory, brd->brd_pattern, &brd->brd_cur_file_num, 0))
    {
    case -1:  // Errorr
      bk_error_printf(B, BK_ERR_ERR, "Failed to recover checkpoint value\n");
      goto error;
      
    case 0:  // Checkpoint found. Open for append
      ringdir_open_flags = BK_RINGDIR_FLAG_OPEN_APPEND;
      break;
      
    case 1: // Checkpoint not found. Do normal truncate open.
      break;

    default:
      bk_error_printf(B, BK_ERR_ERR, "Unknown return value from open callback: %d\n", ret);
      break;
    }

    if (!(brd->brd_cur_filename = create_file_name(B, brd->brd_path, brd->brd_cur_file_num, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create filename from pattern\n");
      goto error;
    }
      
    if ((*brd->brd_brc.brc_open)(B, brd->brd_opaque, brd->brd_cur_filename, ringdir_open_flags))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s\n", brd->brd_cur_filename);
      goto error;
    }

    if (BK_FLAG_ISSET(ringdir_open_flags, BK_RINGDIR_FLAG_OPEN_APPEND))
    {
      // Check that we did just *happen* to check point a file which is full.
      if (bk_ringdir_rotate(B, brd, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to perform rotate check\n");
	goto error;
      }
    }
  }
    
  BK_RETURN(B,(bk_ringdir_t)brd);  

 error:
  if (brd)
    brd_destroy(B, brd);
  
  BK_RETURN(B,NULL);  
}



/**
 * Destroy a ring directory.
 *
 * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: Empty directory and ask callback to
 * nuke directory.
 *
 * BK_RINGDIR_FLAG_NO_NUKE: Override BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY
 * flag in @a brd
 *
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 */
void
bk_ringdir_destroy(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  const char *filename = NULL;
  u_int32_t cnt;
  bk_flags destroy_callback_flags = 0;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }
  
  if ((BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_NO_NUKE) &&
       BK_FLAG_ISSET(brd->brd_flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY)) ||
      BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY))
  {
    BK_FLAG_SET(destroy_callback_flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY);
    for(cnt = 0; cnt < brd->brd_max_num_files; cnt++)
    {
      if (!(filename = create_file_name(B, brd->brd_path, cnt, 0)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create filename\n");
	goto error;
      }

      if ((*brd->brd_brc.brc_unlink)(B, brd->brd_opaque, filename, 0) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not unlink %s\n", filename);
      }
      
      free((char *)filename);
      filename = NULL;
    }
    
  if (brd->brd_brc.brc_chkpnt)
    (*brd->brd_brc.brc_chkpnt)(B, brd->brd_opaque, BkRingDirChkpntActionDelete, brd->brd_directory, brd->brd_pattern, NULL, 0);
  }

  if (brd->brd_brc.brc_destroy)
    (*brd->brd_brc.brc_destroy)(B, brd->brd_opaque, brd->brd_directory, destroy_callback_flags);
  
  brd_destroy(B, brd);

  BK_VRETURN(B);  

 error:
  if (filename)
    free((char *)filename);

  BK_VRETURN(B);  
}



/**
 * Check if a rotate is needed and do so if it is.
 *
 *
 * BK_RINGDIR_FLAG_FORCE_ROTATE: Rotate regardless of file "size"
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bk_ringdir_rotate(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;
  int need_rotate = 0;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_FORCE_ROTATE))
  {
    need_rotate = 1;
  }
  else
  {
    off_t size;
    
    if ((size = (*brd->brd_brc.brc_get_size)(B, brd->brd_opaque, brd->brd_cur_filename, 0)) == BK_RINGDIR_GET_SIZE_ERROR)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not obtain size of %s\n", brd->brd_cur_filename);
      goto error;
    }
    
    if (size >= brd->brd_rotate_size)
      need_rotate = 1;
  }
      
  if (need_rotate)
  {
    if ((*brd->brd_brc.brc_close)(B, brd->brd_opaque, brd->brd_cur_filename, 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not close %s\n", brd->brd_cur_filename);
      goto error;
    }

    free((char *)brd->brd_cur_filename);
    brd->brd_cur_filename = NULL;

    brd->brd_cur_file_num = (brd->brd_cur_file_num + 1) % brd->brd_max_num_files;

    if (!(brd->brd_cur_filename = create_file_name(B, brd->brd_path, brd->brd_cur_file_num, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create filename from pattern\n");
      goto error;
    }

    if ((*brd->brd_brc.brc_open)(B, brd->brd_opaque, brd->brd_cur_filename, 0))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s\n", brd->brd_cur_filename);
      goto error;
    }
  }
  
  BK_RETURN(B,0);  

 error:
  BK_RETURN(B,-1);  
}



/**
 * Obtain one's private data from the ring directory handle.
 *
 *	@param B BAKA thread/global state.
 *	@param brdh Ring directory handle.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return @a private_data on success.
 */
void *
bk_rindir_get_private_data(bk_s B, bk_ringdir_t brdh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ring_directory *brd = (struct bk_ring_directory *)brdh;

  if (!brd)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B,brd->brd_opaque);  
}



/**
 * Create a filename based on the directoy, pattern, and cnt.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>filename</i> on success.
 */
static const char *
create_file_name(bk_s B, const char *pattern, u_int32_t cnt, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *filename;

  if (!pattern)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!(filename = bk_string_alloc_sprintf(B, 0, 0, pattern, cnt)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create file name\n");
    goto error;
  }

  BK_RETURN(B,filename);  

 error:
  if (filename)
    free(filename);
  BK_RETURN(B,NULL);  
}



/**
 *
 * Allocate and initialize private data for this particular
 * implementation of a ring directory. The valued returnd from this
 * function will the @a opaque argument in all the other functions in the
 * API. This function is OPTIONAL but if it declared then it must return
 * a non NULL value for success even if there is no private state
 * created. Using (void *)1 works fine, but irriates the heck out of
 * memory checkers. We suggest returning the function name (ie function
 * pointer) instead. For your convenience, you are passed all the values
 * which the caller passed to @a bk_ringdir_init(), you may use them how
 * you will.
 *
 *	@param B BAKA thread/global state.
 *	@param directory The "path" to the ring directory;
 *	@param rotate_size How big a file may grow before rotation.
 *	@param max_num_files How many files in the ring dir before reuse.
 *	@param file_name_pattern The sprintf-like pattern for creating names in the directory.
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_DO_NOT_CREATE: If you passed this flag, do not create
 * the directory if it doesn't exist; return an error instead.
 *
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>private_data</i> on success.
 */
static void *
standard_init(bk_s B, const char *directory, off_t rotate_size, u_int32_t max_num_files, const char *file_name_pattern, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = NULL;
  struct stat st; 
  int does_not_exist = 0;
  int created_directory = 1;
  bk_flags destroy_flags = 0;


  if (!directory || !file_name_pattern)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }
  
  if (!BK_CALLOC(brs))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate standard ringdir state: %s\n", strerror(errno));
    goto error;
  }
  brs->brs_fd = -1;

  if (!(brs->brs_chkpnt_filename = bk_string_alloc_sprintf(B, 0, 0, "%s%s", directory, file_name_pattern)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create checkpoint file name\n");
    goto error;
  }

  if (stat(directory, &st) < 0)
  {
    if (errno == ENOENT)
      does_not_exist = 1;
    else
      bk_error_printf(B, BK_ERR_ERR, "Could not stat %s: %s\n", directory, strerror(errno));
  }

  if (does_not_exist)
  {
    if (BK_FLAG_ISCLEAR(flags, BK_RINGDIR_FLAG_DO_NOT_CREATE))
    {
      if (mkdir(directory, 0777) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create %s: %s\n", directory, strerror(errno));
	goto error;
      }
      created_directory = 1;
    }
    else
    {
      bk_error_printf(B, BK_ERR_ERR, "%s does not exist and caller requested not to create it\n", directory);
      goto error;
    }
  }
  else
  {
    if (!S_ISDIR(st.st_mode))
    {
      bk_error_printf(B, BK_ERR_ERR, "%s exists but is not a directory\n", directory);
      goto error;
    }
  }

  BK_RETURN(B,brs);  

 error:
  if (brs)
  {
    if (created_directory)
    {
      destroy_flags = BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY;
    }
    else
    {
      directory = NULL;
    }
    standard_destroy(B, brs, directory, destroy_flags);
  }
  
  BK_RETURN(B,NULL);  
}



/**
 * Create a @a bk_ring_directory
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY: If you passed this flag, the
 * caller is requesting that you destroy the directory. You are not
 * obliged to honor this, but it's a good idea.
 */
static void
standard_destroy(bk_s B, void *opaque, const char *directory, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY) && !directory))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_NUKE_DIR_ON_DESTROY))
  {
    // If we get this flag the directory should be empty, it's just up to us to remove it.
    if (rmdir(directory) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not remove directory %s: %s\n", directory, strerror(errno));
      goto error;
    }
  }

  if (brs->brs_chkpnt_filename)
    free((char *)brs->brs_chkpnt_filename);

  BK_VRETURN(B);  

 error:
  BK_VRETURN(B);  
  
}



/**
 * Return the "size" of the current file. You may define size in any
 * manner you chose. The only requirement is that the value returned from
 * this function be greater than or equal to that of @a rotate_size (from
 * the init function) when you desire rotation to occur. The macro
 * BK_RINGDIR_GET_SIZE_MAX is provided as a convenience value for the
 * largest legal return value from this function. 
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *	@return <i>BK_RINGDIR_GET_SIZE_ERROR</i> on failure.<br>
 *	@return <i>non-negative</i> on success.
 */
static off_t
standard_get_size(bk_s B, void *opaque, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  struct stat st;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (fstat(brs->brs_fd, &st) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not stat %s: %s\n", filename, strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,st.st_size);  

 error:
  BK_RETURN(B,BK_RINGDIR_GET_SIZE_ERROR);
}



/**
 * Open a new file by whatever method is appropriate for your implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *
 * BK_RINGDIR_FLAG_OPEN_APPEND: If you are passed this flag you should
 * open the file in append mode (ie we are starting up from a
 * checkpointed location).
 *
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
standard_open(bk_s B, void *opaque, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  int open_flags = O_WRONLY | O_LARGEFILE;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (BK_FLAG_ISSET(flags, BK_RINGDIR_FLAG_OPEN_APPEND))
    open_flags |= O_APPEND;
  else
    open_flags |= O_CREAT | O_TRUNC;
  

  if ((brs->brs_fd = open(filename, open_flags)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s for writing: %s\n", filename, strerror(errno));
    goto error;
  }

  if (!(brs->brs_cur_filename = strdup(filename)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not copy filename: %s\n", strerror(errno));
    goto error;
  }

  BK_RETURN(B,0);  

 error:
  standard_close(B, brs, filename, 0);
  BK_RETURN(B,-1);  
}



/**
 * Close a file in the manner consistent with your ringdir implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
standard_close(bk_s B, void *opaque, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (brs->brs_cur_filename && brs->brs_fd != -1)
  {
    if (!BK_STREQ(brs->brs_cur_filename, filename))
      bk_error_printf(B, BK_ERR_ERR, "Requsted file != cached file name. Closing cached descriptor\n");
  }

  if (brs->brs_fd != -1)
    close(brs->brs_fd);
  brs->brs_fd = -1;

  if (brs->brs_cur_filename)
    free((char *)brs->brs_cur_filename);
  brs->brs_cur_filename = NULL;

  BK_RETURN(B,0);  
}



/**
 * Remove a file in the manner consistent with your ringdir implementation.
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param directory The directory you may be asked to nuke.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
standard_unlink(bk_s B, void *opaque, const char *filename, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;

  if (!brs || !filename)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }
  
  if (unlink(filename))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not unlink %s: %s\n", filename, strerror(errno));
    goto error;
  }
  
  BK_RETURN(B,0);  

 error:
  BK_RETURN(B,-1);  
}



/**
 * Do checkpointing managment. You will be called with one of the actions: 
 *	BkRingDirChkpntActionChkpnt: 	Save value to checkpoint state "file"
 *	BkRingDirChkpntActionRecover: 	Recover checkpoint value from state "file"
 *	BkRingDirChkpntActionDelete: 	Delete the checkpoint state "file"
 *
 * The ring directory name and the file name pattern are supplied to you
 * for your convience as your checkpoint key will almost certainly need
 * to be some function of these to values.
 *
 * The value of @a valuep depends on the action. If saving then *valuep
 * is the copy in value to save. If recoving then *valuep is the copy out
 * value you need to update. If deleting, then @a valuep is NULL.
 *
 *
 *	@param B BAKA thread/global state.
 *	@param opaque Your private data.
 *	@param action The action to take (described above).
 *	@param directory The directory you may be asked to nuke.
 *	@param pattern The file pattern.
 *	@param valuep The file pattern.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static int
standard_chkpnt(bk_s B, void *opaque, enum bk_ringdir_chkpnt_actions action, const char *directory, const char *pattern, u_int32_t *valuep, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_ringdir_standard *brs = (struct bk_ringdir_standard *)opaque;
  int fd = -1;
  int len;
  u_int32_t value;
  int ret;

  if (!brs || !directory || !pattern || (action != BkRingDirChkpntActionDelete && !valuep))
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  switch(action)
  {
  case BkRingDirChkpntActionChkpnt:
    value = htonl(*valuep);

    if ((fd = open(brs->brs_chkpnt_filename, O_WRONLY)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s for reading: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }

    len = sizeof(value);
    do 
    {
      if ((ret = write(fd, ((char *)&value)+sizeof(value)-len, len)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to read out check value: %s\n", strerror(errno));
	goto error;
      }
      len -= ret;
    } while(len);

    close(fd);
    fd = -1;

    break;

  case  BkRingDirChkpntActionRecover:
    // Check for file existence. 
    if (access(brs->brs_chkpnt_filename, F_OK) < 0)
    {
      if (errno == ENOENT) // Not found is OK, but returns 1.
	BK_RETURN(B,1);      
	
      bk_error_printf(B, BK_ERR_ERR, "Could not access %s: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }

    if ((fd = open(brs->brs_chkpnt_filename, O_RDONLY)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not open %s for reading: %s\n", brs->brs_chkpnt_filename, strerror(errno));
      goto error;
    }
    
    len = sizeof(*valuep);
    do 
    {
      if ((ret = read(fd, valuep+sizeof(*valuep)-len, len)) < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Failed to read out check value: %s\n", strerror(errno));
	goto error;
      }
      
      len -= ret;
    } while (len);

    close(fd);
    fd = -1;

    *valuep = ntohl(*valuep);
    break;

  case BkRingDirChkpntActionDelete:
    if (unlink(brs->brs_chkpnt_filename) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not unlink: %s\n", strerror(errno));
      goto error;
    }
    break;
  }

  BK_RETURN(B,0);  

 error:
  if (fd != -1)
    close(fd);

  BK_RETURN(B, -1);  
}
