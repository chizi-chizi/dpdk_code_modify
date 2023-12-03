#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "ring.h"

struct Person{
	char name[16];
	int age;
	int weight;
	int height;
};

void person_init(struct Person *p, char* name, int age, int weight, int height){
	strcpy(p->name, name);
	p->age = age;
	p->weight = weight;
	p->height = height;

	return;
}
int main(int argc, char** argv){
	struct rte_ring *ring = NULL;
	struct Person person[32];
	struct Person *p = NULL;
	char name[16]={0};
	int i;
	int ret;
	
	ring = rte_ring_create(16, 0);
	if(ring == NULL){
		printf("create ring failed\n");
		
		return -1;
	}
#if 0
	for(i=0; i< 16; i++){
		sprintf(name, "chizi%d", i);
		person_init(&person[i], name, 32, 150, 170);
		ret = rte_ring_enqueue(ring, &person[i]);
		if(ret == 0){
			printf("%d enqueue sucess!\n", i);
		}else{
			printf("%d enqueue failed:%s!\n", i, strerror(-ret));
		}
	}

	for(i=0; i<16; i++){
		ret = rte_ring_dequeue(ring, (void**)&p);
		if(ret == 0){
			printf("dequeue success,name:%s!\n", p->name);
		}else{
			printf("%d dequeue failed:%s!\n", i, strerror(-ret));
		}
	}
	
	for(i=0; i< 16; i++){
		sprintf(name, "chizi%d", i);
		person_init(&person[i], name, 32, 150, 170);
		ret = rte_ring_enqueue(ring, &person[i]);
		if(ret == 0){
			printf("%d enqueue sucess!\n", i);
		}else{
			printf("%d enqueue failed:%s!\n", i, strerror(-ret));
		}
	}

	for(i=0; i<16; i++){
		ret = rte_ring_dequeue(ring, (void**)&p);
		if(ret == 0){
			printf("dequeue success,name:%s!\n", p->name);
		}else{
			printf("%d dequeue failed:%s!\n", i, strerror(-ret));
		}
	}
#else
	for(i=0; i<256; i++){
		sprintf(name, "chizi%d", i);
		person_init(&person[i], name, 32, 150, 170);
		ret = rte_ring_enqueue(ring, &person[i]);
		if(ret == 0){
			printf("%d enqueue sucess!\n", i);
		}else{
			printf("%d enqueue failed:%s!\n", i, strerror(-ret));
		}
		ret = rte_ring_dequeue(ring, (void**)&p);
		if(ret == 0){
			printf("dequeue success,name:%s!\n", p->name);
		}else{
			printf("%d dequeue failed:%s!\n", i, strerror(-ret));
		}
	}
#endif
	rte_ring_free(ring);
	return 0;
}

