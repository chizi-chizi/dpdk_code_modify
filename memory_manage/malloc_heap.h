#ifndef MALLOC_HEAP_H_
#define MALLOC_HEAP_H_
#include <stddef.h>
#include <sys/queue.h>

/* Number of free lists per heap, grouped by size. */
#define RTE_HEAP_NUM_FREELISTS  13
#define RTE_HEAP_NAME_MAX_LEN 32

/**
 * Structure to hold malloc heap
 */
struct malloc_heap {
	LIST_HEAD(, malloc_elem) free_head[RTE_HEAP_NUM_FREELISTS];
	struct malloc_elem *volatile first;
	struct malloc_elem *volatile last;

	unsigned int alloc_count;
	unsigned int socket_id;
	size_t total_size;
	char name[RTE_HEAP_NAME_MAX_LEN];
};

struct malloc_heap * malloc_heap_create(size_t len);
void * heap_alloc(struct malloc_heap *heap, size_t size, size_t align);
void heap_free(void *ptr);
void malloc_heap_dump(struct malloc_heap *heap);

#endif
