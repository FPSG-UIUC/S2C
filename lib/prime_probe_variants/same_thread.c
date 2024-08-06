#include "same_thread.h"

void victim_func(uint8_t* victim_addr) {
    asm volatile (
            "dsb sy\n\t"
            "ldrb w1, [%[addr]]\n\t"
            "dsb sy\n\t"
            :
            : [addr] "r" (victim_addr)
            : "w1");
}
