#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <stdlib.h>

#include "ring.h"

/* return the size of memory occupied by a ring */
ssize_t
rte_ring_get_memsize_elem(unsigned int esize, unsigned int count)
{
	ssize_t sz;

	/* Check if element size is a multiple of 4B */
	if (esize % 4 != 0) {
		printf("element size is not a multiple of 4\n");

		return -EINVAL;
	}

	/* count must be a power of 2 */
	if ((!POWEROF2(count)) || (count > RTE_RING_SZ_MASK )) {
		printf("Requested number of elements is invalid, must be power of 2, and not exceed %u\n",
			RTE_RING_SZ_MASK);

		return -EINVAL;
	}

	sz = sizeof(struct rte_ring) + (ssize_t)count * esize;
	sz = RTE_ALIGN(sz, RTE_CACHE_LINE_SIZE);
	return sz;
}


static int
get_sync_type(uint32_t flags, enum rte_ring_sync_type *prod_st,
	enum rte_ring_sync_type *cons_st)
{
	static const uint32_t prod_st_flags =RING_F_SP_ENQ;
	static const uint32_t cons_st_flags =RING_F_SC_DEQ;

	switch (flags & prod_st_flags) {
	case 0:
		*prod_st = RTE_RING_SYNC_MT;
		break;
	case RING_F_SP_ENQ:
		*prod_st = RTE_RING_SYNC_ST;
		break;

	default:
		return -EINVAL;
	}

	switch (flags & cons_st_flags) {
	case 0:
		*cons_st = RTE_RING_SYNC_MT;
		break;
	case RING_F_SC_DEQ:
		*cons_st = RTE_RING_SYNC_ST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}


int
rte_ring_init(struct rte_ring *r, unsigned int count, unsigned int flags)
{
	int ret;

	/* future proof flags, only allow supported values */
	if (flags & ~RING_F_MASK) {
		printf("Unsupported flags requested %#x\n", flags);
		return -EINVAL;
	}

	/* init the ring structure */
	memset(r, 0, sizeof(*r));
	r->flags = flags;
	ret = get_sync_type(flags, &r->prod.sync_type, &r->cons.sync_type);
	if (ret != 0)
		return ret;

	if (flags & RING_F_EXACT_SZ) {
		r->size = rte_align32pow2(count + 1);
		r->mask = r->size - 1;
		r->capacity = count;
	} else {
		if ((!POWEROF2(count)) || (count > RTE_RING_SZ_MASK)) {
			printf("Requested size is invalid, must be power of 2, and not exceed the size limit %u\n",
				RTE_RING_SZ_MASK);
			return -EINVAL;
		}
		r->size = count;
		r->mask = count - 1;
		r->capacity = r->mask;
	}

	return 0;
}

/* create the ring for a given element size */
struct rte_ring *
rte_ring_create_elem(unsigned int esize, unsigned int count, unsigned int flags)
{
	struct rte_ring *r;
	void*  mem_ptr = NULL;
	ssize_t ring_size;
	const unsigned int requested_count = count;
	int ret;


	/* for an exact size ring, round up from count to a power of two */
	if (flags & RING_F_EXACT_SZ)
		count = rte_align32pow2(count + 1);

	ring_size = rte_ring_get_memsize_elem(esize, count);
	if (ring_size < 0) {
		return NULL;
	}

	mem_ptr = malloc(ring_size);
	if (mem_ptr != NULL) {
		r = mem_ptr;
		/* no need to check return value here, we already checked the
		 * arguments above */
		rte_ring_init(r, requested_count, flags);

	} else {
		r = NULL;
		printf("Cannot reserve memory\n");
	}

	return r;
}


/* create the ring */
struct rte_ring *
rte_ring_create(unsigned int count, unsigned int flags)
{
	return rte_ring_create_elem(sizeof(void *), count, flags);
}



static  void update_tail(struct rte_ring_headtail *ht, uint32_t old_val, uint32_t new_val,
		uint32_t single, uint32_t enqueue)
{
	if (!single)
		while (unlikely(ht->tail != old_val))
			rte_pause();

	ht->tail = new_val;
}


static void __rte_ring_enqueue_elems_64(struct rte_ring *r, uint32_t prod_head,
		const void *obj_table, uint32_t n)
{
	unsigned int i;
	const uint32_t size = r->size;
	uint32_t idx = prod_head & r->mask;
	uint64_t *ring = (uint64_t *)&r[1];
	const uint64_t *obj = (const uint64_t *)obj_table;
	if (likely(idx + n <= size)) {
		for (i = 0; i < (n & ~0x3); i += 4, idx += 4) {
			ring[idx] = obj[i];
			ring[idx + 1] = obj[i + 1];
			ring[idx + 2] = obj[i + 2];
			ring[idx + 3] = obj[i + 3];
		}
		switch (n & 0x3) {
		case 3:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 2:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 1:
			ring[idx++] = obj[i++];
		}
	} else {
		for (i = 0; idx < size; i++, idx++)
			ring[idx] = obj[i];
		/* Start at the beginning */
		for (idx = 0; i < n; i++, idx++)
			ring[idx] = obj[i];
	}
}


static  void __rte_ring_enqueue_elems_32(struct rte_ring *r, const uint32_t size,
		uint32_t idx, const void *obj_table, uint32_t n)
{
	unsigned int i;
	uint32_t *ring = (uint32_t *)&r[1];
	const uint32_t *obj = (const uint32_t *)obj_table;
	if (likely(idx + n <= size)) {
		for (i = 0; i < (n & ~0x7); i += 8, idx += 8) {
			ring[idx] = obj[i];
			ring[idx + 1] = obj[i + 1];
			ring[idx + 2] = obj[i + 2];
			ring[idx + 3] = obj[i + 3];
			ring[idx + 4] = obj[i + 4];
			ring[idx + 5] = obj[i + 5];
			ring[idx + 6] = obj[i + 6];
			ring[idx + 7] = obj[i + 7];
		}
		switch (n & 0x7) {
		case 7:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 6:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 5:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 4:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 3:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 2:
			ring[idx++] = obj[i++]; /* fallthrough */
		case 1:
			ring[idx++] = obj[i++]; /* fallthrough */
		}
	} else {
		for (i = 0; idx < size; i++, idx++)
			ring[idx] = obj[i];
		/* Start at the beginning */
		for (idx = 0; i < n; i++, idx++)
			ring[idx] = obj[i];
	}
}


/* the actual enqueue of elements on the ring.
 * Placed here since identical code needed in both
 * single and multi producer enqueue functions.
 */
static  void
__rte_ring_enqueue_elems(struct rte_ring *r, uint32_t prod_head,
		const void *obj_table, uint32_t esize, uint32_t num)
{
	/* 8B and 16B copies implemented individually to retain
	 * the current performance.
	 */
	if (esize == 8)
		__rte_ring_enqueue_elems_64(r, prod_head, obj_table, num);
	else {
		uint32_t idx, scale, nr_idx, nr_num, nr_size;

		/* Normalize to uint32_t */
		scale = esize / sizeof(uint32_t);
		nr_num = num * scale;
		idx = prod_head & r->mask;
		nr_idx = idx * scale;
		nr_size = r->size * scale;
		__rte_ring_enqueue_elems_32(r, nr_size, nr_idx,
				obj_table, nr_num);
	}
}


static  unsigned int
__rte_ring_move_prod_head(struct rte_ring *r, unsigned int is_sp,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uint32_t *old_head, uint32_t *new_head,
		uint32_t *free_entries)
{
	const uint32_t capacity = r->capacity;
	unsigned int max = n;
	int success;

	do {
		/* Reset n to the initial burst count */
		n = max;

		*old_head = r->prod.head;

		/*
		 *  The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * *old_head > cons_tail). So 'free_entries' is always between 0
		 * and capacity (which is < size).
		 */
		*free_entries = (capacity + r->cons.tail - *old_head);
		printf("r->cons.tail:%d,  r->prod.head:%d\n",  r->cons.tail, r->prod.head);
		/* check that we have enough room in ring */
		if (unlikely(n > *free_entries))
			n = (behavior == RTE_RING_QUEUE_FIXED) ?
					0 : *free_entries;

		if (n == 0)
			return 0;

		*new_head = *old_head + n;
		if (is_sp)
			r->prod.head = *new_head, success = 1;
		else
			success = rte_atomic32_cmpset(&r->prod.head,
					*old_head, *new_head);
	} while (unlikely(success == 0));
	return n;
}


static  unsigned int
__rte_ring_do_enqueue_elem(struct rte_ring *r, const void *obj_table,
		unsigned int esize, unsigned int n,
		enum rte_ring_queue_behavior behavior, unsigned int is_sp,
		unsigned int *free_space)
{
	uint32_t prod_head, prod_next;
	uint32_t free_entries;

	n = __rte_ring_move_prod_head(r, is_sp, n, behavior,
			&prod_head, &prod_next, &free_entries);
	if (n == 0)
		goto end;

	__rte_ring_enqueue_elems(r, prod_head, obj_table, esize, n);

	update_tail(&r->prod, prod_head, prod_next, is_sp, 1);
end:
	if (free_space != NULL)
		*free_space = free_entries - n;
	return n;
}



static  unsigned int
rte_ring_mp_enqueue_bulk_elem(struct rte_ring *r, const void *obj_table,
		unsigned int esize, unsigned int n, unsigned int *free_space)
{
	return __rte_ring_do_enqueue_elem(r, obj_table, esize, n,
			RTE_RING_QUEUE_FIXED, RTE_RING_SYNC_MT, free_space);
}


static  unsigned int
rte_ring_sp_enqueue_bulk_elem(struct rte_ring *r, const void *obj_table,
		unsigned int esize, unsigned int n, unsigned int *free_space)
{
	return __rte_ring_do_enqueue_elem(r, obj_table, esize, n,
			RTE_RING_QUEUE_FIXED, RTE_RING_SYNC_ST, free_space);
}


static  unsigned int
rte_ring_enqueue_bulk_elem(struct rte_ring *r, const void *obj_table,
		unsigned int esize, unsigned int n, unsigned int *free_space)
{
	switch (r->prod.sync_type) {
	case RTE_RING_SYNC_MT:
		return rte_ring_mp_enqueue_bulk_elem(r, obj_table, esize, n,
			free_space);
	case RTE_RING_SYNC_ST:
		return rte_ring_sp_enqueue_bulk_elem(r, obj_table, esize, n,
			free_space);
	}

	return 0;
}


static  int
rte_ring_enqueue_elem(struct rte_ring *r, void *obj, unsigned int esize)
{
	return rte_ring_enqueue_bulk_elem(r, obj, esize, 1, NULL) ? 0 :
								-ENOBUFS;
}

 int rte_ring_enqueue(struct rte_ring *r, void *obj)
{
	return rte_ring_enqueue_elem(r, &obj, sizeof(void *));
}


static  void
__rte_ring_dequeue_elems_64(struct rte_ring *r, uint32_t prod_head,
		void *obj_table, uint32_t n)
{
	unsigned int i;
	const uint32_t size = r->size;
	uint32_t idx = prod_head & r->mask;
	uint64_t *ring = (uint64_t *)&r[1];
	uint64_t *obj = (uint64_t *)obj_table;
	if (likely(idx + n <= size)) {
		for (i = 0; i < (n & ~0x3); i += 4, idx += 4) {
			obj[i] = ring[idx];
			obj[i + 1] = ring[idx + 1];
			obj[i + 2] = ring[idx + 2];
			obj[i + 3] = ring[idx + 3];
		}
		switch (n & 0x3) {
		case 3:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 2:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 1:
			obj[i++] = ring[idx++]; /* fallthrough */
		}
	} else {
		for (i = 0; idx < size; i++, idx++)
			obj[i] = ring[idx];
		/* Start at the beginning */
		for (idx = 0; i < n; i++, idx++)
			obj[i] = ring[idx];
	}
}


static  void
__rte_ring_dequeue_elems_32(struct rte_ring *r, const uint32_t size,
		uint32_t idx, void *obj_table, uint32_t n)
{
	unsigned int i;
	uint32_t *ring = (uint32_t *)&r[1];
	uint32_t *obj = (uint32_t *)obj_table;
	if (likely(idx + n <= size)) {
		for (i = 0; i < (n & ~0x7); i += 8, idx += 8) {
			obj[i] = ring[idx];
			obj[i + 1] = ring[idx + 1];
			obj[i + 2] = ring[idx + 2];
			obj[i + 3] = ring[idx + 3];
			obj[i + 4] = ring[idx + 4];
			obj[i + 5] = ring[idx + 5];
			obj[i + 6] = ring[idx + 6];
			obj[i + 7] = ring[idx + 7];
		}
		switch (n & 0x7) {
		case 7:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 6:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 5:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 4:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 3:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 2:
			obj[i++] = ring[idx++]; /* fallthrough */
		case 1:
			obj[i++] = ring[idx++]; /* fallthrough */
		}
	} else {
		for (i = 0; idx < size; i++, idx++)
			obj[i] = ring[idx];
		/* Start at the beginning */
		for (idx = 0; i < n; i++, idx++)
			obj[i] = ring[idx];
	}
}


/* the actual dequeue of elements from the ring.
 * Placed here since identical code needed in both
 * single and multi producer enqueue functions.
 */
static void
__rte_ring_dequeue_elems(struct rte_ring *r, uint32_t cons_head,
		void *obj_table, uint32_t esize, uint32_t num)
{
	/* 8B and 16B copies implemented individually to retain
	 * the current performance.
	 */
	if (esize == 8)
		__rte_ring_dequeue_elems_64(r, cons_head, obj_table, num);
	else {
		uint32_t idx, scale, nr_idx, nr_num, nr_size;

		/* Normalize to uint32_t */
		scale = esize / sizeof(uint32_t);
		nr_num = num * scale;
		idx = cons_head & r->mask;
		nr_idx = idx * scale;
		nr_size = r->size * scale;
		__rte_ring_dequeue_elems_32(r, nr_size, nr_idx,
				obj_table, nr_num);
	}
}

static  unsigned int
__rte_ring_move_cons_head(struct rte_ring *r, unsigned int is_sc,
		unsigned int n, enum rte_ring_queue_behavior behavior,
		uint32_t *old_head, uint32_t *new_head,
		uint32_t *entries)
{
	unsigned int max = n;
	int success;

	/* move cons.head atomically */
	do {
		/* Restore n as it may change every loop */
		n = max;

		*old_head = r->cons.head;

		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * cons_head > prod_tail). So 'entries' is always between 0
		 * and size(ring)-1.
		 */
		*entries = (r->prod.tail - *old_head);

		/* Set the actual entries for dequeue */
		if (n > *entries)
			n = (behavior == RTE_RING_QUEUE_FIXED) ? 0 : *entries;

		if (unlikely(n == 0))
			return 0;

		*new_head = *old_head + n;
		if (is_sc) {
			r->cons.head = *new_head;
			success = 1;
		} else {
			success = rte_atomic32_cmpset(&r->cons.head, *old_head,
					*new_head);
		}
	} while (unlikely(success == 0));
	return n;
}



static  unsigned int
__rte_ring_do_dequeue_elem(struct rte_ring *r, void *obj_table,
		unsigned int esize, unsigned int n,
		enum rte_ring_queue_behavior behavior, unsigned int is_sc,
		unsigned int *available)
{
	uint32_t cons_head, cons_next;
	uint32_t entries;

	n = __rte_ring_move_cons_head(r, (int)is_sc, n, behavior,
			&cons_head, &cons_next, &entries);
	if (n == 0)
		goto end;

	__rte_ring_dequeue_elems(r, cons_head, obj_table, esize, n);

	update_tail(&r->cons, cons_head, cons_next, is_sc, 0);

end:
	if (available != NULL)
		*available = entries - n;
	return n;
}


static  unsigned int
rte_ring_sc_dequeue_bulk_elem(struct rte_ring *r, void *obj_table,
		unsigned int esize, unsigned int n, unsigned int *available)
{
	return __rte_ring_do_dequeue_elem(r, obj_table, esize, n,
			RTE_RING_QUEUE_FIXED, RTE_RING_SYNC_ST, available);
}


static  unsigned int
rte_ring_mc_dequeue_bulk_elem(struct rte_ring *r, void *obj_table,
		unsigned int esize, unsigned int n, unsigned int *available)
{
	return __rte_ring_do_dequeue_elem(r, obj_table, esize, n,
			RTE_RING_QUEUE_FIXED, RTE_RING_SYNC_MT, available);
}

static  unsigned int rte_ring_dequeue_bulk_elem(
		struct rte_ring *r,
		void *obj_table,
		unsigned int esize,
		unsigned int n, 
		unsigned int *available)
{
	switch (r->cons.sync_type) {
	case RTE_RING_SYNC_MT:
		return rte_ring_mc_dequeue_bulk_elem(r, obj_table, esize, n,
			available);
	case RTE_RING_SYNC_ST:
		return rte_ring_sc_dequeue_bulk_elem(r, obj_table, esize, n,
			available);
	}
	return 0;
}



static  int rte_ring_dequeue_elem(struct rte_ring *r, void *obj_p, unsigned int esize)
{
	return rte_ring_dequeue_bulk_elem(r, obj_p, esize, 1, NULL) ? 0 :
								-ENOENT;
}

 int rte_ring_dequeue(struct rte_ring *r, void **obj_p)
{
	return rte_ring_dequeue_elem(r, obj_p, sizeof(void *));
}

/* free the ring */
void rte_ring_free(struct rte_ring *r)
{
	if (r == NULL){
		free(r);
		return;
	}
}

