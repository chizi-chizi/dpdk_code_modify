#include <inttypes.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "malloc_elem.h"
#include "malloc_heap.h"
#include "common.h"

#define MALLOC_ELEM_DEBUG 0

void malloc_elem_init(struct malloc_elem *elem, struct malloc_heap *heap, size_t size)
{
	elem->heap = heap;
	elem->prev = NULL;
	elem->next = NULL;
	memset(&elem->free_list, 0, sizeof(elem->free_list));
	elem->state = ELEM_FREE;
	elem->size = size;
}

void malloc_elem_insert(struct malloc_elem *elem)
{
	struct malloc_elem *prev_elem, *next_elem;
	struct malloc_heap *heap = elem->heap;

	/* first and last elements must be both NULL or both non-NULL */
	if ((heap->first == NULL) != (heap->last == NULL)) {
		printf ("Heap is probably corrupt\n");
		return;
	}

	if (heap->first == NULL && heap->last == NULL) {
		/* if empty heap */
		heap->first = elem;
		heap->last = elem;
		prev_elem = NULL;
		next_elem = NULL;
	} else if (elem < heap->first) {
		/* if lower than start */
		prev_elem = NULL;
		next_elem = heap->first;
		heap->first = elem;
	} else if (elem > heap->last) {
		/* if higher than end */
		prev_elem = heap->last;
		next_elem = NULL;
		heap->last = elem;
	} else {
		/* the new memory is somewhere between start and end */
		uint64_t dist_from_start, dist_from_end;

		dist_from_end = RTE_PTR_DIFF(heap->last, elem);
		dist_from_start = RTE_PTR_DIFF(elem, heap->first);

		/* check which is closer, and find closest list entries */
		if (dist_from_start < dist_from_end) {
			prev_elem = heap->first;
			while (prev_elem->next < elem)
				prev_elem = prev_elem->next;
			next_elem = prev_elem->next;
		} else {
			next_elem = heap->last;
			while (next_elem->prev > elem)
				next_elem = next_elem->prev;
			prev_elem = next_elem->prev;
		}
	}

	/* insert new element */
	elem->prev = prev_elem;
	elem->next = next_elem;
	if (prev_elem)
		prev_elem->next = elem;
	if (next_elem)
		next_elem->prev = elem;
}

/*
 * calculate the starting point of where data of the requested size
 * and alignment would fit in the current element. If the data doesn't
 * fit, return NULL.
 */
static void * elem_start_pt(struct malloc_elem *elem, size_t size, unsigned align)
{
	size_t elem_size = elem->size;

	/*
	 * we're allocating from the end, so adjust the size of element by
	 * alignment size.
	 */
	while (elem_size >= size) {
		uintptr_t end_pt = (uintptr_t)elem + elem_size;
		uintptr_t new_data_start = RTE_ALIGN_FLOOR((end_pt - size), align);
		#if MALLOC_ELEM_DEBUG
		if(new_data_start != (end_pt - size)){
			printf("%s:%d, new_data_start:%ld, aligned to %ld\n", __func__, __LINE__, end_pt-size, new_data_start);
		}
		#endif
		uintptr_t new_elem_start;


		new_elem_start = new_data_start - MALLOC_ELEM_HEADER_LEN;

		/* if the new start point is before the exist start,
		 * it won't fit
		 */
		if (new_elem_start < (uintptr_t)elem)
			return NULL;
		return (void *)new_elem_start;
	}
	return NULL;
}


/*
 * use elem_start_pt to determine if we get meet the size and
 * alignment request from the current element
 */
int malloc_elem_can_hold(struct malloc_elem *elem, size_t size, unsigned align)
{
	return elem_start_pt(elem, size, align) != NULL;
}


/*
 * split an existing element into two smaller elements at the given
 * split_pt parameter.
 */
static void
split_elem(struct malloc_elem *elem, struct malloc_elem *split_pt)
{
	struct malloc_elem *next_elem = elem->next;
	const size_t old_elem_size = (uintptr_t)split_pt - (uintptr_t)elem;
	const size_t new_elem_size = elem->size - old_elem_size;
#if MALLOC_ELEM_DEBUG
	printf("new_elem_size:%ld\n", new_elem_size);
#endif
	malloc_elem_init(split_pt, elem->heap,new_elem_size);
	split_pt->prev = elem;
	split_pt->next = next_elem;
	if (next_elem)
		next_elem->prev = split_pt;
	else
		elem->heap->last = split_pt;
	elem->next = split_pt;
	elem->size = old_elem_size;
}


/*
 * our malloc heap is a doubly linked list, so doubly remove our element.
 */
static void remove_elem(struct malloc_elem *elem)
{
	struct malloc_elem *next, *prev;
	next = elem->next;
	prev = elem->prev;

	if (next)
		next->prev = prev;
	else
		elem->heap->last = prev;
	if (prev)
		prev->next = next;
	else
		elem->heap->first = next;

	elem->prev = NULL;
	elem->next = NULL;
}


static int
next_elem_is_adjacent(struct malloc_elem *elem)
{
	return elem->next == RTE_PTR_ADD(elem, elem->size);
}

static int
prev_elem_is_adjacent(struct malloc_elem *elem)
{
	return elem == RTE_PTR_ADD(elem->prev, elem->prev->size);
}


/*
 * Given an element size, compute its freelist index.
 * We free an element into the freelist containing similarly-sized elements.
 * We try to allocate elements starting with the freelist containing
 * similarly-sized elements, and if necessary, we search freelists
 * containing larger elements.
 *
 * Example element size ranges for a heap with five free lists:
 *   heap->free_head[0] - (0   , 2^8]
 *   heap->free_head[1] - (2^8 , 2^10]
 *   heap->free_head[2] - (2^10 ,2^12]
 *   heap->free_head[3] - (2^12, 2^14]
 *   heap->free_head[4] - (2^14, MAX_SIZE]
 */
size_t malloc_elem_free_list_index(size_t size)
{
#define MALLOC_MINSIZE_LOG2   8
#define MALLOC_LOG2_INCREMENT 2

	size_t log2;
	size_t index;

	if (size <= (1UL << MALLOC_MINSIZE_LOG2))
		return 0;

	/* Find next power of 2 >= size. */
	log2 = sizeof(size) * 8 - __builtin_clzl(size - 1);

	/* Compute freelist index, based on log2(size). */
	index = (log2 - MALLOC_MINSIZE_LOG2 + MALLOC_LOG2_INCREMENT - 1) /
			MALLOC_LOG2_INCREMENT;

	return index <= RTE_HEAP_NUM_FREELISTS - 1 ?
			index : RTE_HEAP_NUM_FREELISTS - 1;
}

/*
 * Add the specified element to its heap's free list.
 */
void
malloc_elem_free_list_insert(struct malloc_elem *elem)
{
	size_t idx;

	idx = malloc_elem_free_list_index(elem->size);
	elem->state = ELEM_FREE;
	LIST_INSERT_HEAD(&elem->heap->free_head[idx], elem, free_list);
}


/*
 * Remove the specified element from its heap's free list.
 */
void malloc_elem_free_list_remove(struct malloc_elem *elem)
{
	LIST_REMOVE(elem, free_list);
}

/*
 * reserve a block of data in an existing malloc_elem. If the malloc_elem
 * is much larger than the data block requested, we split the element in two.
 * This function is only called from malloc_heap_alloc so parameter checking
 * is not done here, as it's done there previously.
 */
struct malloc_elem *
malloc_elem_alloc(struct malloc_elem *elem, size_t size, unsigned align)
{
	struct malloc_elem *new_elem = elem_start_pt(elem, size, align);
	const size_t old_elem_size = (uintptr_t)new_elem - (uintptr_t)elem;

	malloc_elem_free_list_remove(elem);

	split_elem(elem, new_elem);
	new_elem->state = ELEM_BUSY;
	malloc_elem_free_list_insert(elem);

	return new_elem;
}


/*
 * join two struct malloc_elem together. elem1 and elem2 must
 * be contiguous in memory.
 */
static inline void
join_elem(struct malloc_elem *elem1, struct malloc_elem *elem2)
{
	struct malloc_elem *next = elem2->next;
	elem1->size += elem2->size;
	if (next)
		next->prev = elem1;
	else
		elem1->heap->last = elem1;
	elem1->next = next;
}


struct malloc_elem *
malloc_elem_join_adjacent_free(struct malloc_elem *elem)
{
	/*
	 * check if next element exists, is adjacent and is free, if so join
	 * with it, need to remove from free list.
	 */
	if (elem->next != NULL && elem->next->state == ELEM_FREE &&
			next_elem_is_adjacent(elem)) {

		/* remove from free list, join to this one */
		malloc_elem_free_list_remove(elem->next);
		join_elem(elem, elem->next);
	}

	/*
	 * check if prev element exists, is adjacent and is free, if so join
	 * with it, need to remove from free list.
	 */
	if (elem->prev != NULL && elem->prev->state == ELEM_FREE &&
			prev_elem_is_adjacent(elem)) {
		struct malloc_elem *new_elem;

		/* remove from free list, join to this one */
		malloc_elem_free_list_remove(elem->prev);

		new_elem = elem->prev;
		join_elem(new_elem, elem);

		elem = new_elem;
	}

	return elem;
}


/*
 * free a malloc_elem block by adding it to the free list. If the
 * blocks either immediately before or immediately after newly freed block
 * are also free, the blocks are merged together.
 */
struct malloc_elem * malloc_elem_free(struct malloc_elem *elem)
{
	void *ptr;
	size_t data_len;

	ptr = RTE_PTR_ADD(elem, MALLOC_ELEM_HEADER_LEN);
	data_len = elem->size - MALLOC_ELEM_HEADER_LEN;

	elem = malloc_elem_join_adjacent_free(elem);

	malloc_elem_free_list_insert(elem);

	/* decrease heap's count of allocated elements */
	elem->heap->alloc_count--;

	memset(ptr, 0, data_len);

	return elem;
}

static inline const char *
elem_state_to_str(enum elem_state state)
{
	switch (state) {
	case ELEM_BUSY:
		return "BUSY";
	case ELEM_FREE:
		return "FREE";
	}
	return "ERROR";
}



void malloc_elem_dump(const struct malloc_elem *elem)
{
	printf("Malloc element at %p (%s)\n", elem,
			elem_state_to_str(elem->state));
	//printf("  len: 0x%zx" PRIx32 "\n", elem->size);
	printf("  len: %ld\n", elem->size);
	printf("  prev: %p next: %p\n", elem->prev, elem->next);
}

