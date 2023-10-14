/**
 * @file pool.h
 * @author Hightman Mar
 * @editor set number ; syntax on ; set autoindent ; set tabstop=4 (vim)
 * $Id$
 */

#ifndef	_SCWS_POOL_20070525_H_
#define	_SCWS_POOL_20070525_H_

#include "xsize.h"

#ifdef __cplusplus
extern "C" {
#endif

/* block size for pool */
#define	POOL_BLK_SIZ	4096

/* data structure for pool */
struct pheap
{
	scws_max_size_t size;
	scws_max_size_t used;
	char block[0];
};

struct pclean
{
	void *obj;
	struct pclean *nxt;
};

typedef struct
{
	scws_max_size_t size;			/* total allocated */
	scws_max_size_t dirty;			/* total wasted */
	struct pheap *heap;	
	struct pclean *clean;
}	pool_st, *pool_t;

/* pool: api */
pool_t pool_new();								/* create a new memory pool with an initial heap size */
void pool_free(pool_t p);						/* frees all the data on the pool & delete the pool itself */
void *pmalloc(pool_t p, scws_max_size_t size);			/* wrapper around malloc, takes from the pool */
void *pmalloc_x(pool_t p, scws_max_size_t size, char c);	/* wrapper around pmalloc which prefills buffer with c */
void *pmalloc_z(pool_t p, scws_max_size_t size);			/* wrapper around pmalloc, which prefills by zero */
char *pstrdup(pool_t p, const char *s);			/* wrapper around strdup, gains the mem from the pool */
char *pstrndup(pool_t p, const char *s, scws_max_size_t l);

#ifdef __cplusplus
}
#endif

#endif
