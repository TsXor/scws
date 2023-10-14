/**
 * @file xdb.c (xtree use file storage)
 * @author Hightman Mar
 * @editor set number ; syntax on ; set autoindent ; set tabstop=4 (vim)
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include "compat.h"

#include "xdb.h"
#include "lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#ifdef HAVE_FLOCK
#   include <sys/file.h>
#endif

#ifdef HAVE_MMAP
#   include <sys/mman.h>
#endif

static int _xdb_hasher(xdb_t x, const char *s, int len)
{
	unsigned int h = x->base;
	while (len--)
	{
		h += (h<<5);
		h ^= (unsigned char) s[len];
		h &= 0x7fffffff;
	}
	return (h % x->prime);
}

static void _xdb_read_data(xdb_t x, void *buf, scws_io_size_t off, scws_io_size_t len)
{
	/* check off & x->fsize? */
	if (off > x->fsize)
		return;
	
	/* fixed the len boundary!! */
	if ((off + len) > x->fsize)	
		len = x->fsize - off;

	if (x->fd >= 0)
	{
		lseek(x->fd, off, SEEK_SET);
		read(x->fd, buf, len);
	}
	else
	{
		memcpy(buf, x->fmap + off, len);
	}
        /* hightman.101230: fixed overflow, thanks to hovea on bbs */
	//memset((void *)((char *)buf + len), 0, 1);
}

/* recursive to search the matched record */
static void _xdb_rec_get(xdb_t x, xrec_t rec, const char *key, int len)
{
	unsigned char buf[XDB_MAXKLEN + 2];	// greater than: 255
	int cmp;

	if (rec->me.len == 0)
		return;

	// [left][right] = 16\0
	_xdb_read_data(x, buf, rec->me.off + 16, len + 1);
	cmp = memcmp(key, buf+1, len);
	if (!cmp)
		cmp = len - buf[0];	
	if (cmp > 0)
	{
		// right
		rec->poff = rec->me.off + sizeof(xptr_st);
		_xdb_read_data(x, &rec->me, rec->me.off + sizeof(xptr_st), sizeof(xptr_st));
		_xdb_rec_get(x, rec, key, len);
	}
	else if (cmp < 0)
	{
		// left
		rec->poff = rec->me.off;
		_xdb_read_data(x, &rec->me, rec->me.off, sizeof(xptr_st));
		_xdb_rec_get(x, rec, key, len);
	}
	else
	{
		// found!
		rec->value.off = rec->me.off + 17 + len;
		rec->value.len = rec->me.len - 17 - len;
	}
}

static xrec_t _xdb_rec_find(xdb_t x, const char *key, int len, xrec_t rec)
{	
	int i;
	
	if (rec == NULL)
		rec = (xrec_t) malloc(sizeof(xrec_st));

	memset(rec, 0, sizeof(xrec_st));
	i = (x->prime > 1 ? _xdb_hasher(x, key, len) : 0);
	rec->poff = i * sizeof(xptr_st) + sizeof(struct xdb_header);

	_xdb_read_data(x, &rec->me, rec->poff, sizeof(xptr_st));
	_xdb_rec_get(x, rec, key, len);
	return rec;
}

/* mode = r(readonly) | w(write&read) */
xdb_t xdb_open(const char *fpath, int mode)
{
	xdb_t x;
	struct stat st;
	struct xdb_header xhdr;

	/* create the new memory */
	if (!(x = (xdb_t ) malloc(sizeof(xdb_st))))
		return NULL;

	/* try to open & check the file */
	if ((x->fd = open(fpath, mode == 'w' ? O_RDWR : O_RDONLY)) < 0)
	{
#ifdef DEBUG
		perror("Failed to open the XDB file");
#endif
		free(x);
		return NULL;
	}

	/* check the file */
	if (fstat(x->fd, &st) || !S_ISREG(st.st_mode) || (x->fsize = st.st_size) <= 0)
	{
#ifdef DEBUG
		perror("Invalid XDB file");
#endif
		close(x->fd);
		free(x);
		return NULL;
	}

	/* check the XDB header: XDB+version(1bytes)+base+prime+fsize+<dobule check> = 19bytes */
	lseek(x->fd, 0, SEEK_SET);
	if ((read(x->fd, &xhdr, sizeof(xhdr)) != sizeof(xhdr))
		|| memcmp(xhdr.tag, XDB_TAGNAME, 3) || (xhdr.fsize != x->fsize))
	{
#ifdef DEBUG
		perror("Invalid XDB file format");
#endif
		close(x->fd);
		free(x);
		return NULL;
	}
	x->prime = xhdr.prime;
	x->base = xhdr.base;
	x->version = (scws_io_size_t) xhdr.ver;
	x->fmap = NULL;
	x->mode = mode;

	/* lock the file in write mode */
	if (mode == 'w')
		_xdb_flock(x->fd, LOCK_EX);
	/* try mmap if readonly */
#ifdef HAVE_MMAP
	else
	{
		x->fmap = (char *) mmap(NULL, x->fsize, PROT_READ, MAP_SHARED, x->fd, 0);
		close(x->fd);
		x->fd = -1;

		if (x->fmap == (char *) MAP_FAILED)
		{
#ifdef DEBUG
			perror("Mmap() failed");
#endif
			free(x);
			return NULL;
		}
	}
#endif
	return x;
}

xdb_t xdb_create(const char *fpath, scws_io_size_t base, scws_io_size_t prime)
{
	xdb_t x;
	struct xdb_header xhdr;

	/* create the new memory */
	if (!(x = (xdb_t ) malloc(sizeof(xdb_st))))
		return NULL;

	/* try to open & check the file */
	if ((x->fd = open(fpath, (O_CREAT|O_RDWR|O_TRUNC|O_EXCL), 0600)) < 0)
	{
#ifdef DEBUG
		perror("Failed to open & create the db file");
#endif
		free(x);
		return NULL;
	}

	/* write the header */
	_xdb_flock(x->fd, LOCK_EX);
	x->prime = prime ? prime : 2047;
	x->base = base ? base : 0xf422f;
	x->fsize = sizeof(xhdr) + x->prime * sizeof(xptr_st);
	x->fmap = NULL;
	x->mode = 'w';
	memset(&xhdr, 0, sizeof(xhdr));
	memcpy(&xhdr.tag, XDB_TAGNAME, 3);
	xhdr.ver = XDB_VERSION;
	xhdr.prime = x->prime;
	xhdr.base = x->base;
	xhdr.fsize = x->fsize;
	xhdr.check = (float)XDB_FLOAT_CHECK;

	/* check the XDB header: XDB+version(1bytes)+base+prime+fsize+<dobule check> = 19bytes */
	lseek(x->fd, 0, SEEK_SET);
	write(x->fd, &xhdr, sizeof(xhdr));
	return x;
}

void xdb_close(xdb_t x)
{
	if (x == NULL)
		return;

#ifdef HAVE_MMAP
	if (x->fmap != NULL)
	{		
		munmap(x->fmap, x->fsize);
		x->fmap = NULL;
	}
#endif

	if (x->fd >= 0)
	{
		if (x->mode == 'w')
		{		
			lseek(x->fd, 12, SEEK_SET);
			write(x->fd, &x->fsize, sizeof(x->fsize));
			_xdb_flock(x->fd, LOCK_UN);
		}
		close(x->fd);
		x->fd = -1;
	}
	free(x);
}

/* read mode (value require free by user) */
void *xdb_nget(xdb_t x, const char *key, scws_io_size_t len, scws_io_size_t *vlen)
{
	xrec_st rec;
	void *value = NULL;

	if (x == NULL || key == NULL || len > XDB_MAXKLEN)
		return NULL;

	/* not found, return the poff(for write) */
	_xdb_rec_find(x, key, len, &rec);
	if (rec.value.len > 0)
	{
		/* auto append one byte with '\0' */		
		value = malloc(rec.value.len + 1);
		if (vlen != NULL)		
			*vlen = rec.value.len;
		_xdb_read_data(x, value, rec.value.off, rec.value.len);
                *((char *)value + rec.value.len) = '\0';
	}	
	return value;
}

void *xdb_get(xdb_t x, const char *key, scws_io_size_t *vlen)
{
	if (x == NULL || key == NULL)
		return NULL;
	return xdb_nget(x, key, (scws_io_size_t)strlen(key), vlen);
}

/* write mode */
void xdb_nput(xdb_t x, void *value, scws_io_size_t vlen, const char *key, scws_io_size_t len)
{
	xrec_st rec;

	if (x == NULL || x->fd < 0 || key == NULL || len > XDB_MAXKLEN)
		return;

	/* not found, return the poff(for write) */	
	_xdb_rec_find(x, key, len, &rec);
	if (rec.value.len > 0 && vlen <= rec.value.len)
	{
		/* just replace */
		if (vlen > 0)
		{		
			lseek(x->fd, rec.value.off, SEEK_SET);
			write(x->fd, value, vlen);
		}
		if (vlen < rec.value.len)
		{
			vlen += rec.me.len - rec.value.len;
			lseek(x->fd, rec.poff + 4, SEEK_SET);
			write(x->fd, &vlen, sizeof(vlen));
		}
	}
	else if (vlen > 0)
	{
		/* insert for new data */
		unsigned char buf[512];
		xptr_st pnew;

		pnew.off = x->fsize;		
		memset(buf, 0, sizeof(buf));
		pnew.len = rec.me.len - rec.value.len;
		if (pnew.len > 0)
		{
			_xdb_read_data(x, buf, rec.me.off, pnew.len);
		}
		else
		{
			buf[16] = len;	// key len
			strncpy(buf + 17, key, len);
			pnew.len = 17 + len;
		}
		lseek(x->fd, pnew.off, SEEK_SET);
		write(x->fd, buf, pnew.len);
		write(x->fd, value, vlen);
		pnew.len += vlen;
		x->fsize += pnew.len;

		/* update noff & vlen -> poff */
		lseek(x->fd, rec.poff, SEEK_SET);
		write(x->fd, &pnew, sizeof(pnew));
	}
}

void xdb_put(xdb_t x, const char *value, const char *key)
{
	if (x == NULL || key == NULL)
		return;	
	xdb_nput(x, (void *) value, value ? strlen(value) : 0, key, strlen(key));
}

#ifdef DEBUG
/* draw the xdb to stdout */
struct draw_arg
{
	int depth;
	int count;
	int flag;
};

static void _xdb_draw_node(xdb_t x, xptr_t ptr, struct draw_arg *arg, int depth, char *icon1)
{
	char *icon2;
	
	icon2 = malloc(strlen(icon1) + 4);
	strcpy(icon2, icon1);

	// output the flag & icon
	if (arg->flag == 'T')	
		printf("(Ｔ) ");	
	else
	{
		printf("%s", icon2);
		if (arg->flag  == 'L')
		{
			strcat(icon2, " ┃");
			printf(" ┟(Ｌ) ");
		}
		else
		{
			strcat(icon2, " 　");
			printf(" └(Ｒ) ");
		}
	}

	// draw the node data
	if (ptr->len == 0)
		printf("<NULL>\n");	
	else
	{
		unsigned char buf[XDB_MAXKLEN + 18];		// greater than 18 = sizeof(xptr_st)*2+1
		int vlen, voff;

		vlen = sizeof(buf) - 1;
		if (vlen > ptr->len)
			vlen = ptr->len;

		_xdb_read_data(x, buf, ptr->off, vlen);
		vlen = ptr->len - buf[16] - 17;
		voff = ptr->off + buf[16] + 17;

		printf("%.*s (vlen=%d, voff=%d)\n", buf[16], buf+17, vlen, voff);

		arg->count++;
		depth++;
		if (depth > arg->depth)
			arg->depth = depth;

		// draw the left & right;
		arg->flag = 'L';
		memcpy(ptr, buf, sizeof(xptr_st));
		_xdb_draw_node(x, ptr, arg, depth, icon2);

		arg->flag = 'R';
		memcpy(ptr, buf + sizeof(xptr_st), sizeof(xptr_st));
		_xdb_draw_node(x, ptr, arg, depth, icon2);
	}
	free(icon2);
}

void xdb_draw(xdb_t x)
{
	int i;
	struct draw_arg arg;
	xptr_st ptr;

	if (!x) return;

	xdb_version(x);
	for (i = 0; i < x->prime; i++)
	{		
		arg.depth = 0;
		arg.count = 0;
		arg.flag = 'T';

		_xdb_read_data(x, &ptr, i * sizeof(xptr_st) + sizeof(struct xdb_header), sizeof(xptr_st));
		_xdb_draw_node(x, &ptr, &arg, 0, "");

		printf("-----------------------------------------\n");
		printf("Tree(xdb) [%d] max_depth: %d nodes_num: %d\n", i, arg.depth, arg.count);
	}
}
#endif

/* optimize the xdb */
typedef struct xdb_cmper
{
	xptr_st ptr;
	char *key;
}	xcmper_st;

#pragma pack(push, 1)
struct _xdb_node_data {
	xptr_st left;
	xptr_st right;
	unsigned char klen;
	char key[]; // null-terminated
};
#pragma pack(pop)

static void _xdb_count_nodes_imp(xdb_t x, xptr_t ptr, scws_io_size_t *count, scws_io_size_t *klen)
{
	if (ptr->len == 0) return;

	*count += 1;
	scws_io_size_t off = ptr->off;
	unsigned char cur_klen;
	_xdb_read_data(x, &cur_klen, off + offsetof(struct _xdb_node_data, klen), sizeof(unsigned char));
	*klen += cur_klen + 1;

	/* left & right */
	_xdb_read_data(x, ptr, off + offsetof(struct _xdb_node_data, left), sizeof(xptr_st));
	_xdb_count_nodes_imp(x, ptr, count, klen);
	_xdb_read_data(x, ptr, off + offsetof(struct _xdb_node_data, right), sizeof(xptr_st));
	_xdb_count_nodes_imp(x, ptr, count, klen);
}

struct _xdb_node_cnt {
	scws_io_size_t n;
	scws_io_size_t klen;
};

static struct _xdb_node_cnt _xdb_count_nodes(xdb_t x, scws_io_size_t idx) {
	xptr_st head;
	scws_io_size_t cnt = 0;
	scws_io_size_t klen_sum = 0;
	scws_io_size_t poff = sizeof(struct xdb_header) + idx * sizeof(xptr_st);
	_xdb_read_data(x, &head, poff, sizeof(xptr_st));
	_xdb_count_nodes_imp(x, &head, &cnt, &klen_sum);
	struct _xdb_node_cnt ret = {cnt, klen_sum};
	return ret;
}

static void _xdb_load_nodes_imp(xdb_t x, xptr_t ptr, xcmper_st *dst, scws_io_size_t *count, char *keys, scws_io_size_t *keys_used)
{
	if (ptr->len == 0) return;

	unsigned char node_data[sizeof(struct _xdb_node_data) + XDB_MAXKLEN + 1];
	_xdb_read_data(x, node_data, ptr->off, ptr->len);
	struct _xdb_node_data* node = (struct _xdb_node_data* )&node_data;

	*count += 1;
	char* cur_key = keys + *keys_used;
	memcpy(cur_key, &node->key, node->klen);
	cur_key[node->klen] = 0;
	*keys_used += node->klen + 1;
	dst[*count].ptr = *ptr;
	dst[*count].key = cur_key;

	/* left & right */
	memcpy(ptr, &node->left, sizeof(xptr_st));
	_xdb_load_nodes_imp(x, ptr, dst, count, keys, keys_used);

	memcpy(ptr, &node->right, sizeof(xptr_st));
	_xdb_load_nodes_imp(x, ptr, dst, count, keys, keys_used);
}

static scws_io_size_t _xdb_load_nodes(xdb_t x, scws_io_size_t idx, xcmper_st *dst, char *keys) {
	xptr_st head;
	scws_io_size_t cnt = 0, keys_used = 0;
	scws_io_size_t poff = sizeof(struct xdb_header) + idx * sizeof(xptr_st);
	_xdb_read_data(x, &head, poff, sizeof(xptr_st));
	_xdb_load_nodes_imp(x, &head, dst, &cnt, keys, &keys_used);
	return cnt;
}

static const xptr_st null_file_ptr = {0, 0};

static const xcmper_st* _xdb_make_prefect_subtree(int fd, scws_io_size_t poff, const xcmper_st* ordered, unsigned char n_layer)
{
	if (n_layer == 0) return NULL;
	if (n_layer == 1) return ordered;
	// 1 ~ 2**n-1
	for (scws_io_size_t i = 1; i >> n_layer == 0; i++) {
		const xcmper_st* cur = &ordered[i - 1];
		unsigned int n_tz = _bit_ctzll((unsigned long long)i);
		if (n_tz) {
			lseek(fd, poff + cur->ptr.off + offsetof(struct _xdb_node_data, left), SEEK_SET);
			write(fd, &ordered[i - (1ULL << (n_tz - 1)) - 1].ptr, sizeof(xptr_st)); // left
			lseek(fd, poff + cur->ptr.off + offsetof(struct _xdb_node_data, right), SEEK_SET);
			write(fd, &ordered[i + (1ULL << (n_tz - 1)) - 1].ptr, sizeof(xptr_st)); // right
		} else {
			lseek(fd, poff + cur->ptr.off + offsetof(struct _xdb_node_data, left), SEEK_SET);
			write(fd, &null_file_ptr, sizeof(xptr_st)); // left = NULL
			lseek(fd, poff + cur->ptr.off + offsetof(struct _xdb_node_data, right), SEEK_SET);
			write(fd, &null_file_ptr, sizeof(xptr_st)); // right = NULL
		}
	}
	return &ordered[(1ULL << (n_layer - 1)) - 1];
}

static const xcmper_st* _xdb_reorganize_nodes(xdb_t x, scws_io_size_t idx, xcmper_st *ordered, scws_io_size_t count)
{
	scws_io_size_t poff = sizeof(struct xdb_header) + idx * sizeof(xptr_st);
	// TsXor: reorganize node with O(1) space and O(n) time
	unsigned char n_layer = 0;
	const xcmper_st* sub = ordered + count;
	const xcmper_st* head = NULL;
	for (scws_io_size_t left = count; left != 0; left >>= 1) {
		const xcmper_st* last_head = head;
		head = &sub[-1];
		sub -= 1ULL << n_layer;
		const xcmper_st* subtree = _xdb_make_prefect_subtree(x->fd, poff, sub, n_layer);
		lseek(x->fd, poff + head->ptr.off + offsetof(struct _xdb_node_data, left), SEEK_SET);
		write(x->fd, &subtree->ptr, sizeof(xptr_st)); // left
		lseek(x->fd, poff + head->ptr.off + offsetof(struct _xdb_node_data, right), SEEK_SET);
		write(x->fd, &last_head->ptr, sizeof(xptr_st)); // right
		n_layer++;
	}
	return head;
}

static int _xdb_node_cmp(a, b)
	const xcmper_st *a, *b;
{
	return strcmp(a->key, b->key);
}

void xdb_optimize(xdb_t x)
{
	if (x == NULL || x->fd < 0) return;	

	for (scws_io_size_t i = 0; i < x->prime; i++)
	{
		struct _xdb_node_cnt counts = _xdb_count_nodes(x, i);
		if (counts.n <= 2) continue;

		xcmper_st *nodes_array = (xcmper_st *) malloc(sizeof(xcmper_st) * counts.n);
		// TsXor: allocate spaces for all keys in one go
		char *keys_buf = (char *) malloc(counts.klen);
		scws_io_size_t n_loaded = _xdb_load_nodes(x, i, nodes_array, keys_buf);
		qsort(nodes_array, n_loaded, sizeof(xcmper_st), _xdb_node_cmp);
		_xdb_reorganize_nodes(x, i, nodes_array, n_loaded);

		/* free the nodes & key pointer */
		free(keys_buf);
		free(nodes_array);
	}
}

void xdb_version(xdb_t x)
{
	printf("%s/%d.%d (base=%d, prime=%d)\n", XDB_TAGNAME,
		(x->version >> 5), (x->version & 0x1f), x->base, x->prime);
}

/* convert xdb file to xtree struct(memory) */
static void _xdb_to_xtree_node(xdb_t x, xtree_t xt, xptr_t ptr)
{
	unsigned char *buf;
	void *value;
	int voff;

	if (ptr->len == 0)
		return;

	buf = (unsigned char *) malloc(ptr->len + 1);
	_xdb_read_data(x, buf, ptr->off, ptr->len);

	/* save the key & value -> xtree */
	voff = buf[16] + 17;

	/* 2009-09-22, 11:29, Mistruster: posted on bbs */
	if (voff >= (int)ptr->len)
		return;
	value = pmalloc(xt->p, ptr->len - voff);
	memcpy(value, buf + voff, ptr->len - voff);
	xtree_nput(xt, value, ptr->len - voff, buf + 17, buf[16]);

	/* left & right */
	memcpy(ptr, buf, sizeof(xptr_st));
	_xdb_to_xtree_node(x, xt, ptr);

	memcpy(ptr, buf + sizeof(xptr_st), sizeof(xptr_st));
	_xdb_to_xtree_node(x, xt, ptr);

	free(buf);
}

xtree_t xdb_to_xtree(xdb_t x, xtree_t xt)
{
	scws_io_size_t i = 0;
	xptr_st ptr;

	if (!x)
		return NULL;

	if (!xt && !(xt = xtree_new(x->base, x->prime)))
		return NULL;
	
    do
	{
		_xdb_read_data(x, &ptr, i * sizeof(xptr_st) + sizeof(struct xdb_header), sizeof(xptr_st));
		_xdb_to_xtree_node(x, xt, &ptr);
	}
    while (++i < x->prime);

	return xt;
}

