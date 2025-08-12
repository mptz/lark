/*
 * Copyright (c) 2009-2025 Michael P. Touloumtzis.
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

/*
 * The heap pressure is the fraction of the heap which is in use, with a
 * a clamped upper limit at 95%.  When pressure is above the threshold,
 * we may attempt collection; after collecting, we revise the threshold
 * up or down:
 *
 *                    pressure <  0.333 threshold? => threshold *= 0.666
 * 0.333 threshold <= pressure <= 0.666 threshold? => no adjustment
 * 0.666 threshold <  pressure <= 1.000 threshold? => raise threshold
 *                                                (halve distance to 1.0)
 * 1.000 threshold <  pressure?                    => raise threshold
 *                                                    (above pressure)
 *
 * The 95% maximum pressure prevents collection attempts when we are
 * down to < 5% free heap capacity, to prevent futile hyperactive
 * collection attempts when the heap is nearly full.  At this point we
 * can only recover via passive garbage collection during reduction, or
 * more likely fail due to heap exhaustion.
 *
 * The 60% minimum threshold prevents collection attempts when urgency
 * to reclaim memory is low.
 *
 * Note that these capacity calculations are made based on the number
 * of nodes, not their sizes--that would be a potential improvement.
 */
static const float MAX_HEAP_PRESSURE = 0.95;
static const float MIN_HEAP_THRESHOLD = 0.6;

/*
 * These are not static; we reference them directly in a performance-
 * critical part of the reduction loop to avoid function call overhead.
 */
float the_heap_pressure, the_heap_threshold = MIN_HEAP_THRESHOLD;

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

/*
 * After we install a node to the global environment, we no longer
 * consider it part of the heap--the heap is only for active reductions
 * not constants.  Reset the heap to its initial state.
 */
void node_heap_baseline(void)
{
	memset(&the_heap_stats, 0, sizeof the_heap_stats);
	the_heap_threshold = MIN_HEAP_THRESHOLD;
	update_heap_pressure();
}

void node_heap_calibrate(void)
{
	update_heap_pressure();
	assert(the_heap_pressure >= 0.0);
	assert(the_heap_pressure <  1.0);
	assert(the_heap_threshold >= MIN_HEAP_THRESHOLD);
	assert(the_heap_threshold <  1.0);
	if (the_heap_pressure > the_heap_threshold)
		the_heap_threshold = the_heap_pressure +
					(1.0 - the_heap_pressure) / 2.0;
	else if (the_heap_pressure > the_heap_threshold * 0.666)
		the_heap_threshold +=	(1.0 - the_heap_threshold) / 2.0;
	else if (the_heap_pressure < the_heap_threshold * 0.333) {
		the_heap_threshold *=	0.666;
		if (the_heap_threshold < MIN_HEAP_THRESHOLD)
			the_heap_threshold = MIN_HEAP_THRESHOLD;
	}
	assert(the_heap_pressure < the_heap_threshold);
}

struct node *node_heap_alloc(size_t nslots)
{
	if (the_heap_stats.nodes_in_use >= MAX_NODES)
		panic("Node heap exhausted!\n");
	the_heap_stats.node_allocs++;
	the_heap_stats.nodes_in_use++;
	update_heap_pressure();
	struct node *node = xmalloc(sizeof *node +
				    nslots * sizeof node->slots[0]);
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
