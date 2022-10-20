/*
 * Copyright (c) 2009-2022 Michael P. Touloumtzis.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>

#include <util/memutil.h>
#include <util/message.h>

#include "heap.h"
#include "node.h"

/*
 * We could easily make the heap more dynamic, but as SLC is meant as a
 * prototype/proof of concept, it's not clear it's worth it (yet).
 */
#define MAX_NODES 1000000

static struct node the_nodes [MAX_NODES];

static struct node *the_free_list,
		   *the_next_node = the_nodes,
		   *the_node_bound;

struct heap_stats {
	unsigned long node_allocs, node_frees;
};

static struct heap_stats the_heap_stats;

void node_heap_init(void)
{
	the_node_bound = the_next_node + MAX_NODES;
}

struct node *node_heap_alloc(void)
{
	the_heap_stats.node_allocs++;
	struct node *node = the_free_list;
	if (node) {
		the_free_list = the_free_list->prev;
		goto done;
	}
	if (the_next_node >= the_node_bound)
		panic("Node heap exhausted!\n");
	node = the_next_node++;
	node->bits = NODE_INVALID;
done:
	node->prev = NULL;	/* for safety */
	return node;
}

void node_heap_free(struct node *node)
{
	the_heap_stats.node_frees++;
	assert(node);
	node->bits = NODE_INVALID;
	node->prev = the_free_list;
	the_free_list = node;
}

static unsigned long free_list_length(void)
{
	const struct node *p;
	unsigned long i;
	for (p = the_free_list, i = 0; p; p = p->prev, ++i);
	return i;
}

void print_heap_stats(void)
{
	printf(
	"\t\t\tHEAP STATISTICS\n"
	"\t\t\t===============\n"
	"Nodes:\t%12s %-10lu %12s %-10lu %12s %-10lu\n"
	      "\t%12s %-10lu %12s %-10lu\n",
	"total",	(unsigned long) MAX_NODES,
	"untouched",	(unsigned long) (the_node_bound - the_next_node),
	"free_list",	free_list_length(),
	"allocs",	the_heap_stats.node_allocs,
	"frees",	the_heap_stats.node_frees);
}
