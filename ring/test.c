#include <stdio.h>

int main(){
    unsigned int head, tail;
    unsigned int capacity = 16;

    head = 4;
    tail = 0xfffffffa;
    printf("%u\n", head + tail);
    printf("capacity:%u\n", capacity - head + tail);
    printf("capacity:%d\n", capacity - head + tail);
    return 0;
}
