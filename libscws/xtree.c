/**
 * @file xtree.c
 * @author Hightman Mar
 * @editor set number ; syntax on ; set autoindent ; set tabstop=4 (vim)
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include "compat.h"

#include "xtree.h"
#include "xdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* private static functions */
static int _xtree_key_cmp(node_t head, const char *key, scws_io_size_t len) {
	/*
		此函数用于取代_xtree_node_search的这一部分：
		int cmp;
		int hlen = strlen(head->key);
		int clen = len < hlen ? len : hlen;
		cmp = memcmp(key, head->key, clen);
		if (cmp == 0) cmp = len - hlen;
	*/
	for (scws_io_size_t i = 0; i < len; i++) {
		if (head->key[i] == 0) return 1; // 说明len - hlen > 0，返回正数
		int diff = (int)key[i] - (int)head->key[i];
		if (diff != 0) return diff;
	}
	if (head->key[len] == 0) return 0;
	else return -1; // 说明len - hlen < 0，返回负数
}

static scws_io_size_t _xtree_hasher(xtree_t xt, const char *s, scws_io_size_t len)
{
	scws_io_size_t h = xt->base;
	while (len--)
	{
		h += (h<<5);
		h ^= (unsigned char) s[len];
		h &= 0x7fffffff;
	}
	return (h % xt->prime);
}

static node_t _xtree_node_search(node_t head, node_t **pnode, const char *key, scws_io_size_t len)
{
	int cmp = _xtree_key_cmp(head, key, len);
	
	if (cmp != 0)	
	{
		node_t *next;

		next = (cmp > 0 ? &head->right : &head->left);
		if (*next == NULL)
		{
			if (pnode != NULL)
				*pnode = next;
			return NULL;
		}
		return _xtree_node_search(*next, pnode, key, len);
	}
	return head;
}

static node_t _xtree_node_find(xtree_t xt, node_t **pnode, const char *key, scws_io_size_t len)
{	
	int i;
	i = (xt->prime > 1 ? _xtree_hasher(xt, key, len) : 0);
	if (xt->trees[i] == NULL)
	{
		if (pnode != NULL) 
			*pnode = &xt->trees[i];
		return NULL;
	}
	return _xtree_node_search(xt->trees[i], pnode, key, len);
}

/* public functions */
xtree_t xtree_new(scws_io_size_t base, scws_io_size_t prime)
{
	xtree_t xnew;
	pool_t p;

	p = pool_new();
	xnew = pmalloc(p, sizeof(xtree_st));
	xnew->p = p;
	xnew->base = (base ? base : 0xf422f);
	xnew->prime = (prime ? prime :  31);
	xnew->count = 0;
	xnew->trees = (node_t *) pmalloc_z(p, sizeof(node_t) * xnew->prime);
	return xnew;
}

void xtree_free(xtree_t xt)
{
	if (xt)
		pool_free(xt->p);
}

void xtree_nput(xtree_t xt, void *value, scws_io_size_t vlen, const char *key, scws_io_size_t len)
{
	node_t node, *pnode;

	if (xt == NULL || key == NULL || len == 0)
		return;

	if ((node = _xtree_node_find(xt, &pnode, key, len)) != NULL)
	{
		node->value = value;
		node->vlen = vlen;
		return;
	}
	
	if (value != NULL)
	{	
		*pnode = node = (node_t) pmalloc(xt->p, sizeof(node_st));
		node->key = pstrndup(xt->p, key, len);
		node->value = value;
		node->vlen = vlen;
		node->left = NULL;
		node->right = NULL;
	}
}

void xtree_put(xtree_t xt, const char *value, const char *key)
{
	if (xt != NULL && key != NULL)
		xtree_nput(xt, (void *) value, value ? (scws_io_size_t)strlen(value) : 0, key, (scws_io_size_t)strlen(key));
}

void *xtree_nget(xtree_t xt, const char *key, scws_io_size_t len, scws_io_size_t *vlen)
{
	node_t node;

	if (xt == NULL || key == NULL || len == 0
		|| !(node = _xtree_node_find(xt, NULL, key, len)))
	{
		return NULL;
	}

	if (vlen != NULL)
		*vlen = node->vlen;
	return node->value;
}

void *xtree_get(xtree_t xt, const char *key, scws_io_size_t *vlen)
{
	if (xt == NULL || key == NULL)
		return NULL;
	
	return xtree_nget(xt, key, (scws_io_size_t)strlen(key), vlen);
}

/*
void xtree_ndel(xtree_t xt, const char *key, scws_io_size_t len)
{
	xtree_nput(xt, NULL, 0, key, len);
}

void xtree_del(xtree_t xt, const char *key)
{
	if (xt == NULL || key == NULL)
		return;
	
	xtree_ndel(xt, key, (scws_io_size_t)strlen(key));
}
*/

#ifdef DEBUG
/* draw the xtree to stdout */
struct draw_arg
{
	int depth;
	int count;
	int flag;
};

static void _xtree_draw_node(node_t node, struct draw_arg *arg, int depth, char *icon1)
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
	if (node == NULL)	
		printf("<NULL>\n");	
	else
	{
		printf("%s (value on 0x%x vlen=%d)\n", node->key, (unsigned int)node->value, node->vlen);
		
		arg->count++;
		depth++;
		if (depth > arg->depth) 
			arg->depth = depth;

		// draw the left & right
		arg->flag = 'L';
		_xtree_draw_node(node->left, arg, depth, icon2);

		arg->flag = 'R';
		_xtree_draw_node(node->right, arg, depth, icon2);
	}
	free(icon2);
}

void xtree_draw(xtree_t xt)
{
	int i;
	struct draw_arg arg;	

	if (!xt)
		return;	

	for (i = 0; i < xt->prime; i++)
	{		
		arg.depth = 0;
		arg.count = 0;
		arg.flag = 'T';
		_xtree_draw_node(xt->trees[i], &arg, 0, "");
		printf("-----------------------------------------\n");
		printf("Tree [%d] max_depth: %d nodes_num: %d\n", i, arg.depth, arg.count);
	}
}
#endif

/* optimize the tree */
static void _xtree_count_nodes_imp(node_t node, scws_io_size_t *count)
{
	if (node == NULL)
		return;

	*count += 1;
	_xtree_count_nodes_imp(node->left, count);
	_xtree_count_nodes_imp(node->right, count);
}

static scws_io_size_t _xtree_count_nodes(node_t node)
{
	scws_io_size_t cnt = 0;
	_xtree_count_nodes_imp(node, &cnt);
	return cnt;
}

static void _xtree_load_nodes_imp(node_t node, node_t *dst, scws_io_size_t *count)
{
	if (node == NULL) return;
	dst[*count] = node;
	*count += 1;
	_xtree_load_nodes_imp(node->left, dst, count);
	_xtree_load_nodes_imp(node->right, dst, count);
}

static scws_io_size_t _xtree_load_nodes(node_t head, node_t *dst)
{
	scws_io_size_t cnt = 0;
	_xtree_load_nodes_imp(head, dst, &cnt);
	return cnt;
}

static node_t _xtree_make_prefect_subtree(const node_t* ordered, unsigned char n_layer) {
	if (n_layer == 0) return NULL;
	if (n_layer == 1) return ordered[0];
	// 1 ~ 2**n-1
	for (scws_io_size_t i = 1; i >> n_layer == 0; i++) {
		node_t cur = ordered[i - 1];
		unsigned int n_tz = _bit_ctzll((unsigned long long)i);
		if (n_tz) {
			cur->left  = ordered[i - (1ULL << (n_tz - 1)) - 1];
			cur->right = ordered[i + (1ULL << (n_tz - 1)) - 1];
		} else {
			cur->left  = NULL;
			cur->right = NULL;
		}
	}
	return ordered[(1ULL << (n_layer - 1)) - 1];
}

static node_t _xtree_reorganize_nodes(const node_t *ordered, const scws_io_size_t count)
{	// TsXor: reorganize node with O(1) space and O(n) time
	unsigned char n_layer = 0;
	const node_t* sub = ordered + count;
	node_t head = NULL;
	for (scws_io_size_t left = count; left != 0; left >>= 1) {
		node_t last_head = head;
		head = sub[-1];
		sub -= 1ULL << n_layer;
		head->left = _xtree_make_prefect_subtree(sub, n_layer);
		head->right = last_head;
		n_layer++;
	}
	return head;
}

static int _xtree_node_cmp(a, b)
	const node_t *a, *b;
{
	return strcmp((*a)->key, (*b)->key);
}

void xtree_optimize(xtree_t xt)
{
	if (!xt) return;

	for (scws_io_size_t i = 0; i < xt->prime; i++)
	{
		node_t tree_head = xt->trees[i];
		scws_io_size_t n_tree_nodes = _xtree_count_nodes(tree_head);
		if (n_tree_nodes <= 2) continue;			
		
		node_t *nodes_array = (node_t *)malloc(sizeof(node_t) * n_tree_nodes);
		scws_io_size_t n_loaded = _xtree_load_nodes(tree_head, nodes_array);
		qsort(nodes_array, n_loaded, sizeof(node_t), _xtree_node_cmp);
		tree_head = _xtree_reorganize_nodes(nodes_array, n_loaded);
		free(nodes_array);
	}
}

/* convert xtree to xdb file */
static void _xtree_to_xdb_node(node_t node, xdb_t x)
{
	if (node == NULL)
		return;

	xdb_nput(x, node->value, node->vlen, node->key, (scws_io_size_t)strlen(node->key));
	_xtree_to_xdb_node(node->left, x);
	_xtree_to_xdb_node(node->right, x);
}

void xtree_to_xdb(xtree_t xt, const char *fpath)
{
	xdb_t x;

	if (!xt || !(x = xdb_create(fpath, xt->base, xt->prime)))
		return;

	for (scws_io_size_t i = 0; i < xt->prime; i++)
	{
		_xtree_to_xdb_node(xt->trees[i], x);
	}

	xdb_close(x);
}

