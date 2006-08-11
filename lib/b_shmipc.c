#if !defined(lint) && !defined(__INSIGHT__)
#include "libbk_compiler.h"
UNUSED static const char libbk__rcsid[] = "$Id: b_shmipc.c,v 1.1 2006/08/11 14:48:21 seth Exp $";
UNUSED static const char libbk__copyright[] = "Copyright (c) 2003";
UNUSED static const char libbk__contact[] = "<projectbaka@baka.org>";
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
 * Implementation of shared memory IPC
 *
 * Layout of shared memory ring buffer
 *
 */

#include <libbk.h>
#include <sys/ipc.h>
#include <sys/shm.h>


#define SHMIPC_MAGIC		0xfeedface	///< Magic cookie
#define SHMIPC_MAGIC_EOF	0xdeadbeef	///< Magic cookie for death
#define DEFAULT_SIZE		4074		///< Default size of ring if not specified
#define DEFAULT_SPINUS		0		///< Default, spin



/**
 * Information about an extensible buffer being managed
 */
struct bk_shmipc
{
  const char   *si_filename;			///< Filename we are using
  key_t		si_shmkey;			///< Key/nounce for the shm ipc we are using
  int		si_shmid;			///< Shared memory id
  u_int		si_timeoutus;			///< Default timeout in microseconds
  u_int		si_spinus;			///< How often to check for more data, in microseconds
  struct bk_shmipc_header *si_base;		///< Start of shared memory segment
  char	       *si_ring;			///< Start of data ring (character for pointer arithmetic)
  u_int		si_ringbytes;			///< Number of bytes of ring space
  u_int		si_errno;			///< Errno of last operation
  bk_flags	si_flags;			///< Fun for the future
#define SI_SAWEND	0x01			///< Saw EOF on way or another
#define SI_READONLY	0x02			///< Readonly, otherwise writeonly
#define SI_SEENREADER	0x04			///< Have seen reader
};



/**
 * Information about ring structure
 */
struct bk_shmipc_header
{
  u_int32_t	bsh_magic;			///< Set once everything is initialized
  u_int32_t	bsh_ringsize;			///< Size of ring
  u_int32_t	bsh_ringoffset;			///< Offset of start of ring from base of shared memory
  u_int32_t	bsh_writehand;			///< Byte offset of write hand
  u_int32_t	bsh_readhand;			///< Byte offset of read hand
};



static inline int bytes_available_write(u_int32_t writehand, u_int32_t readhand, u_int32_t ringbytes);
static inline int bytes_available_read(u_int32_t writehand, u_int32_t readhand, u_int32_t ringbytes);




/**
 * Initialize shared memory IPC (reader cannot initialize until writer is present)
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param name name to rendevouz on
 *	@param timeoutus Default timeout for I/O functions in microseconds
 *	@param spinus How often to check for available data/space for operations (microseconds)
 *	@param initus Timeout to wait for writer to become present or old instance to disappear
 *	@param size Desired size of buffers (writer only, ignored for reader)
 *	@param mode SHM permissions mode (writer only, ignored for reader)
 *	@param flags BK_SHMIPC_RDONLY, BK_SHMIPC_WRONLY, BK_SHMIPC_WRWAITFORRD
 *	@return <i>NULL</i> on call failure, allocation failure, writer not present (reader only)
 *	@return <br><i>IPC handle</i> on success
 */
struct bk_shmipc *bk_shmipc_create(bk_s B, const char *name, u_int timeoutus, u_int initus, u_int spinus, u_int size, u_int mode, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_shmipc *bsi = NULL;
  struct timeval endtime;
  struct timeval delta;
  bk_MD5_CTX mdContext;
  int shmflg = 0;
  struct shmid_ds buf;

  if (!name || BK_FLAG_ISCLEAR(flags, BK_SHMIPC_RDONLY|BK_SHMIPC_WRONLY) || BK_FLAG_ALLSET(flags, BK_SHMIPC_RDONLY|BK_SHMIPC_WRONLY))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!spinus)
    spinus = DEFAULT_SPINUS;

  if (!size)
    size = DEFAULT_SIZE;

  if (!BK_CALLOC(bsi))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (!(bsi->si_filename = strdup(name)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate shm name (%s): %s\n", name, strerror(errno));
    goto error;
  }
  bsi->si_spinus = spinus;
  bsi->si_timeoutus = timeoutus;

  bk_MD5Init(B, &mdContext);
  bk_MD5Update(B, &mdContext, bsi->si_filename, strlen(bsi->si_filename));
  bk_MD5Final(B, &mdContext);
  memcpy(&bsi->si_shmkey, &(mdContext.digest), MIN(sizeof(bsi->si_shmkey), sizeof(mdContext.digest)));

  if (BK_FLAG_ISSET(flags, BK_SHMIPC_RDONLY))
  {
    BK_FLAG_SET(bsi->si_flags, SI_READONLY);
  }
  else
  {
    shmflg = IPC_CREAT|IPC_EXCL|mode;
  }
  bsi->si_shmid = -1;
  gettimeofday(&endtime, NULL);
  delta.tv_sec = initus / 1000000;
  delta.tv_usec = initus % 1000000;
  BK_TV_ADD(&endtime,&endtime,&delta);

  while (bsi->si_shmid < 0 && (!initus || BK_TV_CMP(&endtime,&delta) <= 0))
  {
    if ((bsi->si_shmid = shmget(bsi->si_shmkey, size+sizeof(struct bk_shmipc_header), shmflg)) < 0)
    {
      if ((bsi->si_errno = errno) == EEXIST)
      {
	int id;

	bk_error_printf(B, BK_ERR_WARN, "Could not get shared memory--attempting to delete (%s): %s\n", bsi->si_filename, strerror(errno));

	id = shmget(bsi->si_shmkey, 0, 0);
	if (id >= 0)
	{
	  if (shmctl(id, IPC_RMID, NULL) < 0)
	    bk_error_printf(B, BK_ERR_WARN, "Could not get delete old shared memory (%s): %s\n", bsi->si_filename, strerror(errno));
	}
	else
	{
	  bk_error_printf(B, BK_ERR_WARN, "Could not get shared memory for delete (%s): %s\n", bsi->si_filename, strerror(errno));
	}
      }
      else if ((bsi->si_errno = errno) == ENOENT)
      {
	bk_error_printf(B, BK_ERR_WARN, "Could not get shared memory (%s): %s\n", bsi->si_filename, strerror(errno));
      }
      else
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not get shared memory (%s): %s\n", bsi->si_filename, strerror(errno));
	goto error;
      }
      usleep(bsi->si_spinus);
      gettimeofday(&delta, NULL);
    }
  }

  if (bsi->si_shmid < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not get shared memory within timeout(%s): %s\n", bsi->si_filename, strerror(errno));
    goto error;
  }

  if (!(bsi->si_base = shmat(bsi->si_shmid, NULL, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not attach shared memory (%s): %s\n", bsi->si_filename, strerror(errno));
    goto error;
  }

  if (BK_FLAG_ISCLEAR(bsi->si_flags, SI_READONLY))
  {
    bsi->si_base->bsh_ringsize = size;
    bsi->si_base->bsh_ringoffset = sizeof(struct bk_shmipc_header);
    bsi->si_base->bsh_writehand = 0;
    bsi->si_base->bsh_readhand = 0;
    bsi->si_base->bsh_magic = SHMIPC_MAGIC;
  }

  while (bsi->si_base->bsh_magic != SHMIPC_MAGIC && (!initus || BK_TV_CMP(&endtime,&delta) <= 0))
  {
    if (bsi->si_base->bsh_magic == SHMIPC_MAGIC_EOF)
    {
      bk_error_printf(B, BK_ERR_ERR, "Writer closed before we saw magic (%s)\n", bsi->si_filename);
      goto error;
    }
    usleep(bsi->si_spinus);
    gettimeofday(&delta, NULL);
  }

  if (bsi->si_base->bsh_magic != SHMIPC_MAGIC)
  {
    bk_error_printf(B, BK_ERR_ERR, "Writer has not initialized shm within timeout (%s)\n", bsi->si_filename);
    goto error;
  }

  bsi->si_ringbytes = bsi->si_base->bsh_ringsize;
  bsi->si_ring = ((char *)bsi->si_base) + bsi->si_base->bsh_ringoffset;

  if (shmctl(bsi->si_shmid, IPC_STAT, &buf) < 0)
  {
    bsi->si_errno = errno;
    bk_error_printf(B, BK_ERR_ERR, "Could not stat shmipc: %s\n", strerror(errno));
    goto error;
  }

  if ((u_int)((bsi->si_ring - (char *)bsi->si_base) + bsi->si_ringbytes) > buf.shm_segsz)
  {
    bk_error_printf(B, BK_ERR_ERR, "Memory corruption or attack to induce memory corruption ((%p-%p)%u + %d > %u)\n", bsi->si_ring, bsi->si_base, (u_int)(bsi->si_ring - (char *)bsi->si_base), bsi->si_ringbytes, (u_int)buf.shm_segsz);
    bsi->si_errno = EBADSLT;
    goto error;
  }

  if (BK_FLAG_ISCLEAR(bsi->si_flags, SI_READONLY) && BK_FLAG_ISSET(flags, BK_SHMIPC_WRWAITFORRD))
  {
    int numreader = 0;

    bk_shmipc_peek(B, bsi, NULL, NULL, &numreader, 0);
    while (numreader < 1 && (!initus || BK_TV_CMP(&endtime,&delta) <= 0))
    {
      usleep(bsi->si_spinus);
      gettimeofday(&delta, NULL);
      bk_shmipc_peek(B, bsi, NULL, NULL, &numreader, 0);
    }

    if (numreader < 1)
    {
      bk_error_printf(B, BK_ERR_ERR, "Reader of shared memory did not attach in time (%s): %s\n", bsi->si_filename, strerror(errno));
      goto error;
    }
    BK_FLAG_SET(bsi->si_flags, SI_SEENREADER);
  }

  BK_RETURN(B, bsi);

 error:
  bk_shmipc_destroy(B, bsi, 0);
  BK_RETURN(B, NULL);
}



/**
 * Destroy shared memory IPC
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on call failure, allocation failure
 *	@return <br><i>Buffer handle</i> on success
 *	@return  <i>crc</i> of data
 */
void bk_shmipc_destroy(bk_s B, struct bk_shmipc *bsi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsi)
    BK_VRETURN(B);

  if (bsi->si_base)
  {
    bsi->si_base->bsh_magic = SHMIPC_MAGIC_EOF;
    if (shmdt(bsi->si_base) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not detach shared memory: %s\n", strerror(errno));
    }
  }

  if (bsi->si_shmid >= 0 && (shmctl(bsi->si_shmid, IPC_RMID, NULL) < 0))
  {
    bk_error_printf(B, BK_ERR_WARN, "Could not removed shared memory (other side may have done so): %s\n", strerror(errno));
  }

  if (bsi->si_filename)
    free((char *)bsi->si_filename);
  free(bsi);

  BK_VRETURN(B);
}



/**
 * Find the number of bytes available to write in the closed space of the ring buffer
 */
static inline int bytes_available_write(u_int32_t writehand, u_int32_t readhand, u_int32_t ringbytes)
{
   if (readhand <= writehand)
     readhand += ringbytes;

   return(readhand - writehand - 1);
 }



/**
 * Find the number of bytes available to read in the closed space of the ring buffer
 */
static inline int bytes_available_read(u_int32_t writehand, u_int32_t readhand, u_int32_t ringbytes)
 {
   if (readhand > writehand)
     writehand += ringbytes;

   return(writehand - readhand);
 }



/**

 * Write data via shmipc
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param data Data to write
 *	@param len Number of bytes to write
 *	@param timeoutus Timeout override (0 for default)
 *	@param flags BK_SHMIPC_WRITEALL|BK_SHMIPC_NOBLOCK
 *	@return <i>-1</i> on failure
 *	@return <br><i>bytes written</i> on success
 */
ssize_t bk_shmipc_write(bk_s B, struct bk_shmipc *bsi, void *data, size_t len, u_int timeoutus, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  char *cdata = data;
  ssize_t ret = 0;
  u_int32_t writehand;
  struct timeval endtime;
  struct timeval delta;

  if (!bsi || !data)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");

    if (bsi)
      bsi->si_errno = EINVAL;

    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bsi->si_flags, SI_SAWEND))
  {
    bk_error_printf(B, BK_ERR_ERR, "Reader is no longer reachable (%s)\n", bsi->si_filename);
    bsi->si_errno = ENETRESET;
    BK_RETURN(B, -1);
  }

  // Security check
  if (BK_FLAG_ISSET(bsi->si_flags, SI_READONLY))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempting to write to the read side of shmipc\n");
    bsi->si_errno = EACCES;
    BK_RETURN(B, -1);
  }

  // Security check
  if ((writehand = bsi->si_base->bsh_writehand) >= bsi->si_ringbytes)
  {
    bk_error_printf(B, BK_ERR_ERR, "Memory corruption or attack to induce memory corruption\n");
    bsi->si_errno = EBADSLT;
    BK_RETURN(B, -1);
  }

  gettimeofday(&endtime, NULL);
  if (!timeoutus)
    timeoutus = bsi->si_timeoutus;
  delta.tv_sec = timeoutus / 1000000;
  delta.tv_usec = timeoutus % 1000000;
  BK_TV_ADD(&endtime,&endtime,&delta);

  while (len)
  {
    u_int writelen;

    if (!(writelen = bytes_available_write(writehand, bsi->si_base->bsh_readhand, bsi->si_ringbytes)))
    {
      int numreader = 0;

      if (ret && (BK_FLAG_ISCLEAR(flags, BK_SHMIPC_WRITEALL)))
      {
	// Forward progress was made
	BK_RETURN(B, ret);
      }

      bk_shmipc_peek(B, bsi, NULL, NULL, &numreader, 0);

      if (BK_FLAG_ISSET(flags, BK_SHMIPC_NOBLOCK))
      {
      wouldblock:
	bsi->si_errno = EAGAIN;
	bk_error_printf(B, BK_ERR_WARN, "Write failed--timeout with no free space to push data with %d peers (%s)\n", numreader, bsi->si_filename);
	BK_RETURN(B, -1);
      }

      if (numreader > 0 && BK_FLAG_ISCLEAR(bsi->si_flags, SI_SEENREADER))
	BK_FLAG_SET(bsi->si_flags, SI_SEENREADER);
      if (numreader < 1 && BK_FLAG_ISSET(bsi->si_flags, SI_SEENREADER))
      {
	// EOF
	BK_FLAG_SET(bsi->si_flags, SI_SAWEND);
	bsi->si_errno = ENETRESET;
	BK_RETURN(B, -1);
      }

      if (timeoutus)
      {
	gettimeofday(&delta, NULL);
	if (BK_TV_CMP(&endtime,&delta) > 0)
	  goto wouldblock;
      }
      usleep(bsi->si_spinus);
      continue;
    }

    writelen = MIN(writelen, len);
    writelen = MIN(writelen, bsi->si_ringbytes-writehand);

    memcpy(bsi->si_ring + writehand, cdata, writelen);
    cdata += writelen;
    len -= writelen;
    writehand += writelen;
    ret += writelen;

    if (writehand == bsi->si_ringbytes)
      writehand = 0;
    bsi->si_base->bsh_writehand = writehand;
  }

  BK_RETURN(B, ret);
}



/**
 * Read data via shmipc
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param data Data to date (copy-out)
 *	@param len Number of bytes to read
 *	@param timeoutus Timeout override (0 for default)
 *	@param flags BK_SHMIPC_READALL BK_SHMIPC_NOBLOCK
 *	@return <i>-1</i> on failure
 *	@return <br><i>bytes actually read</i> on success
 */
ssize_t bk_shmipc_read(bk_s B, struct bk_shmipc *bsi, void *data, size_t len, u_int timeoutus, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  ssize_t ret = 0;
  char *cdata = data;
  u_int32_t readhand;
  struct timeval endtime;
  struct timeval delta;
  u_int readlen;

  if (!bsi || !data || !len)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    if (bsi)
      bsi->si_errno = EINVAL;
    BK_RETURN(B, -1);
  }

  if (BK_FLAG_ISSET(bsi->si_flags, SI_SAWEND))
  {
    bk_error_printf(B, BK_ERR_ERR, "Writer is no longer reachable (%s)\n", bsi->si_filename);
    BK_RETURN(B, 0);
  }

  // Stupidity check
  if (BK_FLAG_ISCLEAR(bsi->si_flags, SI_READONLY))
  {
    bk_error_printf(B, BK_ERR_ERR, "Attempting to read to the write side of shmipc\n");
    bsi->si_errno = EACCES;
    BK_RETURN(B, -1);
  }

  // Security check
  if ((readhand = bsi->si_base->bsh_readhand) >= bsi->si_ringbytes)
  {
    bk_error_printf(B, BK_ERR_ERR, "Memory corruption or attack to induce memory corruption\n");
    bsi->si_errno = EBADSLT;
    BK_RETURN(B, -1);
  }

  gettimeofday(&endtime, NULL);
  if (!timeoutus)
    timeoutus = bsi->si_timeoutus;
  delta.tv_sec = timeoutus / 1000000;
  delta.tv_usec = timeoutus % 1000000;
  BK_TV_ADD(&endtime,&endtime,&delta);

  while (len)
  {
    if (!(readlen = bytes_available_read(bsi->si_base->bsh_writehand, readhand, bsi->si_ringbytes)))
    {
      int numwriter;

      if (ret && (BK_FLAG_ISCLEAR(flags, BK_SHMIPC_READALL)))
      {
	// Forward progress was made
	BK_RETURN(B, ret);
      }

      bk_shmipc_peek(B, bsi, NULL, NULL, &numwriter, 0);

      if (numwriter < 1 || bsi->si_base->bsh_magic == SHMIPC_MAGIC_EOF)
      {
	// EOF
	BK_FLAG_SET(bsi->si_flags, SI_SAWEND);
	BK_RETURN(B, 0);
      }

      if (BK_FLAG_ISSET(flags, BK_SHMIPC_NOBLOCK))
      {
      wouldblock:
	bsi->si_errno = EAGAIN;
	bk_error_printf(B, BK_ERR_WARN, "Read failed--timeout with no data available (%s)\n", bsi->si_filename);
	BK_RETURN(B, -1);
      }

      if (timeoutus)
      {
	gettimeofday(&delta, NULL);
	if (BK_TV_CMP(&endtime,&delta) > 0)
	  goto wouldblock;
      }
      usleep(bsi->si_spinus);
      continue;
    }

    readlen = MIN(readlen, len);
    readlen = MIN(readlen, bsi->si_ringbytes-readhand);
    memcpy(cdata, bsi->si_ring + readhand, readlen);
    cdata += readlen;
    len -= readlen;
    readhand += readlen;
    ret += readlen;

    if (readhand == bsi->si_ringbytes)
      readhand = 0;
    bsi->si_base->bsh_readhand = readhand;
  }

  BK_RETURN(B, ret);
}



/**
 * Read data via shmipc (will never block)
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param maxbytes Maximum bytes to read (0 for infinite)
 *	@param flags Fun for the future
 *	@return <i>NULL</i> on failure, including EOF and no data available at the moment
 *	@return <br><i>bk_vptr of data</i> on success
 */
bk_vptr *bk_shmipc_readall(bk_s B, struct bk_shmipc *bsi, size_t maxbytes, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  bk_vptr *ret = NULL;
  u_int readbytes;

  if (!bsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    if (bsi)
      bsi->si_errno = EINVAL;
    BK_RETURN(B, NULL);
  }

  if (!(readbytes = bytes_available_read(bsi->si_base->bsh_writehand, bsi->si_base->bsh_readhand, bsi->si_ringbytes)))
  {
    bsi->si_errno = 0;
    goto error;
  }
  if (maxbytes)
    readbytes = MIN(readbytes,maxbytes);

  if (!BK_MALLOC(ret))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    bsi->si_errno = ENOMEM;
    goto error;
  }
  ret->len = readbytes;

  if (!(ret->ptr = malloc(readbytes)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    bsi->si_errno = ENOMEM;
    goto error;
  }

  if ((ret->len = bk_shmipc_read(B, bsi, ret->ptr, ret->len, 0, BK_SHMIPC_NOBLOCK)) < 1)
    goto error;

  if (ret->len != readbytes)
  {
    bk_error_printf(B, BK_ERR_WARN, "Unexpected mismatch between requested/available and actual bytes: %d != %d", ret->len, readbytes);
  }

  BK_RETURN(B, ret);

 error:
  if (ret)
  {
    if (ret->ptr)
      free(ret->ptr);
    free(ret);
  }
  BK_RETURN(B, NULL);
}



/**
 * Peek at available data in ipc ring
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param bytesreadable Copy-out number of bytes someone can read
 *	@param byteswritable Copy-out number of bytes someone can write
 *	@param numothers Copy-out number of other people attached
 *	@param flags Fun for the future
 *	@return <i>-1</i> on failure
 *	@return <br><i>0</i> on success
 */
int bk_shmipc_peek(bk_s B, struct bk_shmipc *bsi, size_t *bytesreadable, size_t *byteswritable, int *numothers, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    if (bsi)
      bsi->si_errno = EINVAL;
    BK_RETURN(B, -1);
  }

  if (bytesreadable)
    *bytesreadable = bytes_available_read(bsi->si_base->bsh_writehand, bsi->si_base->bsh_readhand, bsi->si_ringbytes);
  if (byteswritable)
    *byteswritable = bytes_available_write(bsi->si_base->bsh_writehand, bsi->si_base->bsh_readhand, bsi->si_ringbytes);

  if (numothers)
  {
    struct shmid_ds buf;

    if (shmctl(bsi->si_shmid, IPC_STAT, &buf) < 0)
    {
      bsi->si_errno = errno;
      bk_error_printf(B, BK_ERR_ERR, "Could not stat shmipc: %s\n", strerror(errno));
      BK_RETURN(B, -1);
    }

    *numothers = buf.shm_nattch - 1;

    if (BK_FLAG_ISCLEAR(bsi->si_flags, SI_SEENREADER) && buf.shm_nattch > 1)
      BK_FLAG_SET(bsi->si_flags, SI_SEENREADER);
  }

  BK_RETURN(B, 0);
}



/**
 * Retrieve errno for last operation
 *
 * THREADS: MT-SAFE
 *
 *	@param B BAKA Thread/global state
 *	@param bsi Shared memory structure
 *	@param flags Fun for the future
 *	@return <i>-1</i> on failure to retrieve
 *	@return <br><i>0</i> if last operation succeeded
 *	@return <br><i>E*</i> errno numbers for last operation
 */
int bk_shmipc_errno(bk_s B, struct bk_shmipc *bsi, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!bsi)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    if (bsi)
      bsi->si_errno = EINVAL;
    BK_RETURN(B, -1);
  }

  BK_RETURN(B, bsi->si_errno);
}
