#include <stdio.h>
#include <stddef.h>
#include <inttypes.h>
static uint32_t rte_combine32ms1b(uint32_t x)
{
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;

	return x;
}

static uint32_t rte_align32pow2(uint32_t x)
{
	x--;
	x = rte_combine32ms1b(x);

	return x + 1;
}

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 算法1：按位操作
unsigned int align_to_power_of_two_bitwise(unsigned int n) {
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

// 算法2：循环
unsigned int align_to_power_of_two_loop(unsigned int n) {
    unsigned int result = 1;
    while (result < n) {
        result *= 2;
    }
    return result;
}

#define TEST_TIMES 2000000
// 性能测试函数
void performance_test() {
    unsigned int n = 1000000;  // 输入整数
    clock_t start, end;
    double elapsed_time;

    // 测试算法1的性能
    start = clock();
    for (int i = 0; i < TEST_TIMES; i++) {
        align_to_power_of_two_bitwise(n);
    }
    end = clock();
    elapsed_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("算法1的执行时间：%f 秒\n", elapsed_time);

    // 测试算法2的性能
    start = clock();
    for (int i = 0; i < TEST_TIMES; i++) {
        align_to_power_of_two_loop(n);
    }
    end = clock();
    elapsed_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("算法2的执行时间：%f 秒\n", elapsed_time);
}


int main(){
	int i;

	for(i=0; i<64; i++){
		printf("%d:%d\n", i, rte_align32pow2(i+1));
	}
    	performance_test();

	return 0;
}
