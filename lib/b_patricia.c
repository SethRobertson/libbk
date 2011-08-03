#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2001-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2001-2011 The Authors. All rights reserved.
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
 * Patricia tree suitable for text or ipv4 or ipv6 addresses
 */

#include <libbk.h>
#include "libbk_internal.h"


/**
 * Round from bits to bytes, either rounding down or up as necessary
 * (with an alternative for minimum allocation lengths)
 */
#define BROUNDDOWN(bitlen) ((int)(bitlen)/8)
#define BROUNDUP(bitlen) BROUNDDOWN((bitlen)+7)
#define BROUNDUP_LOC(bitlen) ({ int x = BROUNDUP(bitlen); x?x:1; })



/**
 * Functions to extract specific bits
 */
#define bp_bitsof_int(k,o,b) ((k)[(o)]>>(7-(b)))
#define bp_bitsof(k,b) ( bp_bitsof_int((k),(b)/8,(((b)%8))) )
#define bp_bitof(k,b) (bp_bitsof((k),(b))&1)



/**
 * Compute sizes of interesting types, in bits
 */
#define INTBITS (sizeof(int)*8)
#define BYTEBITS (sizeof(char)*8)



/**
 * Match the key against the lock and see how many pin (bits) match
 *
 * Copy-out the length in common
 *
 * Note, we are assuming int alignment of needle and key
 *
 * TODO: Optimization: track how many bits have been previously
 * checked and don't recheck them.
 */
#define PREFIX_MATCH(needle, needlelen, key, keylen, commonlenp)		\
{										\
  u_int testbits = MIN(needlelen,keylen);					\
  u_int offset = 0;								\
  u_int i;									\
										\
  /* Test word-at-a-time for speed when possible */				\
  while (testbits > INTBITS)							\
  {										\
    if (*(int *)&needle[offset] != *(int *)&key[offset])			\
      break;									\
    offset += sizeof(int);							\
    testbits -= INTBITS;							\
  }										\
  /* Test byte-at-a-time for speed when possible */				\
  while (testbits > BYTEBITS)							\
  {										\
    if (needle[offset] != key[offset])						\
      break;									\
    offset++;									\
    testbits -= BYTEBITS;							\
  }										\
										\
  /* Test remaining bits (<8 matching left) */					\
  *commonlenp = offset*BYTEBITS;						\
  for(i=0;i<testbits;i++)							\
  {										\
    if (bp_bitsof_int(needle,offset,i) != bp_bitsof_int(key,offset,i))		\
      break;									\
    (*commonlenp) += 1;								\
  }										\
}



/**
 * Patricia (sub)tree.
 *
 * A node with bp_data set is a terminal node (a node where
 * bp_prefix/bp_bitlen is a value the user inserted).
 *
 * Without that set, it is just a intermediate node (crit-bit) to
 * allow the bit selection to select the next node of interest.
 *
 * If bp_prefix is zero and data is NULL, node is empty.
 */
struct bk_pnode
{
  u_char	       *bp_prefix;		///< Key represented by this node
  void		       *bp_data;		///< Terminating element, end user data
  struct bk_pnode      *bp_children[2];		///< Left (0), Right (1)
  u_short		bp_bitlen;		///< Length of prefix in bits
};



static void *bk_patricia_successor_int(struct bk_pnode *tree, void **last);
static int bk_patricia_delete_int(struct bk_pnode *tree, u_char *key, u_short keyblen);
static void bk_patricia_node_destroy(struct bk_pnode *node);
static int bk_patricia_vdelete_int(struct bk_pnode *tree, struct bk_pnode *node, void *value);



/**
 * Create a patricia trie/tree, suitable for strings,
 * ipv4, or ipv6 addresses.
 *
 * THREADS: MT-SAFE
 *
 * @param B BAKA THread/global state
 * @return <i>NULL</i> on allocation failure
 * @return <br><i>tree root pointer</i> on success
 */
struct bk_pnode *bk_patricia_create(bk_s B)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_pnode *root = calloc(1, sizeof(*root));

  if (!root)
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate: %s\n", strerror(errno));

  BK_RETURN(B, root);
}



/**
 * Insert a node into a patricia trie/tree
 *
 * Key length represents big endian length, where, for example,
 * a length of 8 represents one byte (key[0]) and a a length of 9 represents
 * one byte (key[0]) and (key[1]&7)
 *
 * Strings obviously should always have a length which is a multiple of 8 bits.
 * Strings should be null terminated only if you want to prevent prefix matching
 * (eg. "foo"/3 will prefix-match "food", but "foo"/4 will not.
 *
 * If a particular insert matches an already existent node, replace
 * the old data with the new and return the old data in a copy out (or
 * leak memory if the olddata pointer was not provided).
 *
 * THREADS: MT-SAFE (as long as tree is thread-private)
 *
 * @param B BAKA THread/global state
 * @param tree Patricia tree
 * @param key Pointer to key data
 * @param keyblen Length (in bits) of key data.
 * @param data Data associated with key
 * @param olddata Pointer to old data if insert is a duplicate
 * @return <i>-1</i> failure
 * @return <br><i>0</i> on success
 */
int bk_patricia_insert(bk_s B, struct bk_pnode *tree, u_char *key, u_short keyblen, void *data, void **olddata)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_short common_len = 0;
  int bit;
  struct bk_pnode *child;

#ifdef BP_DEBUG2
  fprintf(stderr,"Entering with tree %p, key %.*s/%d, data=%p\n",tree,BROUNDUP(keyblen),key,keyblen,data);
  { int x; fprintf(stderr, "tree %10.*s/%3d: ",BROUNDUP(tree->bp_bitlen),tree->bp_prefix,tree->bp_bitlen); for(x=0;x<tree->bp_bitlen;x++) { fprintf(stderr,"%d",bp_bitof(tree->bp_prefix,x)); }; fprintf(stderr,"\n"); }
  { int x; fprintf(stderr, "key  %10.*s/%3d: ",BROUNDUP(keyblen),key,keyblen); for(x=0;x<keyblen;x++) { fprintf(stderr,"%d",bp_bitof(key,x)); }; fprintf(stderr,"\n"); }
#endif /*BP_DEBUG2*/

  if (!tree || !key)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, -1);
  }

  if (olddata)
    *olddata = NULL;

  // Linear programming instead of recursion.  We are decending the tree
  while (1)
  {
    // Case one: current node is empty (root or otherwise)
    if (!tree->bp_bitlen && !tree->bp_prefix)
    {
      tree->bp_bitlen = keyblen;
      if (!(tree->bp_prefix = bk_memdup(key,BROUNDUP_LOC(keyblen))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate node key of length %d: %s\n", BROUNDUP_LOC(keyblen), strerror(errno));
	BK_RETURN(B, -1);
      }
      tree->bp_data = data;
#ifdef BP_DEBUG
      fprintf(stderr,"Case 1: inserting %p with %.*s/%d\n",tree->bp_data,tree->bp_bitlen,tree->bp_prefix, tree->bp_bitlen);
#endif /*BP_DEBUG*/
      BK_RETURN(B, 0);
    }

    PREFIX_MATCH(tree->bp_prefix, tree->bp_bitlen, key, keyblen, &common_len);
#ifdef BP_DEBUG2
    fprintf(stderr,"PREFIX match of %.*s/%d v %.*s/%d yielded %d (%d+%d %d+%d)\n", BROUNDUP(tree->bp_bitlen), tree->bp_prefix, tree->bp_bitlen, BROUNDUP(keyblen), key, keyblen, common_len,bp_bitof(tree->bp_prefix,common_len-1),bp_bitof(tree->bp_prefix,common_len),bp_bitof(key,common_len-1),bp_bitof(key,common_len));
    { int x; fprintf(stderr, "tree %10.*s/%3d: ",BROUNDUP(tree->bp_bitlen),tree->bp_prefix,tree->bp_bitlen); for(x=0;x<tree->bp_bitlen;x++) { fprintf(stderr,"%d",bp_bitof(tree->bp_prefix,x)); }; fprintf(stderr,"\n"); }
#endif /*BP_DEBUG2*/

    // Case two: inserting value matches this node (node=aaa/24, key=aaa/24)
    if (common_len == keyblen && common_len == tree->bp_bitlen)
    {
      if (tree->bp_data)
      {						// Was terminal node?  Return bad node
	if (olddata)
	  *olddata = tree->bp_data;
      }
#ifdef BP_DEBUG
      fprintf(stderr,"Case 2: update %p->%p with %.*s/%d\n",data,tree->bp_data,tree->bp_bitlen,tree->bp_prefix, tree->bp_bitlen);
#endif /*BP_DEBUG*/
      tree->bp_data = data;
      BK_RETURN(B, 0);
    }

    // Case three: current node is a prefix of the inserting node (node=aaa/24, key=aaaa/32)
    if (common_len == tree->bp_bitlen)
    {
      bit = bp_bitof(key,common_len);
#ifdef BP_DEBUG
      fprintf(stderr,"Case 3: try with child %d@%d ",bit,common_len+1);
#endif /*BP_DEBUG*/

      // If no child exists, create one
      if (!(child = tree->bp_children[bit]))
      {
#ifdef BP_DEBUG
	fprintf(stderr,"(new child)");
#endif /*BP_DEBUG*/
	if (!(child = tree->bp_children[bit] = calloc(1, sizeof(struct bk_pnode))))
	{
	  bk_error_printf(B, BK_ERR_ERR, "Could not allocate patricia node: %s\n", strerror(errno));
	  BK_RETURN(B, -1);
	}
      }
#ifdef BP_DEBUG
      fprintf(stderr,"\n");
#endif /*BP_DEBUG*/
      // Try again which this child with a fresh tree so it will hit case one
      tree = child;
      continue;
    }

    // Case four: new node is a prefix of the current node: node=aaa/24, key=aa/16 (common==16)
    if (common_len == keyblen)
    {
      bit = bp_bitof(tree->bp_prefix,common_len);

      if (!(child = malloc(sizeof(struct bk_pnode))))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not allocate patricia node: %s\n", strerror(errno));
	BK_RETURN(B, -1);
      }
      *child = *tree;
      memset(tree,0, sizeof(*tree));
      tree->bp_children[bit] = child;

#ifdef BP_DEBUG
      fprintf(stderr,"Case 4: insert copy-child %d@%d\n",bit,common_len+1);
#endif /*BP_DEBUG*/

      // We fall through loop with a tree which was emptied (aside from children) so it will hit case one
      continue;
    }

    bit = bp_bitof(tree->bp_prefix,common_len);

    // Last case: New node is creating an alternative: node=aaa/24, key=aab/24
    if (!(child = malloc(sizeof(struct bk_pnode))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate patricia node: %s\n", strerror(errno));
      BK_RETURN(B, -1);
    }
    *child = *tree;

#ifdef BP_DEBUG2
    fprintf(stderr,"REPEAT          %.*s/%d v %.*s/%d yielded %d (%d+%d %d+%d)\n", BROUNDUP(tree->bp_bitlen), tree->bp_prefix, tree->bp_bitlen, BROUNDUP(keyblen), key, keyblen, common_len,bp_bitof(tree->bp_prefix,common_len-1),bp_bitof(tree->bp_prefix,common_len),bp_bitof(key,common_len-1),bp_bitof(key,common_len));
    assert(bp_bitof(tree->bp_prefix,common_len) != bp_bitof(key,common_len));
#endif /*BP_DEBUG2*/

    memset(tree,0, sizeof(*tree));
    tree->bp_children[bit] = child;

#ifdef BP_DEBUG
    fprintf(stderr,"Case 5: insert copy-child %d@%d ",bit,common_len+1);
#endif /*BP_DEBUG*/

    tree->bp_bitlen = common_len;
    if (!(tree->bp_prefix = bk_memdup(key,BROUNDUP_LOC(common_len))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate inode key of length %d: %s\n", BROUNDUP_LOC(keyblen), strerror(errno));
      BK_RETURN(B, -1);
    }

    if (!(child = calloc(1, sizeof(struct bk_pnode))))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate patricia node: %s\n", strerror(errno));
      BK_RETURN(B, -1);
    }

    bit = bp_bitof(key,common_len);
    tree->bp_children[bit] = child;
    tree = child;

#ifdef BP_DEBUG
    fprintf(stderr," insert new-child %d@%d\n",bit,common_len+1);
#endif /*BP_DEBUG*/

    // We fall through loop with fresh tree so it will hit case one
  }

  // UNREACHED
  BK_RETURN(B, -1);
}



/**
 * Search for a node in the bit-trie
 *
 *
 * @param B BAKA THread/global state
 * @param tree Patricia tree
 * @param key Pointer to key data
 * @param keyblen Length (in bits) of key data.
 * @return <i>NULL</i> on failure or missing key
 * @return <br><i>data</i> if found key
 */
void *bk_patricia_search(bk_s B, struct bk_pnode *tree, u_char *key, u_short keyblen)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  u_short common_len = 0;

  if (!tree || !key)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_RETURN(B, NULL);
  }

  // Linear programming instead of recursion.  We are decending the tree
  while (1)
  {
    // Case zero: null pointers (caused by going down tree to a node which doesn't exist yet)
    if (!tree)
      BK_RETURN(B, NULL);

    PREFIX_MATCH(tree->bp_prefix, tree->bp_bitlen, key, keyblen, &common_len);

    // Case one: exact match
    if (common_len == keyblen && common_len == tree->bp_bitlen)
    {
      BK_RETURN(B, tree->bp_data);
    }

    // Case two: complete match of key, partial match of tree)
    if (common_len == keyblen)
    {
      BK_RETURN(B, NULL);
    }

    // Case three: partial match of key, go down the tree
    tree = tree->bp_children[bp_bitof(key,common_len)];
  }
  // notreached
  BK_RETURN(B, NULL);
}



/**
 * Find the minimum node in the bit-trie (ie prefix order)
 *
 * @param B Baka thread/global state
 * @param tree Patricia tree
 * @return <i>NULL</i> on failure or missing key
 * @return <br><i>data</i> if found minimum node
 */
void *bk_patricia_minimum(bk_s B, struct bk_pnode *tree)
{
  return bk_patricia_successor_int(tree, NULL);
}



/**
 * Find the successor node in the bit-trie (ie prefix order)
 *
 * Note that this is going to work if the node you found was deleted
 *
 * @param B Baka thread/global state
 * @param tree Patricia tree
 * @param last User data previously inserted into tree (or returned by minimum/successor/search)
 * @return <i>NULL</i> on failure or missing key
 * @return <br><i>data</i> if found minimum node
 */
void *bk_patricia_successor(bk_s B, struct bk_pnode *tree, void *last)
{
  return bk_patricia_successor_int(tree, &last);
}



/**
 * Find the successor node in the bit-trie (ie prefix order)
 *
 * Note that this is going to work if the node you found was deleted
 *
 * @param tree Patricia tree
 * @param last User data previously inserted into tree (or returned by minimum/successor/search)
 * @return <i>NULL</i> on failure or missing key
 * @return <br><i>data</i> if found minimum node
 */
static void *bk_patricia_successor_int(struct bk_pnode *tree, void **last)
{
  void *ret;

#ifdef BP_DEBUG2
  fprintf(stderr,"Entering si with %p %p %p\n",tree,last,last?*last:last);
#endif /*BP_DEBUG2*/

  if (!tree)
    return(NULL);

  if (tree->bp_data)
  {
    if (last && *last)
    {
      // If we match this data, switch to return-next-data mode
      if (*last == tree->bp_data)
      {
	*last=NULL;
#ifdef BP_DEBUG2
	fprintf(stderr,"Found matching data at %p, switching to rnd mode %p\n",tree,*last);
#endif /*BP_DEBUG2*/
      }
    }
    else
    {
#ifdef BP_DEBUG2
      fprintf(stderr,"Found data at %p, returning\n",tree);
#endif /*BP_DEBUG2*/
      return(tree->bp_data);
    }
  }

  if (ret = bk_patricia_successor_int(tree->bp_children[0],last))
    return(ret);

  if (ret = bk_patricia_successor_int(tree->bp_children[1],last))
    return(ret);

#ifdef BP_DEBUG2
  fprintf(stderr,"Miss data at %p, returning\n",tree);
#endif /*BP_DEBUG2*/
  return(NULL);
}



/**
 * Delete a named node by value
 *
 * @param B Baka thread/global state
 * @param tree Patricia tree
 * @param value Value pointer to look for and delete
 */
void bk_patricia_vdelete(bk_s B, struct bk_pnode *tree, void *value)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (!value || !tree)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

#ifdef BP_DEBUG
  fprintf(stderr,"Trying to delete %p\n",value);
#endif /*BP_DEBUG*/

  bk_patricia_vdelete_int(tree, tree, value);
}



/**
 * Delete a named node by value, recursive, slow, multiplish pass
 *
 * @param B Baka thread/global state
 * @param tree Patricia tree
 * @param node Current location
 * @param value Value pointer to look for and delete
 * @param -1 error
 * @param 0 did not find
 * @param 1 found
 */
static int bk_patricia_vdelete_int(struct bk_pnode *tree, struct bk_pnode *node, void *value)
{
  if (!node)
    return(0);

#ifdef BP_DEBUG2
  fprintf(stderr,"Looking node %p %.*s/%d %p\n",node,BROUNDUP(node->bp_bitlen),node->bp_prefix,node->bp_bitlen,node->bp_data);
#endif /*BP_DEBUG2*/

  // Is this the right node?
  if (node->bp_data == value)
  {
#ifdef BP_DEBUG2
    fprintf(stderr,"Found node, now delete it %s\n",node->bp_prefix);
#endif /*BP_DEBUG2*/
    // Found it, delete the node now that we know the key
    bk_patricia_delete(NULL, tree, node->bp_prefix, node->bp_bitlen);
    return(1);
  }

  if (bk_patricia_vdelete_int(tree, node->bp_children[0], value) > 0)
    return(1);
  if (bk_patricia_vdelete_int(tree, node->bp_children[1], value) > 0)
    return(1);

  return(0);
}



/**
 * Delete a named node
 *
 * @param B Baka thread/global state
 * @param tree Patricia tree
 * @param key Pointer to key data
 * @param keyblen Length (in bits) of key data.
 */
void bk_patricia_delete(bk_s B, struct bk_pnode *tree, u_char *key, u_short keyblen)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_pnode *child;

  if (!key || !tree)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  switch(bk_patricia_delete_int(tree,key,keyblen))
  {
  default:
  case 0:
    BK_VRETURN(B);				// Nothing to do
  case 1:					// Mark root of tree as unused/empty
    if (tree->bp_prefix)
      free(tree->bp_prefix);
    memset(tree,0,sizeof(*tree));
    BK_VRETURN(B);
  case 2:					// 0 child of root can move up
    child = tree->bp_children[0];
    break;
  case 3:					// 1 child of root can move up
    child = tree->bp_children[1];
    break;
  }

  // Is root node terminal (in use)
  if (tree->bp_data)
    BK_VRETURN(B);

  // Replace root node with child;
  if (tree->bp_prefix)
    free(tree->bp_prefix);
  *tree = *child;
  child->bp_prefix = NULL;			// Still in use
  bk_patricia_node_destroy(child);
  BK_VRETURN(B);
}



/**
 * Delete a named node, internal
 *
 * @param tree Patricia tree
 * @param key Pointer to key data
 * @param keyblen Length (in bits) of key data.
 * @return <i>-1</i> on error
 * @return <i>0</i> on success (including key does not exist)
 * @return <i>1</i> on success, but child you decended to is now useless
 * @return <i>2</i> on success, but child you decended to is now useless after you pull up 0 grandchild in its place (and free it)
 * @return <i>3</i> on success, but child you decended to is now useless after you pull up 1 grandchild in its place (and free it)
 */
static int bk_patricia_delete_int(struct bk_pnode *tree, u_char *key, u_short keyblen)
{
  u_short common_len = 0;
  short bit,cbit;
  struct bk_pnode *child;

  if (!key)
  {
    return(-1);
  }

  // Going down to a node which does not exist yet
  if (!tree)
    return(0);

  PREFIX_MATCH(tree->bp_prefix, tree->bp_bitlen, key, keyblen, &common_len);

  // Case one: exact match
  if (common_len == keyblen && common_len == tree->bp_bitlen)
  {
    tree->bp_data = NULL;			// Mark node as being non-terminal (in theory it might have been already)
    if (tree->bp_children[0] && tree->bp_children[1])
      return(0);				// Nothing we can do.  We are crit-bit internal. Let it slide
    if (!tree->bp_children[0] && !tree->bp_children[1])
      return(1);				// This is a useless leaf node.  Ask parent to dereference
    if (tree->bp_children[0])
      return(2);				// Pull up 0 child and nuke this node
    return(3);					// Pull up 1 child and nuke this node
  }

  // Case two: complete match of key, partial match of tree)
  if (common_len == keyblen)
    return(0);					// Did not find.  Nothing to do.

  bit = bp_bitof(key,common_len);

  // Case three: partial match of key, go down the tree
  switch (bk_patricia_delete_int(tree->bp_children[bit], key, keyblen))
  {
  default:					// Huh?
    return(-1);
  case 0:					// Didn't find key or found it and no further action required
    return(0);
  case 1:					// Found key, NULL out reference (parent might need to do something special)
    bk_patricia_node_destroy(tree->bp_children[bit]);
    tree->bp_children[bit] = NULL;
    if (!tree->bp_data)
    {
      cbit = !bit;
      if (tree->bp_children[cbit])
	return(cbit|2);				// We must preserve our child, but we are not needed
      else
	return(1);				// We are not needed any more
    }
    return(0);					// This is a terminal node.  Leave it alone

  case 2:					// Found key, pull 0 child up, delete child
    cbit=0;
    break;
  case 3:					// Found key, pull 1 child up, delete child
    cbit=1;
    break;
  }

  child = tree->bp_children[bit];
  tree->bp_children[bit] = child->bp_children[cbit];
  bk_patricia_node_destroy(child);

  return(0);
}



/**
 * Destroy a patricia node
 *
 * @param node Patricia node
 */
static void bk_patricia_node_destroy(struct bk_pnode *node)
{
  if (node->bp_prefix)
    free(node->bp_prefix);
  free(node);
}



/**
 * Destroy a patricia tree
 *
 * @param tree Patricia tree
 * @param freefun Function to free data, if tree is non-empty, optional
 */
void bk_patricia_destroy(struct bk_pnode *tree, void (*freefun)(void *))
{
  if (!tree)
    return;

  bk_patricia_destroy(tree->bp_children[0], freefun);
  bk_patricia_destroy(tree->bp_children[1], freefun);
  if (tree->bp_data && freefun)
    (*freefun)(tree->bp_data);
  bk_patricia_node_destroy(tree);
}




/**
 * Debug print of patricia tree
 *
 * @param B BAKA THread/global state
 * @param tree Patricia tree
 * @param F file handle
 * @param level recursion level (user call with zero)
 */
void bk_patricia_print(bk_s B, struct bk_pnode *tree, FILE *F, int level)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int l;
  int plussed = 0;

  if (!F)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid arguments\n");
    BK_VRETURN(B);
  }

  for (l=level;l>0;l--)
    fprintf(F,".");

  if (!tree)
  {
    fprintf(F,"(NULL)\n");
    BK_VRETURN(B);
  }

  fprintf(F,"%p %d 0x",tree->bp_data,tree->bp_bitlen);
  for(l=tree->bp_bitlen;l>0;)
  {
    int b = tree->bp_bitlen-l;
    if (l>=8)
    {
      fprintf(F,"%02x",tree->bp_prefix[b/8]);
      l -= 8;
      continue;
    }
    if (!plussed)				// We are nonplussed
    {
      fprintf(F,"+");
      plussed = 1;
    }
    fprintf(F,"%d",bp_bitof(tree->bp_prefix,b));
    l--;
  }
  fprintf(F,"\n");
  bk_patricia_print(B,tree->bp_children[0],F,level+1);
  bk_patricia_print(B,tree->bp_children[1],F,level+1);
  BK_VRETURN(B);
}
