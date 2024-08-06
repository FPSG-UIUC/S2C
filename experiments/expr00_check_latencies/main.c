#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <basics/defs.h>
#include <basics/arch.h>
#include <basics/params.h>
#include <basics/cache_utils.h>
#include <basics/sys_utils.h>

int main() {
    pin_p_core(P_CORE_1_ID);

    uint8_t* page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (page);
    memset(page, 0xff, PAGE_SIZE);

    uint8_t* addr = page + rand() % PAGE_SIZE;

    allocate_cache_flush_buffer();

    uint64_t total_latency = 0, latency = 0;

    // empty latency: latency between consecutive perf counter read. Nothing in between.
    total_latency = 0;
    uint64_t min_empty_latency = 9999;
    uint64_t max_empty_latency = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                : [latency] "=r" (latency)
                :
                : "x9", "x10");
        total_latency += latency;
        if (latency < min_empty_latency)
            min_empty_latency = latency;
        if (latency > max_empty_latency)
            max_empty_latency = latency;
    }
    printf("empty latency: [%ld, %ld] average: %ld\n", min_empty_latency, max_empty_latency, total_latency / num_counts);

    // L1 hit latency
    total_latency = 0;
    uint64_t min_l1_latency = 9999;
    uint64_t max_l1_latency = 0;
    global_junk ^= *addr;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                : [latency] "=r" (latency), [val] "=r" (global_junk)
                : [addr] "r" (addr)
                : "x9", "x10");

        total_latency += latency;
        if (latency < min_l1_latency)
            min_l1_latency = latency;
        if (latency > max_l1_latency)
            max_l1_latency = latency;
    }
    printf("L1 latency: [%ld, %ld] average: %ld\n", min_l1_latency, max_l1_latency, total_latency / num_counts);

    // L2 hit latency
    total_latency = 0;
    uint64_t min_l2_latency = 9999;
    uint64_t max_l2_latency = 0;
    global_junk ^= *addr;
    for (int i = 0; i < num_counts; i++) {
        evict_addr_from_L1(addr);

        asm volatile (
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                : [latency] "=r" (latency), [val] "=r" (global_junk)
                : [addr] "r" (addr)
                : "x9", "x10");

        total_latency += latency;
        if (latency < min_l2_latency)
            min_l2_latency = latency;
        if (latency > max_l2_latency)
            max_l2_latency = latency;
    }
    printf("L2 latency: [%ld, %ld] average: %ld\n", min_l2_latency, max_l2_latency, total_latency / num_counts);

    // L2 miss latency
    total_latency = 0;
    uint64_t min_l3_latency = 9999;
    uint64_t max_l3_latency = 0;
    global_junk ^= *addr;
    for (int i = 0; i < num_counts; i++) {
        flush_cache();

        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                : [latency] "=r" (latency), [val] "=r" (global_junk)
                : [addr] "r" (addr)
                : "x9", "x10");

        total_latency += latency;
        if (latency < min_l3_latency)
            min_l3_latency = latency;
        if (latency > max_l3_latency)
            max_l3_latency = latency;
    }
    printf("L3 latency: [%ld, %ld] average: %ld\n", min_l3_latency, max_l3_latency, total_latency / num_counts);

    if (L1_HIT_MAX_LATENCY < max_l1_latency || L1_HIT_MAX_LATENCY > min_l2_latency) {
        printf("WARNING: L1_HIT_MAX_LATENCY is set incorrectly to %d (max_l1_latency = %ld, min_l2_latency = %ld)\n", L1_HIT_MAX_LATENCY, max_l1_latency, min_l2_latency);
        printf("This is probably because you are using a different M1 processor from the one used for this project. Consider changing the value of L1_HIT_MAX_LATENCY accordingly.\n");
    }
    else if (L2_MISS_MIN_LATENCY < max_l2_latency || L2_MISS_MIN_LATENCY > min_l3_latency) {
        printf("WARNING: L2_MISS_MIN_LATENCY is set incorrectly to %d (max_l2_latency = %ld, min_l3_latency = %ld)\n", L2_MISS_MIN_LATENCY, max_l2_latency, min_l3_latency);
        printf("This is probably because you are using a different M1 processor from the one used for this project. Consider changing the value of L1_HIT_MAX_LATENCY accordingly.\n");
    }
    else {
        printf("Latency numbers L1_HIT_MAX_LATENCY (%d) and L2_MISS_MIN_LATENCY (%d) are set properly.\n", L1_HIT_MAX_LATENCY, L2_MISS_MIN_LATENCY);
    }
}
