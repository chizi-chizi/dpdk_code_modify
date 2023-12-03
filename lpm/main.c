#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "lpm.h"


#define THREADS_NUM 200

#define MAX_LPM_RULES (256 * THREADS_NUM)

// LPM表指针
struct rte_lpm* lpm_table;
int is_lock = 0;

pthread_mutex_t lpm_mutex = PTHREAD_MUTEX_INITIALIZER;

// 添加LPM表项的线程函数
void* add_lpm_entries(void* arg) {
	int i = *(int*)arg;
	int j = 0;
	int ret;
	struct in_addr dst_ip;
	uint32_t  nexthop = i;
	for (j = 0; j < 255; j++) {
		char ip_str[20];
		sprintf(ip_str, "192.%d.%d.0", i, j);
		inet_aton(ip_str, &dst_ip);
		//printf("add %s\n", ip_str);
		
		if(is_lock)
			pthread_mutex_lock(&lpm_mutex);
		
		ret = rte_lpm_add(lpm_table, htonl(dst_ip.s_addr), 24, nexthop);
		if(ret != 0){
			perror("rte_lpm_add");
		}else{
			printf("add %s success\n", ip_str);
		}
		
		if(is_lock)
			pthread_mutex_unlock(&lpm_mutex);
	}

	return NULL;
}

void look_up_lpm_entry(struct rte_lpm* lpm, char* dst_ip_str){
	struct in_addr dst_ip;
	uint32_t  nexthop;
	int ret;

	printf("lookup %s\n", dst_ip_str);
	inet_aton(dst_ip_str, &dst_ip);
	ret = rte_lpm_lookup(lpm, htonl(dst_ip.s_addr), &nexthop);

	if(ret != 0){
		printf("rte_lpm_lookup error\n");
	}else{
		  printf("dst_ip:%s, nexthop:%d\n", dst_ip_str, nexthop);
	}
	return;
}

int main(int argc, char* argv[]) {
	struct rte_lpm_config config = {0};
	struct rte_lpm *lpm = NULL;

	config.max_rules = MAX_LPM_RULES;
	config.number_tbl8s = 256;

	if(argc >= 2){
		is_lock = 1;
	}

	// 创建LPM表
	lpm_table = rte_lpm_create("LPM_Table", &config);

	if (lpm_table == NULL) {
		printf("Cannot create LPM table\n");
		return -1;
	}


	pthread_t threads[THREADS_NUM];
	int thread_args[THREADS_NUM];

	// 启动线程
	for (unsigned int i = 0; i < THREADS_NUM; i++) {
		thread_args[i] = i;
		pthread_create(&threads[i], NULL, add_lpm_entries, (void*)&thread_args[i]);
	}

	// 等待线程完成
	for (unsigned int i = 0; i < THREADS_NUM; i++) {
		pthread_join(threads[i], NULL);
	}

	rte_lpm_dump(lpm_table);

	look_up_lpm_entry(lpm_table, "192.0.3.5");
	look_up_lpm_entry(lpm_table, "192.1.3.5");
	look_up_lpm_entry(lpm_table, "192.2.3.5");
	
	return 0;
}

