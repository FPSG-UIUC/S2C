#ifndef __CACHE_UTILS_H__
#define __CACHE_UTILS_H__

#include <stdlib.h>

#define maccess(_addr) \
    asm volatile ( \
            "isb\n\t" \
            "dsb sy\n\t" \
            "ldrb w0, [%[addr]]\n\t" \
            "dsb sy\n\t" \
            "isb\n\t" \
            : : [addr] "r" (_addr) : "x0") \


#define cond_maccess(_cond, _addr) \
    asm volatile ( \
            "isb\n\t" \
            "dsb sy\n\t" \
            "mov x1, #0\n\t" \
            "cmp %w[cond], #0\n\t" \
            "csel x1, %[addr], x1, gt\n\t" \
            "beq SKIP_ACCESS%=\n\t" \
            "ldrb w1, [x1]\n\t" \
            "SKIP_ACCESS%=:\n\t" \
            "dsb sy\n\t" \
            "isb\n\t" \
            : : [cond] "r" ((_cond)), [addr] "r" ((_addr)) : "x1") \


void allocate_cache_flush_buffer();

void flush_cache();

void evict_addr_from_L1(void* addr);

size_t flip_l2_offset(size_t addr);

#endif
