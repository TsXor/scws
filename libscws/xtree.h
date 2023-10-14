/**
 * @file xtree.h
 * @author Hightman Mar
 * @editor set number ; syntax on ; set autoindent ; set tabstop=4 (vim)
 * $Id$
 */

#ifndef	_SCWS_XTREE_20070525_H_
#define	_SCWS_XTREE_20070525_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "xsize.h"

// mini copy of https://github.com/mpdn/bitcount
// put in header to share with xdb
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#pragma intrinsic(_BitScanForward)
#endif

static inline unsigned int _bit_ctzll(unsigned long long v) {
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned int)__builtin_ctzll(v);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	unsigned long result;
	#if defined(_M_X64)
	_BitScanForward64(&result, v);
	#else
	_BitScanForward(&result, v);
	#endif
	return result;
#else
	//see http://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
	static const unsigned int MultiplyDeBruijnBitPosition[32] = 
	{
		0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 
		31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
	};
	return MultiplyDeBruijnBitPosition[((uint32_t)((v & -(int)v) * 0x077CB531U)) >> 27];
#endif
}

/* pool required */
#include "pool.h"

/* data structure for Hash+Tree */
typedef struct tree_node node_st, *node_t;
struct tree_node
{
	char *key;
	void *value;
	scws_io_size_t vlen;
	node_t left;
	node_t right;
};

typedef struct 
{	
	pool_t p;		/* pool for memory manager */
	scws_io_size_t base;	/* base number for hasher (prime number recommend) */
	scws_io_size_t prime;	/* good prime number for hasher */
	scws_io_size_t count;	/* total nodes */
	node_t *trees;	/* trees [total=prime+1] */
}	xtree_st, *xtree_t;

/* xtree: api */
// int xtree_hasher(xtree_t xt, const char *key, scws_io_size_t len);
xtree_t xtree_new(scws_io_size_t base, scws_io_size_t prime);	/* create a new hasxtree */
void xtree_free(xtree_t xt);			/* delete & free xthe xtree */

void xtree_put(xtree_t xt, const char *value, const char *key);
void xtree_nput(xtree_t xt, void *value, scws_io_size_t vlen, const char *key, scws_io_size_t len);

void *xtree_get(xtree_t xt, const char *key, scws_io_size_t *vlen);
void *xtree_nget(xtree_t xt, const char *key, scws_io_size_t len, scws_io_size_t *vlen);

/*
void xtree_del(xtree_t xt, const char *key);
void xtree_ndel(xtree_t xt, const char *key, int len);
*/

#ifdef DEBUG
void xtree_draw(xtree_t xt);
#endif

void xtree_optimize(xtree_t xt);
void xtree_to_xdb(xtree_t xt, const char *fpath);

#ifdef __cplusplus
}
#endif

#endif
