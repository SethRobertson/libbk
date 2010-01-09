#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2002-2010";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2002-2010 The Authors. All rights reserved.
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
 * BAKA Child Management.
 *
 * Creation of child--w w/o pipes to child
 * Signaling child
 * Signaling of all children
 * Notification on child state change/death (if desired)
 * bk_run sync signal for wait
 */

#include <libbk.h>
#include "libbk_internal.h"



/**
 * @name Defines: childidlist_clc
 * Stored childid to child information mappings
 * to hide CLC choice.
 */
// @{
#define childidlist_create(o,k,f,a)	bst_create(o,k,f)
#define childidlist_destroy(h)		bst_destroy(h)
#define childidlist_insert(h,o)		bst_insert(h,o)
#define childidlist_insert_uniq(h,n,o)	bst_insert_uniq(h,n,o)
#define childidlist_append(h,o)		bst_append(h,o)
#define childidlist_append_uniq(h,n,o)	bst_append_uniq(h,n,o)
#define childidlist_search(h,k)		bst_search(h,k)
#define childidlist_delete(h,o)		bst_delete(h,o)
#define childidlist_minimum(h)		bst_minimum(h)
#define childidlist_maximum(h)		bst_maximum(h)
#define childidlist_successor(h,o)	bst_successor(h,o)
#define childidlist_predecessor(h,o)	bst_predecessor(h,o)
#define childidlist_iterate(h,d)	bst_iterate(h,d)
#define childidlist_nextobj(h,i)	bst_nextobj(h,i)
#define childidlist_iterate_done(h,i)	bst_iterate_done(h,i)
#define childidlist_error_reason(h,i)	bst_error_reason(h,i)
static int childidlist_oo_cmp(struct bk_child_comm *a, struct bk_child_comm *b);
static int childidlist_ko_cmp(int *a, struct bk_child_comm *b);
#if 0
static unsigned int childidlist_obj_hash(struct bk_child_comm *b);
static unsigned int childidlist_key_hash(int *a);
static struct ht_args childidlist_args = { 127, 2, (ht_func)childidlist_obj_hash, (ht_func)childidlist_key_hash };
#endif
// @}



/**
 * @name Defines: childpidlist_clc
 * Stored childpid to child information mappings
 * to hide CLC choice.
 */
// @{
#define childpidlist_create(o,k,f,a)	bst_create(o,k,f)
#define childpidlist_destroy(h)		bst_destroy(h)
#define childpidlist_insert(h,o)	bst_insert(h,o)
#define childpidlist_insert_uniq(h,n,o)	bst_insert_uniq(h,n,o)
#define childpidlist_append(h,o)	bst_append(h,o)
#define childpidlist_append_uniq(h,n,o)	bst_append_uniq(h,n,o)
#define childpidlist_search(h,k)	bst_search(h,k)
#define childpidlist_delete(h,o)	bst_delete(h,o)
#define childpidlist_minimum(h)		bst_minimum(h)
#define childpidlist_maximum(h)		bst_maximum(h)
#define childpidlist_successor(h,o)	bst_successor(h,o)
#define childpidlist_predecessor(h,o)	bst_predecessor(h,o)
#define childpidlist_iterate(h,d)	bst_iterate(h,d)
#define childpidlist_nextobj(h,i)	bst_nextobj(h,i)
#define childpidlist_iterate_done(h,i)	bst_iterate_done(h,i)
#define childpidlist_error_reason(h,i)	bst_error_reason(h,i)
static int childpidlist_oo_cmp(struct bk_child_comm *a, struct bk_child_comm *b);
static int childpidlist_ko_cmp(int *a, struct bk_child_comm *b);
#if 0
static unsigned int childpidlist_obj_hash(struct bk_child_comm *b);
static unsigned int childpidlist_key_hash(int *a);
static struct ht_args childpidlist_args = { 127, 2, (ht_func)childpidlist_obj_hash, (ht_func)childpidlist_key_hash };
#endif
// @}




/**
 * Create a child management structure
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA thread/global environment
 * @param flags Fun for the future
 */
struct bk_child *bk_child_icreate(bk_s B, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"PROCONSUL");
  struct bk_child *bchild;

  if (!BK_MALLOC(bchild))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate child management: %s\n", strerror(errno));
    BK_RETURN(B, NULL);
  }

#ifdef BK_USING_PTHREADS
  pthread_mutex_init(&bchild->bc_lock, NULL);
#endif /* BK_USING_PTHREADS */

  if (!(bchild->bc_childidlist = childidlist_create((dict_function)childidlist_oo_cmp, (dict_function)childidlist_ko_cmp, bk_thread_safe_if_thread_ready, &childidlist_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate child id management tracking list: %s\n", childidlist_error_reason(NULL, NULL));
    goto error;
  }

  if (!(bchild->bc_childpidlist = childpidlist_create((dict_function)childpidlist_oo_cmp, (dict_function)childpidlist_ko_cmp, bk_thread_safe_if_thread_ready, &childpidlist_args)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate child pid management tracking list: %s\n", childidlist_error_reason(NULL, NULL));
    goto error;
  }

  bchild->bc_nextchild = 1;

  BK_RETURN(B, bchild);

 error:
  if (bchild)
    bk_child_idestroy(B, bchild, 0);
  BK_RETURN(B, NULL);
}



/**
 * Clean a child list
 *
 * THREADS: MT-SAFE (assuming different bchild)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B Baka thread/global environment
 * @param bchild Child management state
 * @param specialchild Special child who should not be closed (-1 if not special)
 * @param flags Fun fur the future
 */
void bk_child_iclean(bk_s B, struct bk_child *bchild, int specialchild, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"PROCONSUL");
  struct bk_child_comm *bcc;

  if (!bchild)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_lock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */


  DICT_NUKE_CONTENTS(bchild->bc_childpidlist, childpidlist, bcc, bk_error_printf(B, BK_ERR_ERR, "Could not delete minimum bcc: %s\n", childpidlist_error_reason(bchild->bc_childpidlist, NULL)); break, /*naught*/);

  /*
   * <WARNING>THE CLOSES MAY BE OVEREAGER.  If the fd was closed
   * previously through other activity, we do not get notification
   * that this has happened.  Since fds may be reused, these may have
   * been reused for other purpose, including ones which you do NOT
   * want closed.  Hmm.</WARNING>
   */

  DICT_NUKE_CONTENTS(bchild->bc_childidlist, childidlist, bcc, bk_error_printf(B, BK_ERR_ERR, "Could not delete minimum bcc: %s\n", childidlist_error_reason(bchild->bc_childidlist, NULL)); break, if (bcc->cc_id != specialchild) { if (bcc->cc_childcomm[0] >= 0) close(bcc->cc_childcomm[0]); if (bcc->cc_childcomm[1] >= 0) close(bcc->cc_childcomm[1]); if ((bcc->cc_childcomm[2] >= 0) && (bcc->cc_childcomm[2] != bcc->cc_childcomm[1])) close(bcc->cc_childcomm[2]); } free(bcc));

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}



/**
 * Destroy a child list
 *
 * THREADS: MT-SAFE (assuming different bchild)
 * THREADS: REENTRANT (otherwise)
 *
 * @param B Baka thread/global environment
 * @param bchild Child management state
 * @param flags Fun fur the future
 */
void bk_child_idestroy(bk_s B, struct bk_child *bchild, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"PROCONSUL");

  if (!bchild)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  bk_child_iclean(B, bchild, -1, flags);
  childidlist_destroy(bchild->bc_childidlist);
  childpidlist_destroy(bchild->bc_childpidlist);

#ifdef BK_USING_PTHREADS
  if (pthread_mutex_destroy(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  free(bchild);

  BK_VRETURN(B);
}



/**
 * Start a child in various ways
 *
 * THREADS: MT-SAFE (assuming different bchild)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B BAKA thread/global environment
 * @param bchild Child management state
 * @param cc_callback Callback for child state changes
 * @param cc_opaque State for callback
 * @param fds Copy-out pointer to requested child fds
 * @param flags BK_CHILD_* -- how to start this up
 * @return <i>-1</i> Invalid argument, fork failure, etc
 * @return <br><i>0</i> You are child
 * @return <br><i>childid</i> If you are parent--childid returned
 */
int bk_child_istart(bk_s B, struct bk_child *bchild, void (*cc_callback)(bk_s B, void *opaque, int childid, bk_childstate_e state, u_int status), void *cc_opaque, int *fds[], bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"PROCONSUL");
  struct bk_child_comm *bcc;
  int fds0[2] = { -1, -1 };
  int fds1[2] = { -1, -1 };
  int fds2[2] = { -1, -1 };

  if (!bchild || !cc_callback)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (!BK_MALLOC(bcc))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate child communication structure: %s\n", strerror(errno));
    BK_RETURN(B, -1);
  }

  bcc->cc_childpid = 0;
  bcc->cc_callback = cc_callback;
  bcc->cc_opaque = cc_opaque;
  bcc->cc_statuscode = 0;
  bcc->cc_flags = BK_FLAG_ISSET(flags, BK_CHILD_NOTIFYSTOP)?CC_WANT_NOTIFYSTOP:0;
  bcc->cc_childcomm[0] = -1;
  bcc->cc_childcomm[1] = -1;
  bcc->cc_childcomm[2] = -1;

  if (BK_FLAG_ISSET(flags, BK_CHILD_WANTRPIPE))
  {
    if (pipe(fds0) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create pipe: %s\n", strerror(errno));
      goto error;
    }
  }
  else if (BK_FLAG_ISSET(flags, BK_CHILD_WANTWPIPE))
  {
    if (pipe(fds1) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create pipe: %s\n", strerror(errno));
      goto error;
    }
  }
  else if (BK_FLAG_ISSET(flags, BK_CHILD_WANTEPIPE))
  {
    if (pipe(fds2) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not create pipe: %s\n", strerror(errno));
      goto error;
    }
  }
  else if (BK_FLAG_ISSET(flags, BK_CHILD_WANTEASW))
  {
    fds2[0] = fds1[0];
    fds2[1] = fds1[1];
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_lock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  // Search for unused childid -- will spin forever if you have 2^31 forked children :-)
  while (childidlist_search(bchild->bc_childidlist, &bchild->bc_nextchild))
    if (++bchild->bc_nextchild < 0) bchild->bc_nextchild = 1;

  bcc->cc_id = bchild->bc_nextchild++;
  if (bchild->bc_nextchild < 0) bchild->bc_nextchild = 1;

  if ((bcc->cc_childpid = fork()) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "No forking: %s\n", strerror(errno));
    goto lockerror;
  }

  if (fds)
    *fds = bcc->cc_childcomm;

  if (bcc->cc_childpid)
  {						// Parent
    if (childpidlist_insert(bchild->bc_childpidlist, bcc) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert a validated childcomm pid: %s\n", childpidlist_error_reason(bchild->bc_childpidlist, NULL));
      goto lockerror;
    }

    if (childidlist_insert(bchild->bc_childidlist, bcc) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert a validated childcomm id: %s\n", childidlist_error_reason(bchild->bc_childidlist, NULL));
      goto lockerror;
    }

    bcc->cc_childcomm[0] = fds0[1];
    bcc->cc_childcomm[1] = fds1[0];
    bcc->cc_childcomm[2] = fds2[0];
    if (fds0[0] >= 0)
      close(fds0[0]);
    if (fds1[1] >= 0)
      close(fds1[1]);
    if (BK_FLAG_ISCLEAR(flags, BK_CHILD_WANTEASW))
    {
      if (fds2[1] >= 0)
	close(fds2[1]);
    }
    fds0[0] = fds0[1] = -1;
    fds1[0] = fds1[1] = -1;
    fds2[0] = fds2[1] = -1;

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

    BK_RETURN(B, bcc->cc_id);
  }
  else
  {						// Child
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&bchild->bc_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */

    // <TODO>Perhaps we should technially nuke all other children since they are not *our* children...</TODO>

    bcc->cc_id = 0;
    if (childidlist_insert(bchild->bc_childidlist, bcc) != DICT_OK)
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not insert a validated childcomm id: %s\n", childidlist_error_reason(bchild->bc_childidlist, NULL));
      goto lockerror;
    }

    bcc->cc_childcomm[0] = fds0[0];
    bcc->cc_childcomm[1] = fds1[1];
    bcc->cc_childcomm[2] = fds2[1];
    if (fds0[1] >= 0)
      close(fds0[1]);
    if (fds1[0] >= 0)
      close(fds1[0]);
    if (BK_FLAG_ISCLEAR(flags, BK_CHILD_WANTEASW))
    {
      if (fds2[0] >= 0)
	close(fds2[0]);
    }
    fds0[0] = fds0[1] = -1;
    fds1[0] = fds1[1] = -1;
    fds2[0] = fds2[1] = -1;
    BK_RETURN(B, 0);
  }

  if (0)
  {
    int i; // keep here to avoid deprecated label usage warnings
  lockerror:
#ifdef BK_USING_PTHREADS
    if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&bchild->bc_lock) != 0)
      abort();
#endif /* BK_USING_PTHREADS */
    i=0; // keep here to avoid deprecated label usage warnings
  }

 error:
  if (fds0[0] >= 0)
    close(fds0[0]);
  if (fds0[1] >= 0)
    close(fds0[1]);
  if (fds1[0] >= 0)
    close(fds1[0]);
  if (fds1[1] >= 0)
    close(fds1[1]);
  if (BK_FLAG_ISCLEAR(flags, BK_CHILD_WANTEASW))
  {
    if (fds2[0] >= 0)
      close(fds2[0]);
    if (fds2[1] >= 0)
      close(fds2[1]);
  }

  if (bcc)
  {
    childidlist_delete(bchild->bc_childidlist, bcc);
    childpidlist_delete(bchild->bc_childpidlist, bcc);
    free(bcc);
  }
  BK_RETURN(B, -1);
}



/**
 * Handle SIGCHILD in sync signal handler
 *
 * THREADS: MT-SAFE (assuming different bchild/opaque)
 * THREADS: THREAD-REENTRANT (otherwise)
 *
 * @param B BAKA thread/global environment
 * @param run Baka run enviornment
 * @param signum Signal we are handling
 * @param opaque Child management state
 */
void bk_child_isigfun(bk_s B, struct bk_run *run, int signum, void *opaque)
{
  BK_ENTRY(B, __FUNCTION__,__FILE__,"PROCONSUL");
  struct bk_child *bchild = opaque;
  pid_t childpid;
  int status;

  if (!bchild)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_lock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  while ((childpid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0)
  {
    struct bk_child_comm *bcc;

    if (!(bcc = childpidlist_search(bchild->bc_childpidlist, &childpid)))
    {
      bk_error_printf(B, BK_ERR_WARN, "Could not find child in managed child list: %d\n", (int)childpid);
      continue;
    }

    if (WIFSTOPPED(status))
    {
      if (BK_FLAG_ISSET(bcc->cc_flags, CC_WANT_NOTIFYSTOP))
	(*bcc->cc_callback)(B, bcc->cc_opaque, bcc->cc_id, BkChildStateStop, status);
    }
    else
    {
      BK_FLAG_SET(bcc->cc_flags, CC_DEAD);
      (*bcc->cc_callback)(B, bcc->cc_opaque, bcc->cc_id, BkChildStateDead, status);

      // This is an X-child

      /*
       * <WARNING>We are assuming the pipes will automatically close
       * through bk_run discovery of child's auto-close on exit.  We don't
       * want to close ourselves because fd may have closed previously, and
       * the FD may have been reused.</WARNING>
       */
      childpidlist_delete(bchild->bc_childpidlist, bcc);
      childidlist_delete(bchild->bc_childidlist, bcc);
      free(bcc);
    }
  }

#ifdef BK_USING_PTHREADS
  if (BK_GENERAL_FLAG_ISTHREADREADY(B) && pthread_mutex_unlock(&bchild->bc_lock) != 0)
    abort();
#endif /* BK_USING_PTHREADS */

  BK_VRETURN(B);
}


// XXX - <TODO>implement external functions (handle B mgmt, run signal handler)</TODO>
// XXX - <TODO>implement sigone/sigall functions</TODO>



/*
 * CLC comparison routines
 *
 * THREADS: REENTRANT
 */
static int childidlist_oo_cmp(struct bk_child_comm *a, struct bk_child_comm *b)
{
  return (a->cc_id - b->cc_id);
}
static int childidlist_ko_cmp(int *a, struct bk_child_comm *b)
{
  return (*a - b->cc_id);
}
#if 0
static unsigned int childidlist_obj_hash(struct bk_child_comm *b)
{
  return (b->cc_id);
}
static unsigned int childidlist_key_hash(int *a)
{
  return (*a);
}
#endif
static int childpidlist_oo_cmp(struct bk_child_comm *a, struct bk_child_comm *b)
{
  return (a->cc_childpid - b->cc_childpid);
}
static int childpidlist_ko_cmp(int *a, struct bk_child_comm *b)
{
  return (*a - b->cc_childpid);
}
#if 0
static unsigned int childpidlist_obj_hash(struct bk_child_comm *b)
{
  return (b->cc_childpid);
}
static unsigned int childpidlist_key_hash(int *a)
{
  return (*a);
}
#endif
