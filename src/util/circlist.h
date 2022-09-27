#ifndef LARK_UTIL_CIRCLIST_H
#define LARK_UTIL_CIRCLIST_H
/*
 * Copyright (c) 2009-2021 Michael P. Touloumtzis.
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
#include <stddef.h>
#include <stdlib.h>

#define container_of(type, member, ptr) ({				\
	const typeof(((type *) 0)->member) *__pmember = (ptr);		\
	__pmember ?							\
	(type *) ((char *) __pmember - offsetof(type, member)) :	\
	((type *) 0); })

/*
 * Specialized container_of for circlists called 'entry'.
 */
#define node_of(type, ptr) ({						\
	const typeof(((type *) 0)->entry) *__pentry = (ptr);		\
	__pentry ?							\
	(type *) ((char *) __pentry - offsetof(type, entry)) :		\
	((type *) 0); })

struct circlist {
	struct circlist *prev, *next;
};

static inline void
circlist_init(struct circlist *sentinel)
{
	sentinel->prev = sentinel->next = sentinel;
}

static inline void
circlist_add_head(struct circlist *sentinel, struct circlist *entry)
{
	entry->prev = sentinel;
	entry->next = sentinel->next;
	sentinel->next->prev = entry;
	sentinel->next = entry;
}

static inline void
circlist_add_tail(struct circlist *sentinel, struct circlist *entry)
{
	entry->prev = sentinel->prev;
	entry->next = sentinel;
	sentinel->prev->next = entry;
	sentinel->prev = entry;
}

static inline int
circlist_is_empty(const struct circlist *sentinel)
{
	assert((sentinel->prev == sentinel && sentinel->next == sentinel) ||
	       (sentinel->prev != sentinel && sentinel->next != sentinel));
	return sentinel->prev == sentinel;
}

static inline int
circlist_is_inhabited(const struct circlist *sentinel)
{
	assert((sentinel->prev == sentinel && sentinel->next == sentinel) ||
	       (sentinel->prev != sentinel && sentinel->next != sentinel));
	return sentinel->prev != sentinel;
}

static inline int
circlist_is_head(const struct circlist *sentinel, const struct circlist *entry)
{
	return sentinel->next == entry;
}

static inline int
circlist_is_tail(const struct circlist *sentinel, const struct circlist *entry)
{
	return sentinel->prev == entry;
}

static inline struct circlist *
circlist_get_head(const struct circlist *sentinel)
{
	return circlist_is_empty(sentinel) ? NULL : sentinel->next;
}

static inline struct circlist *
circlist_get_tail(const struct circlist *sentinel)
{
	return circlist_is_empty(sentinel) ? NULL : sentinel->prev;
}

static inline unsigned long
circlist_length(const struct circlist *sentinel)
{
	unsigned long length = 0;
	const struct circlist *entry = sentinel->next;
	for (/* nada */; entry != sentinel; ++length, entry = entry->next);
	return length;
}

static inline void
circlist_put_ring(struct circlist *sentinel, struct circlist *ring)
{
	assert(circlist_is_empty(sentinel));
	/* use entry as sentinel, sentinel as entry */
	if (ring) circlist_add_tail(ring, sentinel);
}

static inline void
circlist_remove(struct circlist *entry)
{
	entry->prev->next = entry->next;
	entry->next->prev = entry->prev;
	entry->prev = entry->next = entry;
}

static inline void
circlist_remove_all(struct circlist *sentinel)
{
	circlist_init(sentinel);
}

static inline struct circlist *
circlist_remove_head(struct circlist *sentinel)
{
	struct circlist *entry;
	if ((entry = circlist_get_head(sentinel)) != NULL)
		circlist_remove(entry);
	return entry;
}

/* detach from sentinel and return as a circular structure */
static inline struct circlist *
circlist_remove_ring(struct circlist *sentinel)
{
	struct circlist *entry = sentinel->next;
	circlist_remove(sentinel);
	return entry == sentinel ? NULL : entry;
}

static inline struct circlist *
circlist_remove_tail(struct circlist *sentinel)
{
	struct circlist *entry;
	if ((entry = circlist_get_tail(sentinel)) != NULL)
		circlist_remove(entry);
	return entry;
}

static inline void
circlist_splice_tail(struct circlist *sentinel, struct circlist *ring)
{
	sentinel->prev->next = ring;
	ring->prev->next = sentinel;
	struct circlist *tmp = sentinel->prev;
	sentinel->prev = ring->prev;
	ring->prev = tmp;
}

struct circlist_iter {
	const struct circlist *current, *sentinel;
};

static inline void
circlist_iter_init(const struct circlist *sentinel,
		   struct circlist_iter *iter)
{
	iter->sentinel = sentinel;
	iter->current = sentinel->next;
}

static inline void
circlist_iter_rev_init(const struct circlist *sentinel,
		       struct circlist_iter *iter)
{
	iter->sentinel = sentinel;
	iter->current = sentinel->prev;
}

static inline struct circlist *
circlist_iter_next(struct circlist_iter *iter)
{
	if (iter->current == iter->sentinel)
		return NULL;
	const struct circlist *tmp = iter->current;
	iter->current = iter->current->next;
	return (struct circlist *) tmp;
}

static inline const struct circlist *
circlist_const_iter_next(struct circlist_iter *iter)
{
	if (iter->current == iter->sentinel)
		return NULL;
	const struct circlist *tmp = iter->current;
	iter->current = iter->current->next;
	return tmp;
}

static inline struct circlist *
circlist_iter_prev(struct circlist_iter *iter)
{
	if (iter->current == iter->sentinel)
		return NULL;
	const struct circlist *tmp = iter->current;
	iter->current = iter->current->prev;
	return (struct circlist *) tmp;
}

static inline const struct circlist *
circlist_const_iter_prev(struct circlist_iter *iter)
{
	if (iter->current == iter->sentinel)
		return NULL;
	const struct circlist *tmp = iter->current;
	iter->current = iter->current->prev;
	return tmp;
}

#endif /* LARK_UTIL_CIRCLIST_H */
