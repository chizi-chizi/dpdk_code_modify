#include <stdio.h>
#include <stdlib.h>

#include "malloc_heap.h"
#include "malloc_elem.h"
#include "common.h"

#define MALLOC_HEAP_DEBUG 0

struct malloc_heap *
malloc_heap_create(size_t len)
{
	void *start =NULL;
	struct malloc_heap *heap = NULL;


	heap = malloc(sizeof(struct malloc_heap));
	if(heap == NULL){
		return NULL;
	}
	heap->alloc_count = 0;
	heap->total_size = len;
	start = malloc(len);
	if(start == NULL){
		free(heap);
		return NULL;
	}
	struct malloc_elem *elem = start;

	malloc_elem_init(elem, heap, len);

	malloc_elem_insert(elem);

	malloc_elem_free_list_insert(elem);

	return heap;
}


/*
 * Iterates through the freelist for a heap to find a free element
 * which can store data of the required size and with the requested alignment.
 * If size is 0, find the biggest available elem.
 * Returns null on failure, or pointer to element on success.
 */
static struct malloc_elem *
find_suitable_element(struct malloc_heap *heap, size_t size, size_t align)
{
	size_t idx;
	struct malloc_elem *elem, *alt_elem = NULL;

	for (idx = malloc_elem_free_list_index(size);
			idx < RTE_HEAP_NUM_FREELISTS; idx++) {
		for (elem = LIST_FIRST(&heap->free_head[idx]);
				!!elem; elem = LIST_NEXT(elem, free_list)) {
			if (malloc_elem_can_hold(elem, size, align)) {
				if (alt_elem == NULL)
					alt_elem = elem;
			}
		}
	}

	return alt_elem;
}

/*
 * Main function to allocate a block of memory from the heap.
 * It locks the free list, scans it, and adds a new memseg if the
 * scan fails. Once the new memseg is added, it re-scans and should return
 * the new element after releasing the lock.
 */
void * heap_alloc(struct malloc_heap *heap, size_t size, size_t align)
{
	struct malloc_elem *elem;
	size_t size2, align2;

	size2 = RTE_CACHE_LINE_ROUNDUP(size);
	#if MALLOC_HEAP_DEBUG
	if(size != size2){
		printf("%s:%d, size: %ld aligned to %ld\n", __func__, __LINE__, size, size2);
	}
	#endif
	align2 = RTE_CACHE_LINE_ROUNDUP(align);
	#if MALLOC_HEAP_DEBUG
	if(align != align2){
		printf("%s:%d, align: %ld aligned to %ld\n", __func__, __LINE__, size, size2);
	}
	#endif

	/* roundup might cause an overflow */
	if (size == 0)
		return NULL;
	elem = find_suitable_element(heap, size2, align2);
	if (elem != NULL) {
		elem = malloc_elem_alloc(elem, size2, align2);

		/* increase heap's count of allocated elements */
		heap->alloc_count++;
	}
	// 返回的是数据部分， 也就是去掉header
	return elem == NULL ? NULL : (void *)(&elem[1]);
}

void heap_free(void *ptr){
	struct malloc_elem *elem = NULL;
	//elem = RTE_PTR_SUB(ptr, MALLOC_ELEM_HEADER_LEN);
	elem = malloc_elem_from_data(ptr);
	if(elem != NULL){
		malloc_elem_free(elem);
	}

	return;
}


/*
 * Function to retrieve data for a given heap
 */
void
malloc_heap_dump(struct malloc_heap *heap)
{
	struct malloc_elem *elem;
	printf("sizeof(struct malloc_elem):%ld\n", sizeof(struct malloc_elem));

	printf("Heap size: 0x%zx\n", heap->total_size);
	printf("Heap alloc count: %u\n", heap->alloc_count);

	elem = heap->first;
	while (elem) {
		malloc_elem_dump(elem);
		elem = elem->next;
	}
}

