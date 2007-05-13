/*
 * Memory management functions.
 *
 * Copyright 2000-2007 Willy Tarreau <w@1wt.eu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <common/config.h>
#include <common/memory.h>
#include <common/mini-clist.h>
#include <common/standard.h>

#include <proto/log.h>

static struct list pools = LIST_HEAD_INIT(pools);

/* Try to find an existing shared pool with the same characteristics and
 * returns it, otherwise creates this one. NULL is returned if no memory
 * is available for a new creation.
 */
struct pool_head *create_pool(char *name, unsigned int size, unsigned int flags)
{
	struct pool_head *pool;
	struct pool_head *entry;
	struct list *start;
	unsigned int align;

	/* We need to store at least a (void *) in the chunks. Since we know
	 * that the malloc() function will never return such a small size,
	 * let's round the size up to something slightly bigger, in order to
	 * ease merging of entries. Note that the rounding is a power of two.
	 */

	align = 16;
	size  = (size + align - 1) & -align;

	start = &pools;
	pool = NULL;

	list_for_each_entry(entry, &pools, list) {
		if (entry->size == size) {
			/* either we can share this place and we take it, or
			 * we look for a sharable one or for the next position
			 * before which we will insert a new one.
			 */
			if (flags & entry->flags & MEM_F_SHARED) {
				/* we can share this one */
				pool = entry;
				break;
			}
		}
		else if (entry->size > size) {
			/* insert before this one */
			start = &entry->list;
			break;
		}
	}

	if (!pool) {
		pool = CALLOC(1, sizeof(*pool));
		if (!pool)
			return NULL;
		if (name)
			strlcpy2(pool->name, name, sizeof(pool->name));
		pool->size = size;
		pool->flags = flags;
		LIST_ADDQ(start, &pool->list);
	}
	pool->users++;
	return pool;
}

/* Allocate a new entry for pool <pool>, and return it for immediate use.
 * NULL is returned if no memory is available for a new creation. A call
 * to the garbage collector is performed before returning NULL.
 */
void *pool_refill_alloc(struct pool_head *pool)
{
	void *ret;

	if (pool->limit && (pool->allocated >= pool->limit))
		return NULL;
	ret = MALLOC(pool->size);
	if (!ret) {
		pool_gc2();
		ret = MALLOC(pool->size);
		if (!ret)
			return NULL;
	}
	pool->allocated++;
	pool->used++;
	return ret;
}

/*
 * This function frees whatever can be freed in pool <pool>.
 */
void pool_flush2(struct pool_head *pool)
{
	void *temp, *next;
	next = pool->free_list;
	while (next) {
		temp = next;
		next = *(void **)temp;
		pool->allocated--;
		FREE(temp);
	}
	pool->free_list = next;

	/* here, we should have pool->allocate == pool->used */
}

/*
 * This function frees whatever can be freed in all pools, but respecting
 * the minimum thresholds imposed by owners.
 */
void pool_gc2()
{
	struct pool_head *entry;
	list_for_each_entry(entry, &pools, list) {
		void *temp, *next;
		//qfprintf(stderr, "Flushing pool %s\n", entry->name);
		next = entry->free_list;
		while (next &&
		       entry->allocated > entry->minavail &&
		       entry->allocated > entry->used) {
			temp = next;
			next = *(void **)temp;
			entry->allocated--;
			FREE(temp);
		}
		entry->free_list = next;
	}
}

/*
 * This function destroys a pull by freeing it completely.
 * This should be called only under extreme circumstances.
 */
void pool_destroy2(struct pool_head *pool)
{
	pool_flush2(pool);
	FREE(pool);
}

/* Dump statistics on pools usage.
 */
void dump_pools(void)
{
	struct pool_head *entry;
	unsigned long allocated, used;
	int nbpools;

	allocated = used = nbpools = 0;
	qfprintf(stderr, "Dumping pools usage.\n");
	list_for_each_entry(entry, &pools, list) {
		qfprintf(stderr, "  - Pool %s (%d bytes) : %d allocated (%lu bytes), %d used, %d users%s\n",
			 entry->name, entry->size, entry->allocated,
			 entry->size * entry->allocated, entry->used,
			 entry->users, (entry->flags & MEM_F_SHARED) ? " [SHARED]" : "");

		allocated += entry->allocated * entry->size;
		used += entry->used * entry->size;
		nbpools++;
	}
	qfprintf(stderr, "Total: %d pools, %lu bytes allocated, %lu used.\n",
		 nbpools, allocated, used);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
