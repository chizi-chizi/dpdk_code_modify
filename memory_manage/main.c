#include <stdio.h>
#include <stdlib.h>

#include "malloc_heap.h"
#include "malloc_elem.h"


int main(int argc, char** argv){

	struct malloc_heap *heap = NULL;
	int len = 1024 * 1024 * 1024;
	void* elem1 = NULL;
	void* elem2 = NULL;
	void* elem3 = NULL;
	void* elem4 = NULL;
	void* elem5 = NULL;

	heap = malloc_heap_create(len);
	if(!heap){
		printf("malloc heap create\n");
		return -1;
	}

	elem1 = heap_alloc(heap, 64, 64);
	if(elem1 == NULL){
		printf("heap alloc failed\n");
	}
	elem2 = heap_alloc(heap, 62, 64);
	if(elem2 == NULL){
		printf("heap alloc failed\n");
	}
	elem3 = heap_alloc(heap, 512 * 1024 * 1024, 64);
	if(elem3 == NULL){
		printf("heap alloc failed\n");
	}
	elem4 = heap_alloc(heap, 200 * 1024 * 1024, 64);
	if(elem4 == NULL){
		printf("heap alloc failed\n");
	}
	elem5 = heap_alloc(heap, 200 * 1024 * 1024, 64);
	if(elem5 == NULL){
		printf("heap alloc failed\n");
	}
	malloc_heap_dump(heap);
        heap_free(elem3);
        heap_free(elem4);
	malloc_heap_dump(heap);

	return 0;
}

