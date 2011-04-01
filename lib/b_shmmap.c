#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2011 The Authors. All rights reserved.
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
 *
 * Implementation of shared memory segment creation and mapping
 */

#include <libbk.h>
#include <sys/ipc.h>
#include <sys/shm.h>



static struct bk_shmmap *bk_shmmap_int(bk_s B, const char *name, const char *myname, u_short max_clients, off_t size, mode_t mode, void *addr, u_int fresh, bk_flags flags);
static void *bk_shmmap_manage_thread_th(bk_s B, void *opaque);



/**
 * Create a new posix shared memory segment
 *
 * @param B BAKA World
 * @param name POSIX shm pathname
 * @param max_clients Maximum number of clients which may attach
 * @param size Desired size of shm segment (exclusive of SHM overhead)
 * @param mode Desired local domain permissions of shm segment
 * @param addr Desired address in virtual memory (zero will pick one)
 * @param fresh How fresh does mgmt interface need to be (0 for default)
 * @param flags none
 * @return NULL on error or existing segment
 * @return shmmap on success
 */
struct bk_shmmap *bk_shmmap_create(bk_s B, const char *name, u_short max_clients, off_t size, mode_t mode, void *addr, u_int fresh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!name || size < 1 || max_clients < 1)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bk_shmmap_int(B, name, NULL, max_clients, size, mode, addr, fresh, flags));
}



/**
 * Attach to an existing posix shared memory segment
 *
 * Will block waiting for live shmmap creator to respond
 * (if shmmap creator goes dead, will fail)
 *
 * @param B BAKA World
 * @param shmname POSIX shm pathname
 * @param myname Description of person attaching
 * @return NULL on error (including inability to map to correct address)
 * @return (void *)-1 on retryable error
 * @return shmmap on success
 */
struct bk_shmmap *bk_shmmap_attach(bk_s B, const char *name, const char *myname, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!name || !myname)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, bk_shmmap_int(B, name, myname, 0, 0, 0, NULL, 0, flags));
}



/**
 * Internal create/attach a new posix shared memory segment
 *
 * @param B BAKA World
 * @param name POSIX shm pathname
 * @param myname Name of attacher
 * @param max_clients Maximum number of clients which may attach--or zero for attach
 * @param size Desired size of shm segment--or zero for attach
 * @param mode Desired local domain permissions of shm segment
 * @param addr Desired address in virtual memory (zero will pick one)--ignored for attach
 * @param fresh How fresh does mgmt interface need to be (0 for default)
 * @param flags none
 * @return NULL on error
 * @return (void *)-1 on retryable error
 * @return shmmap on success
 */
static struct bk_shmmap *bk_shmmap_int(bk_s B, const char *name, const char *myname, u_short max_clients, off_t size, mode_t mode, void *addr, u_int fresh, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_shmmap *shmmap = NULL;
  int misc_flags = 0;
  u_int mgmt_size = 0;
  int attach = !size;
  void *errret = (void *)-1;
  struct mq_attr mqattr;

  if (!name)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(shmmap = calloc(1, sizeof(*shmmap))))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate shmmap ref: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }
  shmmap->sm_shmfd = -1;

  if (!(shmmap->sm_name = strdup(name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate shmmap name: %s\n", strerror(errno));
    errret = NULL;
    goto error;
  }

  mqattr.mq_flags = 0;
  mqattr.mq_maxmsg = max_clients||1;
  mqattr.mq_msgsize = sizeof(struct bk_shmmap_cmds);
  mqattr.mq_curmsgs = 0;

  if (!attach)
  {
    mq_unlink(name);
    shmmap->sm_creatorcmds = mq_open(name, O_RDWR|O_CREAT|O_EXCL, mode, &mqattr);
  }
  else
  {
    // Potentially recoverable.  Should we have a retry option?
    shmmap->sm_creatorcmds = mq_open(name, O_WRONLY, mode, &mqattr);
  }

  if (shmmap->sm_creatorcmds < 0)
  {
    if (!attach || errno != ENOENT)
      errret = NULL;
    bk_error_printf(B, BK_ERR_ERR, "Could not open/create command message queue: %s\n", strerror(errno));
    goto error;
  }

  // Try it unconditionally at first
  if (!attach)
  {
    shm_unlink(name);

    misc_flags = O_RDWR|O_CREAT|O_EXCL;
  }
  else
  {
    misc_flags = O_RDWR;
  }

  // Potentially recoverable (for attachers)
  if ((shmmap->sm_shmfd = shm_open(name, misc_flags, mode)) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not open/create shared memory segment: %s\n", strerror(errno));
    if (!attach)
      errret = NULL;
    goto error;
  }

  misc_flags = MAP_SHARED;
  if (!attach)
  {
    mgmt_size = sizeof(struct bk_shmmap_header)+sizeof(struct bk_shmmap_client)*max_clients;
    if (mgmt_size & 7)
    {
      mgmt_size += 7;
      mgmt_size &= ~7;
    }

    size += mgmt_size;

    // Pick a size
    if (ftruncate(shmmap->sm_shmfd, size))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not set size (%lld) of shared memory segment: %s\n", (long long)size, strerror(errno));
      errret = NULL;
      goto error;
    }

    if (addr)
      misc_flags |= MAP_FIXED;
  }
  else
  {
    /*
     * Perform a temporary mapping to read the header in order to find
     * the correct address and size we should be using
     */
    size = sizeof(struct bk_shmmap_header);
    if ((shmmap->sm_addr = mmap(NULL, sizeof(struct bk_shmmap_header), PROT_READ, MAP_SHARED, shmmap->sm_shmfd, 0)) == MAP_FAILED)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not memory map shared memory segment: %s\n", strerror(errno));
      errret = NULL;
      goto error;
    }
    // Should we have a O_NDELAY option? Or failure after N attempts?
    while (shmmap->sm_addr->sh_state != BK_SHMMAP_READY && shmmap->sm_addr->sh_state != BK_SHMMAP_CLOSE)
      sleep(1);

    // Potentially recoverable error
    if (shmmap->sm_addr->sh_state != BK_SHMMAP_READY)
    {
      bk_error_printf(B, BK_ERR_ERR, "Shared memory segment has been closed\n");
      goto error;
    }

    addr = shmmap->sm_addr->sh_addr;
    size = shmmap->sm_addr->sh_size;

    if (munmap(shmmap->sm_addr, sizeof(struct bk_shmmap_header)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Huh? Cannot unmap temporary memory segment\n");
      errret = NULL;
      goto error;
    }

    misc_flags |= MAP_FIXED;
  }

  // Memory map it
  if ((shmmap->sm_addr = mmap(addr, size, PROT_READ|PROT_WRITE, misc_flags, shmmap->sm_shmfd, 0)) == MAP_FAILED)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not memory map shared memory segment: %s\n", strerror(errno));
    errret = NULL;
    goto error;
  }
  if (addr && addr != shmmap->sm_addr)
  {
    bk_error_printf(B, BK_ERR_ERR, "Memory map did not return the requested address: %s\n", strerror(errno));
    errret = NULL;
    goto error;
  }

  if (!attach)
  {
    // Initialize the new segment
    memset(shmmap->sm_addr, 0, mgmt_size);
    shmmap->sm_addr->sh_addr = shmmap->sm_addr;
    shmmap->sm_addr->sh_user = ((u_char *)shmmap->sm_addr)+mgmt_size;
    shmmap->sm_addr->sh_size = size;
    shmmap->sm_addr->sh_usersize = size - mgmt_size;
    shmmap->sm_addr->sh_creatortime = time(NULL);
    shmmap->sm_addr->sh_fresh = fresh?fresh:(u_int)atoi(BK_GWD(B, "bk_shmmap_fresh", BK_SHMMAP_DEFAULT_FRESH));
    shmmap->sm_addr->sh_numclients = max_clients;
    shmmap->sm_addr->sh_state = BK_SHMMAP_READY;
  }
  else
  {
    int x;
    struct bk_shmmap_cmds bop;

    while ((x = BK_SHMMAP_VALIDATE(shmmap)) != 1)
    {
      if (x < 0)
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not validate new shared memory segment attach\n");
	errret = NULL;
	goto error;
      }
      // Waiting for ready--should we time out?
      sleep(1);
    }

    bop.bsc_pid = getpid();
    strncpy(bop.bsc_name, myname,BK_SHMMAP_MAXCLIENTNAME);

    // See if there is a stale registration for me (and zap it)
    for(x=0;x<shmmap->sm_addr->sh_numclients;x++)
    {
      while (shmmap->sm_addr->sh_client[x].su_pid == getpid() && !strncmp(shmmap->sm_addr->sh_client[x].su_name, myname, BK_SHMMAP_MAXCLIENTNAME) &&
	     shmmap->sm_addr->sh_client[x].su_state != BK_SHMMAP_USER_STATEEMPTY && BK_SHMMAP_ISFRESH(shmmap))
      {
	// We are assuming somehow this same process has a stale allocation.  Zap it.
	bop.bsc_op = bk_shmmap_op_detach;
	mq_send(shmmap->sm_creatorcmds, (void *)&bop, sizeof(bop), 0);
	sleep(1);
      }
    }

    if (shmmap->sm_addr->sh_numattach >= shmmap->sm_addr->sh_numclients)
    {
      bk_error_printf(B, BK_ERR_ERR, "No free slots for this client\n");
      errret = NULL;
      goto error;
    }

    // Request a slot
    bop.bsc_op = bk_shmmap_op_attach;
    mq_send(shmmap->sm_creatorcmds, (void *)&bop, sizeof(bop), 0);

    while (!shmmap->sm_userbucket && shmmap->sm_addr->sh_numattach < shmmap->sm_addr->sh_numclients && BK_SHMMAP_ISFRESH(shmmap))
    {
      for(x=0;x<shmmap->sm_addr->sh_numclients;x++)
      {
	if (shmmap->sm_addr->sh_client[x].su_pid == getpid() && !strncmp(shmmap->sm_addr->sh_client[x].su_name, myname, BK_SHMMAP_MAXCLIENTNAME) &&
	    shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATEPREP)
	{
	  shmmap->sm_userbucket = &(shmmap->sm_addr->sh_client[x]);
	  shmmap->sm_userbucket->su_state = BK_SHMMAP_USER_STATEINIT;
	  shmmap->sm_userbucket->su_clienttime = time(NULL);
	  break;
	}
      }
      sleep(1);
    }
    if (!shmmap->sm_userbucket)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not find free slot for myself before shmmap went stale\n");
      errret = NULL;
      goto error;
    }
  }

  BK_RETURN(B, shmmap);

 error:
  if (shmmap)
  {
    if (shmmap->sm_name)
      free(shmmap->sm_name);
    if (shmmap->sm_userbucket)
      shmmap->sm_userbucket->su_state = BK_SHMMAP_USER_STATEEMPTY;
    if (shmmap->sm_addr)
    {
      munmap(shmmap->sm_addr, size);
    }
    if (shmmap->sm_shmfd >= 0)
    {
      close(shmmap->sm_shmfd);
      if (!attach)
	shm_unlink(name);
    }
    if (shmmap->sm_creatorcmds >= 0)
    {
      mq_close(shmmap->sm_creatorcmds);
      if (!attach)
	mq_unlink(name);
    }
    free(shmmap);
  }
  BK_RETURN(B, errret);
}



/**
 * Destroy (by creator) or detach (by others) a shared memory segment
 *
 * Creator detaching is an error.
 * Other destroying will be converted to a detach
 *
 * @param B BAKA World
 * @param shmmap Shared memory map structure
 * @param flags
 */
void bk_shmmap_destroy(bk_s B, struct bk_shmmap *shmmap, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_shmmap_cmds bop;

  if (!shmmap)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  if (shmmap->sm_userbucket)
  {
    shmmap->sm_userbucket->su_state = BK_SHMMAP_USER_STATECLOSE;
    bop.bsc_pid = shmmap->sm_userbucket->su_pid;
    strncpy(bop.bsc_name, shmmap->sm_userbucket->su_name,BK_SHMMAP_MAXCLIENTNAME);
    bop.bsc_op = bk_shmmap_op_detach;
  }
  else
  {
    bop.bsc_op = bk_shmmap_op_destroy;
    shmmap->sm_addr->sh_state = BK_SHMMAP_CLOSE;
  }

  if (shmmap->sm_addr)
  {
    // Mark as going down
    munmap(shmmap->sm_addr, shmmap->sm_addr->sh_size);
  }

  if (shmmap->sm_shmfd >= 0)
    close(shmmap->sm_shmfd);

  if (shmmap->sm_creatorcmds >= 0)
    if (shmmap->sm_userbucket)
      mq_close(shmmap->sm_creatorcmds);

  // manage needs to keep the structure around
  if (shmmap->sm_userbucket)
  {
    if (shmmap->sm_name)
      free(shmmap->sm_name);
    free(shmmap);
  }

  mq_send(shmmap->sm_creatorcmds, (void *)&bop, sizeof(bop), 0);

  BK_VRETURN(B);
}



/**
 * Fire off a thread to manage shared memory segment (respond to commands, etc)
 *
 * Thread does not return until shmmem destruction
 *
 * @param B BAKA World
 * @param shmmap Shared memory map structure
 * @return pthread_t* On thread creation and presumed subsequent management
 * @return NULL On failure
 */
pthread_t *bk_shmmap_manage_thread(bk_s B, struct bk_shmmap *shmmap)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  pthread_t *ret;

  if (!shmmap || !B)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(ret = bk_general_thread_create(B, "bk_shmmap_manage_thread", bk_shmmap_manage_thread_th, shmmap, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not fire off shmmap manage thread\n");
    BK_RETURN(B, NULL);
  }

  BK_RETURN(B, ret);
}



/* Internal thread wrapper */
static void *bk_shmmap_manage_thread_th(bk_s B, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_shmmap_manage(B, opaque, 0);
  BK_RETURN(B, NULL);
}



/**
 * Manage shared memory segment (respond to commands, etc)
 *
 * Does not return until shmmem destruction (unless you execute in poll mode)
 * Typically run in a dedicated thread.
 *
 * @param B BAKA World
 * @param shmmap Shared memory map structure
 * @param flags BK_SHMMEM_MANAGE_POLL
 */
void bk_shmmap_manage(bk_s B, struct bk_shmmap *shmmap, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_shmmap_cmds bop;
  struct timespec deltatime;

  if (!shmmap)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_VRETURN(B);
  }

  deltatime.tv_sec = shmmap->sm_addr->sh_fresh / 2;
  if (shmmap->sm_addr->sh_fresh & 1)
    deltatime.tv_nsec = 500000000;

  while (1)
  {
    struct timeval tvtime;
    struct timespec tstime;
    gettimeofday(&tvtime, NULL);

    shmmap->sm_addr->sh_creatortime = tvtime.tv_sec;

    if (BK_FLAG_ISSET(flags, BK_SHMMAP_MANAGE_POLL))
    {
      tstime.tv_sec = 0;
      tstime.tv_nsec = 0;
    }
    else
    {
      tstime.tv_sec = tvtime.tv_sec + deltatime.tv_sec;
      tstime.tv_nsec = tvtime.tv_usec * 1000 + deltatime.tv_nsec;
      BK_TS_RECTIFY(&tstime);
    }

    if (mq_timedreceive(shmmap->sm_creatorcmds, (char *)&bop, sizeof(bop), NULL, &tstime) < 0)
    {
      if (errno == ETIMEDOUT)
      {
	if (BK_FLAG_ISSET(flags, BK_SHMMAP_MANAGE_POLL))
	  break;
	continue;
      }
      bk_error_printf(B, BK_ERR_ERR, "shmmap mq receive failed: %s\n", strerror(errno));
      BK_VRETURN(B);
    }

    if (bop.bsc_op == bk_shmmap_op_destroy)
    {
      shm_unlink(shmmap->sm_name);
      mq_unlink(shmmap->sm_name);
      free(shmmap->sm_name);
      free(shmmap);
      BK_VRETURN(B);
    }
    if (bop.bsc_op == bk_shmmap_op_detach)
    {
      int x;
      for(x=0;x<shmmap->sm_addr->sh_numclients;x++)
      {
	if (shmmap->sm_addr->sh_client[x].su_pid == bop.bsc_pid && !strncmp(shmmap->sm_addr->sh_client[x].su_name, bop.bsc_name, BK_SHMMAP_MAXCLIENTNAME))
	{
	  shmmap->sm_addr->sh_client[x].su_state = BK_SHMMAP_USER_STATEEMPTY;
	  if (shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATEPREP ||
	      shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATEINIT ||
	      shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATEREADY ||
	      shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATECLOSE)
	    shmmap->sm_addr->sh_numattach--;
	}
      }
    }
    if (bop.bsc_op == bk_shmmap_op_attach)
    {
      int x;
      for(x=0;x<shmmap->sm_addr->sh_numclients;x++)
      {
	if (shmmap->sm_addr->sh_client[x].su_state == BK_SHMMAP_USER_STATEEMPTY)
	{
	  shmmap->sm_addr->sh_client[x].su_pid = bop.bsc_pid;
	  strncpy(shmmap->sm_addr->sh_client[x].su_name, bop.bsc_name, BK_SHMMAP_MAXCLIENTNAME);
	  shmmap->sm_addr->sh_client[x].su_state = BK_SHMMAP_USER_STATEPREP;
	  shmmap->sm_addr->sh_numattach++;
	  break;
	}
      }
    }
  }

  BK_VRETURN(B);
}



/**
 * Check to see of shmmap is still healthy
 *
 * @param B BAKA World
 * @param shmmap Shared memory map structure
 * @return 1 Everything fresh and ready
 * @return 0 Not yet ready (presumably still in initialization)
 * @return -1 Not fresh
 * @return -2 CLOSED
 * @return -3 Other error
 */
int bk_shmmap_validate(bk_s B, struct bk_shmmap *shmmap)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!shmmap)
  {
    bk_error_printf(B, BK_ERR_ERR, "Illegal arguments\n");
    BK_RETURN(B, -3);
  }

  // Check for OK
  if (BK_SHMMAP_ISFRESH(shmmap))
    BK_RETURN(B, 1);

  // Check for closed
  if (shmmap->sm_addr->sh_state == BK_SHMMAP_CLOSE)
    BK_RETURN(B, -2);

  // Check for not ready (and previously not closed)
  if (shmmap->sm_addr->sh_state != BK_SHMMAP_READY)
    BK_RETURN(B, 0);

  // Only thing left is not fresh
  BK_RETURN(B, -1);
}
