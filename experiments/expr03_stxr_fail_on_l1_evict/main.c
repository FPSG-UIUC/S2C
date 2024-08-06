#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>

#include <basics/defs.h>
#include <basics/arch.h>
#include <basics/params.h>
#include <basics/cache_utils.h>
#include <basics/sys_utils.h>

int num_addrs = 10;

int main(int argc, char** argv) {
    srand(time(NULL));
    int E_core = 0;

    if (argc == 2)
        if (strcmp(argv[1], "e") == 0)
            E_core = 1;

    if (E_core) {
        printf("perform the test on E-cores.\n");
        pin_p_core(E_CORE_1_ID);
    }
    else {
        printf("perform the test on P-cores.\n");
        pin_p_core(P_CORE_1_ID);
    }

    uint8_t* buf = (uint8_t*)mmap(NULL, PAGE_SIZE * 8, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (buf);
    memset(buf, rand() % 0xff, PAGE_SIZE * 8);

    uint8_t* page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (page);
    memset(page, 0xff, PAGE_SIZE);

    int latency_gt_l1max_and_fail = 0;
    int latency_gt_l1max_and_success = 0;
    int latency_lt_l1max_and_fail = 0;
    int latency_lt_l1max_and_success = 0;

    allocate_cache_flush_buffer();

    uint64_t latency;
    uint32_t fail;
    for (int i = 0; i < num_addrs; i++) {

        uint8_t* addr = page + rand() % PAGE_SIZE;

        for (int j = 0; j < num_counts; j++) {

            int flush_l1 = rand() % 2;

            asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (global_junk)
                : [addr] "r" (addr)
                : "memory");

            if (flush_l1) {
                evict_addr_from_L1(addr);
            }

            asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [val] "+r" (global_junk), [latency] "=r" (latency), [fail] "+r" (fail)
                : [addr] "r" (addr)
                : "x9", "x10", "memory");

            int latency_gt_l1max = latency > L1_HIT_MAX_LATENCY;

            latency_gt_l1max_and_fail    +=  latency_gt_l1max &&  fail;
            latency_gt_l1max_and_success +=  latency_gt_l1max && !fail;
            latency_lt_l1max_and_fail    += !latency_gt_l1max &&  fail;
            latency_lt_l1max_and_success += !latency_gt_l1max && !fail;
        }
    }

    printf("L1 eviction and STXR fail: %d\n", latency_gt_l1max_and_fail);
    printf("L1 eviction and STXR success: %d\n", latency_gt_l1max_and_success);
    printf("L1 hit and STXR fail: %d\n", latency_lt_l1max_and_fail);
    printf("L1 hit and STXR success: %d\n", latency_lt_l1max_and_success);

    if (latency_gt_l1max_and_success + latency_lt_l1max_and_fail < num_addrs * num_counts / 100)
        printf("Strong correlation (>99%%) between L1 evictions and STXR returning fail.\n");
    else
        printf("Warning: weak correlation between L1 evictions and STXR returning fail. Something must be wrong.\n");
    return 0;
}
