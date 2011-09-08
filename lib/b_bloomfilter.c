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
 *
 *
 * This code is derivative of the Cassandra code, which is licensed under
 * the Apache License version 2.0 (http://www.apache.org/licenses/).
 */

/**
 * @file
 * Bloom Filter implementation, intended to be compatible with the java
 * implementation in Apache Cassandra.
 */

#include <libbk.h>


/**
 * Bloom filter structure
 */
struct bk_bloomfilter
{
  int32_t	bf_hash_count;			///< Number of hash functions used.
  int64_t	bf_words;			///< Number of 64-bit words in bloom filter.
  int64_t	bf_bits;			///< Number of bits in bloom filter.
  uint64_t     *bf_bitset;			///< The bitfield containing the data.
  int	        bf_fd;				///< fd of mmapped file
  bk_flags	bf_flags;			///< Flags
#define BF_FLAG_ALLOCATED	0x01		///< bitset was allocated (as opposed to mmap'd)
#define BF_FLAG_MMAPPED		0x02		///< bitset was mmaped
};



inline static void set_bit(uint64_t *bits, int64_t length, int64_t idx);
inline static uint8_t get_bit(uint64_t *bits, int64_t length, int64_t idx);



/**
 * Create a bloom filter.
 *
 * @param B BAKA Thread/global state
 * @param hashes number of hash functions to use
 * @param bits length of the filter in bits (may be rounded up)
 * @param filename If non-null, contains a filename of a file containing the bitset, which will be mmapped
 * @return <i>new bloomfilter</i> on success
 * @return <i>NULL</i> on failure
 */
struct bk_bloomfilter *bk_bloomfilter_create(bk_s B, int32_t hashes, int64_t bits, const char *filename)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  struct bk_bloomfilter *bf = NULL;

  if ((hashes < 1) || (bits < 1))
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid bloom filter parameters.\n");
    BK_RETURN(B, NULL);
  }

  if (!BK_CALLOC_LEN(bf, sizeof(struct bk_bloomfilter)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory for bloom filter metadata.\n");
    goto error;
  }
  bf->bf_fd = -1;

  bf->bf_hash_count = hashes;
  bf->bf_bits = bits;
  bf->bf_words = ((bits - 1) >> 6) + 1;

  if (!filename)
  {
    if (!BK_CALLOC_LEN(bf->bf_bitset, bf->bf_words * sizeof(uint64_t)))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not allocate memory for bloom filter.\n");
      goto error;
    }
    BK_FLAG_SET(bf->bf_flags, BF_FLAG_ALLOCATED);
  }
  else
  {
    if ((bf->bf_fd = open(filename, O_RDONLY)) < 0)
    {
      bk_error_printf(B, BK_ERR_ERR, "Error opening bloom filter file: %s\n", strerror(errno));
      goto error;
    }

    if ((bf->bf_bitset = mmap(0, bf->bf_words * sizeof(uint64_t), PROT_READ, MAP_SHARED, bf->bf_fd, 0)) == MAP_FAILED)
    {
      bk_error_printf(B, BK_ERR_ERR, "Error mapping bloom filter file: %s\n", strerror(errno));
      goto error;
    }
    BK_FLAG_SET(bf->bf_flags, BF_FLAG_MMAPPED);
  }

  BK_RETURN(B, bf);

 error:
  if (bf)
    bk_bloomfilter_destroy(B, bf);

  BK_RETURN(B, NULL);
}



/**
 * Destroy buffer and management
 *
 * @param B BAKA Thread/global state
 * @@param bm Buffer management handle
 */
void bk_bloomfilter_destroy(bk_s B, struct bk_bloomfilter *bf)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");

  if (bf)
  {
    if (bf->bf_bitset)
    {
      if (BK_FLAG_ISSET(bf->bf_flags, BF_FLAG_ALLOCATED))
      {
	free(bf->bf_bitset);
      }
      else if (BK_FLAG_ISSET(bf->bf_flags, BF_FLAG_MMAPPED))
      {
	munmap(bf->bf_bitset, bf->bf_words * sizeof(int64_t));
      }
    }

    if (bf->bf_fd > -1)
      close(bf->bf_fd);

    free(bf);
  }

  BK_VRETURN(B);
}



/**
 * Set a bit in a bitset.
 *
 * @param bits bitset
 * @param length length (in 64-bit words) of the bitset
 * @param idx index of the bit to set
 */
inline static void set_bit(uint64_t *bits, int64_t length, int64_t idx)
{
  int64_t word;
  uint8_t bit;
  uint64_t bitmask;

  word = idx >> 6; // div 64
  if (word >= length)
    return;
  bit = idx & 0x3f; // mod 64
  bitmask = 1L << bit;
  bits[word] |= bitmask;

  return;
}




/**
 * Get a bit in a bitset.
 *
 * @param bits bitset
 * @param length length (in 64-bit words) of the bitset
 * @param index index of the bit to set
 * @return <i>1</i> if set
 * @return <i>0</i> if not set
 */
inline static uint8_t get_bit(uint64_t *bits, int64_t length, int64_t idx)
{
  int64_t word;
  uint8_t bit;
  uint64_t bitmask;

  word = (int)(idx >> 6); // div 64
  if (word >= length)
    return 0;
  bit = (uint8_t) (idx & 0x3f); // mod 64
  bitmask = 1L << bit;
  return (bits[word] & bitmask) != 0;
}



/**
 * Add a key to a bloom filter.
 *
 * @param B BAKA Thread/global state
 * @param bf the bloom filter
 * @param key string to hash
 * @param len length of key in bytes
 * @return <i>0</i> on success
 * @return <i>-1</i> on failure
 */
int bk_bloomfilter_add(bk_s B, struct bk_bloomfilter *bf, const void *key, const int len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int64_t hash[2];
  int32_t i;

  if (!bf || !bf->bf_bitset || !key || !len)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  murmurhash3_x64_128(key, len, 0L, &hash);
  for (i=0; i<bf->bf_hash_count; i++)
  {
    set_bit(bf->bf_bitset, bf->bf_words, llabs((hash[0] + ((int64_t)i) * hash[1]) % bf->bf_bits));
  }

  BK_RETURN(B, 0);
}



/**
 * Check for a key in a bloom filter.
 *
 * @param B BAKA Thread/global state
 * @param bf the bloom filter
 * @param key string to hash
 * @param len length of key in bytes
 * @return <i>1</i> on success and key was present
 * @return <i>0</i> on success and key was not present
 * @return <i>-1</i> on failure
 */
int bk_bloomfilter_is_present(bk_s B, struct bk_bloomfilter *bf, const void *key, const int len)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int64_t hash[2];
  int32_t i;

  if (!bf || !bf->bf_bitset || !key || !len)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_RETURN(B, -1);
  }

  murmurhash3_x64_128(key, len, 0L, &hash);
  for (i=0; i<bf->bf_hash_count; i++)
  {
    if (!get_bit(bf->bf_bitset, bf->bf_words, llabs((hash[0] + ((int64_t)i) * hash[1]) % bf->bf_bits)))
      BK_RETURN(B, 0);
  }

  BK_RETURN(B, 1);
}



/**
 * Print bloom bits set in key
 *
 * @param B BAKA Thread/global state
 * @param bf the bloom filter
 * @param key string to hash
 * @param len length of key in bytes
 * @param fh File Handle to print bits to
 */
void bk_bloomfilter_printkey(bk_s B, struct bk_bloomfilter *bf, const void *key, const int len, FILE *fh)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbk");
  int64_t hash[2];
  int32_t i;

  if (!bf || !bf->bf_bitset || !key || !len || !fh)
  {
    bk_error_printf(B, BK_ERR_ERR, "Internal error: invalid arguments\n");
    BK_VRETURN(B);
  }

  murmurhash3_x64_128(key, len, 0L, &hash);
  for (i=0; i<bf->bf_hash_count; i++)
  {
    fprintf(fh, "%lld%s", llabs((hash[0] + ((int64_t)i) * hash[1]) % bf->bf_bits), (i+1 >= bf->bf_hash_count)?".":", ");
  }

  BK_VRETURN(B);
}
