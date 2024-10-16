/*
 * Copyright (c) 2009-2023 Michael P. Touloumtzis.
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
 * Nodes in MLC are variable-sized and we have reference-counted
 * garbage collection, so rather than a GC'd heap or preallocated
 * array, I'm going to make libc malloc/free work for a living.
 */
#define MAX_NODES 1000000

struct heap_stats {
	unsigned long node_allocs, node_frees, nodes_in_use;
};

#define MAX_HEAP_PRESSURE 0.9
#define MIN_HEAP_THRESHOLD 0.4

float the_heap_pressure, the_heap_threshold = 0.6;

static struct heap_stats the_heap_stats;

static inline void update_heap_pressure(void)
{
	the_heap_pressure = (float) the_heap_stats.nodes_in_use /
			    (float) MAX_NODES;
	if (the_heap_pressure > MAX_HEAP_PRESSURE)
		the_heap_pressure = MAX_HEAP_PRESSURE;
}

void node_heap_init(void)
{
	update_heap_pressure();
}

void node_heap_calibrate(void)
{
	update_heap_pressure();
	assert(the_heap_pressure >= 0.0);
	assert(the_heap_pressure <  1.0);
	assert(the_heap_threshold >= MIN_HEAP_THRESHOLD);
	assert(the_heap_threshold <  1.0);
	if (the_heap_pressure > the_heap_threshold * 0.666)
		the_heap_threshold += (1.0 - the_heap_threshold) / 2.0;
	else if (the_heap_pressure < the_heap_threshold * 0.333) {
		the_heap_threshold *= 0.666;
		if (the_heap_threshold < MIN_HEAP_THRESHOLD)
			the_heap_threshold = MIN_HEAP_THRESHOLD;
	}
}

struct node *node_heap_alloc(size_t nslots)
{
	if (the_heap_stats.nodes_in_use >= MAX_NODES)
		panic("Node heap exhausted!\n");
	the_heap_stats.node_allocs++;
	the_heap_stats.nodes_in_use++;
	update_heap_pressure();
#ifdef SLOT_COUNTS_SORTED
	struct node *node = xmalloc(sizeof *node +
				    nslots * sizeof node->slots[0]);
#else
	struct node *node = xmalloc(sizeof *node +
				    (nslots < 2 ? 2 : nslots) *
				    sizeof node->slots[0]);
#endif
	node->nslots = nslots;
	node->prev = NULL;	/* for safety */
	return node;
}

void node_heap_free(struct node *node)
{
	if (the_heap_stats.nodes_in_use == 0)
		panic("Nodes in use would fall below zero!\n");
	the_heap_stats.node_frees++;
	the_heap_stats.nodes_in_use--;
	update_heap_pressure();
	assert(node);
	xfree(node);
}

void print_heap_stats(void)
{
	fprintf(stderr,
	"\t\t\tHEAP STATISTICS\n"
	"\t\t\t===============\n"
	"Nodes:\t%12s %-10lu %12s %-10lu\n"
	      "\t%12s %-10lu %12s %-10lu\n"
	"Usage:\t%12s %-10g %12s %-10g\n",
	"total",	(unsigned long) MAX_NODES,
	"in_use",	the_heap_stats.nodes_in_use,
	"allocs",	the_heap_stats.node_allocs,
	"frees",	the_heap_stats.node_frees,
	"pressure",	the_heap_pressure,
	"threshold",	the_heap_threshold);
}

void reset_heap_stats(void)
{
	/* if we're leaking nodes, we'll still see it */
	assert(the_heap_stats.node_allocs >= the_heap_stats.node_frees);
	the_heap_stats.node_allocs -= the_heap_stats.node_frees;
	the_heap_stats.node_frees = 0;
}
