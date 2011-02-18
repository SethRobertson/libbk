#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2007-2010";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2007-2010 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * All of the baka run public and private functions.
 */

#include <libbk.h>
#include "libbk_internal.h"


static void nuke_scandir_proclist(bk_s B, int num_procs, struct dirent **proclist, bk_flags flags);
static struct bk_procinfo *bpi_create(bk_s B);
static void bpi_destroy(bk_s B, struct bk_procinfo *bpi);


/**
 * Create a bpi
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>bpi</i> on success.
 */
static struct bk_procinfo *
bpi_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_procinfo *bpi = NULL;

  if (!BK_CALLOC(bpi))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate bpi: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bpi);
}



/**
 * Destroy a bpi
 *
 *	@param B BAKA thread/global state.
 *	@param bpi The @a bpi to nuke
 */
static void
bpi_destroy(bk_s B, struct bk_procinfo *bpi)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bpi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (bpi->bpi_exec_path)
    free(bpi->bpi_exec_path);

  if (bpi->bpi_comm)
    free(bpi->bpi_comm);

  if (bpi->bpi_cmdline)
    free(bpi->bpi_cmdline);

  if (bpi->bpi_env)
    bk_string_tokenize_destroy(B, bpi->bpi_env);

  free(bpi);

  BK_VRETURN(B);
}


#ifdef HAVE_PROCFS

/**
 * Filter for PID directory entries
 *	@param entry The current /proc directory entry .
 *	@return <i>1</i> if PID match.<br>
 *	@return <i>0</i> if not PID match.
 */
static int
pid_filter(const struct dirent *entry)
{
  pid_t pid;
  if (bk_string_atou32(NULL, entry->d_name, &pid, 0) < 0)
    return(0);

  return(1);
}



#define PROCDIR_FILE_CHUNK	1024

/**
 * Read a file into a string. /proc stores a lot of things are files.
 *
 *	@param B BAKA thread/global state.
 *	@param name Name of file to read.
 *	@param buf C/O buffer of the file contents.
 *	@param size C/O size of the buffer.
 *	@return <i>-1</i> on failure.<br>
 *	@return file <i>0</i> on success.
 */
static int
readfile(bk_s B, const char *name, char **bufp, off_t *sizep)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int fd = -1;
  char *contents = NULL;
  struct stat st;
  u_int filesize = 0;
  int nbytes;

  if (!name || !bufp || !sizep)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  *bufp = NULL;
  *sizep = -1;

  if ((fd = open(name, O_RDONLY)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open %s: %s\n", name, strerror(errno));
    goto error;
  }

  if (fstat(fd, &st) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not fstat %s: %s\n", name, strerror(errno));
    goto error;
  }

  if (!BK_CALLOC_LEN(contents, PROCDIR_FILE_CHUNK))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate string of size %u: %s\n", (u_int32_t)st.st_size, strerror(errno));
    goto error;
  }

  while ((nbytes = read(fd, contents+filesize, PROCDIR_FILE_CHUNK)) > 0)
  {
    filesize += nbytes;
    if (nbytes == PROCDIR_FILE_CHUNK)
    {
      char *tmp;
      if (!(tmp = realloc(contents, filesize + PROCDIR_FILE_CHUNK)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not realloc /proc file buffer: %s\n", strerror(errno));
	goto error;
      }
      contents = tmp;
      memset(contents+filesize, (char)0, PROCDIR_FILE_CHUNK);
    }
    else
    {
      break;
    }
  }

  if (nbytes < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not read from %s: %s\n", name, strerror(errno));
    goto error;
  }

  close(fd);
  fd = -1;

  *bufp = contents;
  *sizep = filesize;

  BK_RETURN(B, 0);

 error:
  if (contents)
    free(contents);

  if (fd != -1)
    close(fd);


  BK_RETURN(B, -1);
}



/**
 * Return a list of procinfo structures. This is an "exposed" dll because
 * callers will need to process the information in different ways.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return <i>stats list</i> on success.
 */
dict_h
bk_procinfo_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  dict_h bpi_list = NULL;
  struct dirent **proclist;
  int num_procs = 0;
  int proc_index;
  char *procdir_name = NULL;
  char *tmpname = NULL;
  char *tmpbuf = NULL;
  struct bk_procinfo *bpi = NULL;
  off_t tmpsz;
  char *comm = NULL;

  if (!(bpi_list = procinfo_list_create(NULL, NULL, DICT_UNORDERED)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create procinfo list: %s\n", procinfo_list_error_reason(NULL, NULL));
    goto error;
  }

  if ((num_procs = scandir("/proc", &proclist, pid_filter, versionsort)) < 0)
  {
    // Errno?
    bk_error_printf(B, BK_ERR_ERR, "Could not scan /proc for process directories\n");
    goto error;
  }

  for(proc_index = 0; proc_index < num_procs; proc_index++)
  {
    struct dirent *procdir = proclist[proc_index];
    int dummy_int;
    long dummy_long;
    u_long dummy_ulong;

    // Create a string with the /proc path to the current process.
    if (!(procdir_name = bk_string_alloc_sprintf(B, 0, 0, "/proc/%s", procdir->d_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create procdir name\n");
      goto error;
    }

    // Extract cmdline
    if (!(tmpname = bk_string_alloc_sprintf(B, 0, 0, "%s/environ", procdir_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create name of /proc environment file\n");
      goto error;
    }

    if (access(tmpname, R_OK) < 0)
    {
      /*
       * <TRICKY>
       * There is a race condition here where a process which existed when
       * the /proc was scanned has completed and gone away before its
       * information can be collected. In this case the environ file won't
       * exist to be read. Furthermore this race condition is not merely
       * hypothetical; it's loss has been frequently obvserved. Of course
       * there is even more serious race condition where a given process id
       * is *reused* between the time /proc is scanned and when this loop
       * occurs, but *that* RC *is* (I hope) purely hypothetical.
       */

      if ((errno == EACCES) || (errno == ENOENT))
	goto drop_procinfo;

      bk_error_printf(B, BK_ERR_ERR, "Could not access %s for reading: %s\n", tmpname, strerror(errno));
      goto error;

      }

    if (readfile(B, tmpname, &tmpbuf, &tmpsz) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Failed to obtain environment data from %s\n", tmpname);
      goto error;
    }

    free(tmpname);
    tmpname = NULL;

    // Create procinfo struct.
    if (!(bpi = bpi_create(B)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create proc info struct\n");
      goto error;
    }

    if (!(bpi->bpi_env = bk_string_tokenize_split(B, tmpbuf, 0, "\0", NULL, NULL, NULL, 0)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not extract environment from %s\n", tmpbuf);
      goto error;
    }

    free(tmpbuf);
    tmpbuf = NULL;


    // Extract PID
    if (bk_string_atou32(NULL, procdir->d_name, &(bpi->bpi_pid), 0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not convert proc directory name to pid\n");
      goto error;
    }

    // Extract cmdline
    if (!(tmpname = bk_string_alloc_sprintf(B, 0, 0, "%s/cmdline", procdir_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create name of /proc cmdline file\n");
      goto error;
    }

    if (readfile(B, tmpname, &tmpbuf, &tmpsz) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not read %s\n", tmpname);
      goto error;
    }

    free(tmpname);
    tmpname = NULL;

    bpi->bpi_cmdline = tmpbuf;
    tmpbuf = NULL;

    // Extract name of real process (ie not the symlink name if the original path was a symlink).
    if (!(tmpname = bk_string_alloc_sprintf(B, 0, 0, "%s/exe", procdir_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create name of /proc exec symlink\n");
      goto error;
    }

    if (!BK_CALLOC_LEN(tmpbuf, PATH_MAX+1))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate a buffer of size %d: %s\n", PATH_MAX + 1, strerror(errno));
      goto error;
    }

    if (readlink(tmpname, tmpbuf, PATH_MAX+1) < 0)
    {
      if (errno == ENOENT)
	goto drop_procinfo;
      bk_error_printf(B, BK_ERR_ERR, "Could not read symlink %s: %s\n", tmpname, strerror(errno));
      goto error;
    }

    // Since tmpbuf is probably way too long, store a shorter copy.
    if (!(bpi->bpi_exec_path = strdup(tmpbuf)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy string \"%s\": %s\n", tmpname, strerror(errno));
      goto error;
    }

    free(tmpbuf);
    tmpbuf = NULL;

    free(tmpname);
    tmpname = NULL;

    // Extract stat info
    if (!(tmpname = bk_string_alloc_sprintf(B, 0, 0, "%s/stat", procdir_name)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create name of /proc stat file\n");
      goto error;
    }

    if (readfile(B, tmpname, &tmpbuf, &tmpsz) < 0)
    {
      if (errno == ENOENT)
	goto drop_procinfo;
      bk_error_printf(B, BK_ERR_ERR, "Could not read %s\n", tmpname);
      goto error;
    }

    free(tmpname);
    tmpname = NULL;

    if (!BK_CALLOC_LEN(comm, tmpsz))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for comm\n");
      goto error;
    }

    sscanf(tmpbuf, "%d %s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld %lu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %lu %lu",
	   &bpi->bpi_pid,
	   comm,
	   &bpi->bpi_state,
	   &bpi->bpi_ppid,
	   &bpi->bpi_pgid,
	   &bpi->bpi_sid,
	   &bpi->bpi_tty_minor,
	   &dummy_int,				// tpgid
	   &bpi->bpi_proc_flags,
	   &dummy_ulong,			// minflt
	   &dummy_ulong,			// cminflt
	   &dummy_ulong,			// majflt
	   &dummy_ulong,			// cmajflt
	   &bpi->bpi_utime,
	   &bpi->bpi_stime,
	   &dummy_long,				// cutime
	   &dummy_long,				// cstime
	   &dummy_long,				// priority
	   &bpi->bpi_nice,
	   &bpi->bpi_num_threads,
	   &dummy_long,				// itrealvalue
	   &bpi->bpi_starttime,
	   &bpi->bpi_vsize,
	   &bpi->bpi_rss,
	   &dummy_ulong,			// rlim
	   &dummy_ulong,			// startcode
	   &dummy_ulong,			// endcode
	   &dummy_ulong,			// startstack
	   &dummy_ulong,			// kstkesp
	   &dummy_ulong,			// kstkeip
	   &dummy_ulong,			// signal
	   &dummy_ulong,			// blocked
	   &dummy_ulong,			// sigignore
	   &dummy_ulong,			// sigcatch
	   &bpi->bpi_wchan,
	   &dummy_ulong,			// nswap
	   &dummy_ulong,			// cnswap
	   &dummy_int,				// exit_signal
	   &dummy_int,				// processor
	   &dummy_ulong,			// rt_priority
	   &dummy_ulong				// policy
	   );

    // the "comm" field is surronded by parenthesis. Drop them.
    comm[strlen(comm)-1] = '\0';
    if (!(bpi->bpi_comm = strdup(comm+1)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not copy comm field: %s\n", strerror(errno));
      goto error;
    }

    free(comm);
    comm = NULL;

    if (procinfo_list_insert(bpi_list, bpi) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert new procinfo structure into list: %s\n", procinfo_list_error_reason(bpi_list, NULL));
      goto error;
    }
    bpi = NULL;

  drop_procinfo:
    free(procdir_name);
    procdir_name = NULL;

    if  (tmpname)
      free(tmpname);
    tmpname = NULL;

    if (tmpbuf)
      free(tmpbuf);
    tmpbuf = NULL;

    if (bpi)
      bpi_destroy(B, bpi);
    bpi = NULL;
  }

  nuke_scandir_proclist(B, num_procs, proclist, 0);

  BK_RETURN(B, bpi_list);

 error:
  if (bpi)
    bpi_destroy(B, bpi);

  if (bpi_list)
    bk_procinfo_destroy(B, bpi_list);

  if ((num_procs > 0) && proclist)
    nuke_scandir_proclist(B, num_procs, proclist, 0);

  if (tmpname)
    free(tmpname);

  if (procdir_name)
    free(procdir_name);

  if (tmpbuf)
    free(tmpbuf);

  if (comm)
    free(comm);

  BK_RETURN(B, NULL);
}



/**
 * Nuke the results from scandir.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
static void
nuke_scandir_proclist(bk_s B, int num_procs, struct dirent **proclist, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int proc_index;

  if (!proclist)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  for(proc_index = 0; proc_index < num_procs; proc_index++)
  {
    free(proclist[proc_index]);
  }
  free(proclist);
  BK_VRETURN(B);
}



/**
 * Destroy a list of procinfo structures
 *
 *	@param B BAKA thread/global state.
 *	@param bpi_list The procinfo list
 */
void
bk_procinfo_destroy(bk_s B, dict_h bpi_list)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_procinfo *bpi;

  if (!bpi_list)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  while(bpi = procinfo_list_minimum(bpi_list))
  {
    if (procinfo_list_delete(bpi_list, bpi) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not delete bpi from list: %s\n", procinfo_list_error_reason(bpi_list, NULL));
    }
    bpi_destroy(B, bpi);
  }
  procinfo_list_destroy(bpi_list);
  BK_VRETURN(B);
}


#else  /* (NOT) HAVE_PROCFS */

/**
 * Return a list of procinfo structures. This is an "exposed" dll because
 * callers will need to process the information in different ways.
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>NULL</i> on failure.<br>
 *	@return There is no success...
 */
dict_h
bk_procinfo_create(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_RETURN(B, NULL);
}



/**
 * Destroy a list of procinfo structures
 *
 *	@param B BAKA thread/global state.
 *	@param bpi_list The procinfo list
 */
void
bk_procinfo_destroy(bk_s B, dict_h bpi_list)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  BK_VRETURN(B);
}
#endif /* HAVE_PROCFS */
