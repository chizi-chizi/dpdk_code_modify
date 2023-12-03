#ifndef MALLOC_ELEM_H_
#define MALLOC_ELEM_H_

#include <stddef.h>
#include <sys/queue.h>

#include "common.h"

enum elem_state {
	ELEM_FREE = 0,
	ELEM_BUSY,
};
	
struct malloc_elem {
	struct malloc_heap *heap;
	struct malloc_elem *volatile prev;
	/**< points to prev elem in memseg */
	struct malloc_elem *volatile next;
	/**< points to next elem in memseg */
	LIST_ENTRY(malloc_elem) free_list;
	/**< list of free elements in heap */
	volatile enum elem_state state;
	size_t size;
} __rte_cache_aligned;

static const unsigned MALLOC_ELEM_HEADER_LEN = sizeof(struct malloc_elem);


/*
 * Given a pointer to the start of a memory block returned by malloc, get
 * the actual malloc_elem header for that block.
 */
static inline struct malloc_elem *
malloc_elem_from_data(const void *data)
{
	if (data == NULL)
		return NULL;
	
	struct malloc_elem *elem = RTE_PTR_SUB(data, MALLOC_ELEM_HEADER_LEN);
	return  elem;
}

void malloc_elem_init(struct malloc_elem *elem, struct malloc_heap *heap, size_t size);
void malloc_elem_insert(struct malloc_elem *elem);
struct malloc_elem * malloc_elem_free(struct malloc_elem *elem);

struct malloc_elem *
malloc_elem_alloc(struct malloc_elem *elem, size_t size, unsigned align);

void malloc_elem_dump(const struct malloc_elem *elem);
void malloc_elem_free_list_insert(struct malloc_elem *elem);
size_t malloc_elem_free_list_index(size_t size);
void malloc_elem_free_list_remove(struct malloc_elem *elem);
int malloc_elem_can_hold(struct malloc_elem *elem, size_t size, unsigned align);


#endif

