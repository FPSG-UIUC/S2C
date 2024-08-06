#include <stdio.h>
#include <stdlib.h>


#include "../basics/defs.h"
#include "../basics/arch.h"
#include "../basics/allocator.h"
#include "../basics/params.h"
#include "../basics/cache_line_set.h"
#include "../basics/cache_utils.h"
#include "../eviction_set/eviction_set.h"

#include "same_thread.h"
#include "cross_thread.h"
#include "prime_probe_variants.h"

void PrimeY1(uint8_t* victim_addr) {
    uint8_t load_val = 0;

    asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldxrb %w[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [addr] "r" (victim_addr)
            : "memory");
}

int ProbeY1(uint8_t* victim_addr) {

    uint8_t load_val = 0;
    int fail = 0;

    asm volatile (
            "isb\n\t"
            "dmb sy\n\t"
            "dsb sy\n\t"
            "stxrb %w[fail], %w[val], [%[addr]]\n\t"
            "dmb sy\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [fail] "+r" (fail), [val] "+r" (load_val) // prevent fail and val from
                                                        // getting mapped to the same reg
            : [addr] "r" (victim_addr) 
            : "memory");

    return fail;
}

void PrimeP1S1(uint8_t* pivot_addr, eviction_set_t* eviction_set) {

    uint8_t load_val = 0;

    asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldxrb %w[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [addr] "r" (pivot_addr)
            : "memory");

    // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
    dual_traverse_roundtrip(eviction_set, 4);
    /*traverse_eviction_set(eviction_set);*/

/*#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)*/
    /*traverse_eviction_set(eviction_set);*/
/*#endif*/

/*#if defined(TRIPLE_TRAVERSE)*/
    /*traverse_eviction_set(eviction_set);*/
/*#endif*/
}

int ProbeS1P1(uint8_t* pivot_addr, eviction_set_t* eviction_set) {

    uint8_t load_val = 0;
    int fail = 0;

    // traverse the eviction set to evict pivot_addr
    traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
    traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
    traverse_eviction_set(eviction_set);
#endif

    asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "stxrb %w[fail], %w[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            : [fail] "+r" (fail), [val] "+r" (load_val) // prevent fail and val from
                                                        // getting mapped to the same reg
            : [addr] "r" (pivot_addr) 
            : "memory");

    return fail;
}

void PrimeP1_Load(uint64_t* pivot_addr) {
    uint8_t load_val = 0;

    asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldr %[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [addr] "r" (pivot_addr)
            : "memory");
}


void PrimeP1S1_Load(uint64_t* pivot_addr, eviction_set_t* eviction_set) {

    uint64_t load_val = 0;

    asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldr %[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [addr] "r" (pivot_addr)
            : "memory");

    // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
    dual_traverse_roundtrip(eviction_set, 8);
    /*traverse_eviction_set(eviction_set);*/

/*#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)*/
    /*traverse_eviction_set(eviction_set);*/
/*#endif*/

/*#if defined(TRIPLE_TRAVERSE)*/
    /*traverse_eviction_set(eviction_set);*/
/*#endif*/
}

uint64_t ProbeP1_Load(uint64_t* pivot_addr) {

    uint64_t load_val = 0;

    asm volatile (
            /*"isb\n\t"*/
            "dsb sy\n\t"
            "ldr %[val], [%[addr]]\n\t"
            "dsb sy\n\t"
            : [val] "+r" (load_val) // getting mapped to the same reg
            : [addr] "r" (pivot_addr) 
            : "memory");

    return load_val;
}

float P1S1P1_timer(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_evict = 0;
    uint32_t threshold;

    if (evrate_to_ret == RET_L1_EVRATE)
        threshold = L1_HIT_MAX_LATENCY;
    else if (evrate_to_ret == RET_L2_EVRATE)
        threshold = L2_MISS_MIN_LATENCY;
    else {
        fprintf(stderr, "ERROR: evrate_to_ret = %d. Should be RET_L1_EVRATE or RET_L2_EVRATE\n", evrate_to_ret);
        exit(1);
    }

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr));

        traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        // this delay is crucial in maintaining the ordering between eviction set access
        // and the subsequent load
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");

        total_evict += (latency > threshold);
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set);

    return (float)total_evict / num_counts;
}

float P1S1P1_timer_naive_traverse(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_evict = 0;
    uint32_t threshold;

    if (evrate_to_ret == RET_L1_EVRATE)
        threshold = L1_HIT_MAX_LATENCY;
    else if (evrate_to_ret == RET_L2_EVRATE)
        threshold = L2_MISS_MIN_LATENCY;
    else {
        fprintf(stderr, "ERROR: evrate_to_ret = %d. Should be RET_L1_EVRATE or RET_L2_EVRATE\n", evrate_to_ret);
        exit(1);
    }

    for (int i = 0; i < num_counts; i++) {
        // load
        asm volatile (
            // load
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"

            // traverse the eviction set
            "L_fwd_traverse_P1S1P1_timer_naive_traverse:\n\t"
            "ldr %[fwd_iter], [%[fwd_iter]]\n\t"
            "cbnz %[fwd_iter], L_fwd_traverse_P1S1P1_timer_naive_traverse\n\t"
            "dsb sy\n\t"

            // measure the 2nd load
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "+r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr),
              [fwd_iter] "r" (eviction_set->list_of_cachelines->head)
            : "x9", "x10", "memory"); // memory must be added to clobbed list, o/w weird bug

        total_evict += (latency > threshold);
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set);

    return (float)total_evict / num_counts;
}

float P1S1P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        traverse_eviction_set(eviction_set);
#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif
#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set);

    return (float)total_fail / num_counts;
}

float P1S1P1_llsc_naive_traverse(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {
    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    uint8_t load_val = 0;
    uint32_t total_sc_fail = 0, fail = 0;

    for (int i = 0; i < num_counts; i++) {
        // load-linked
        asm volatile (
            // load-linked
            "dsb sy\n\t"
            "ldxrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"

            // this instruction barrier is necessary
            // load-linked seems to execute late, so w/o this barrier, subsequent eviction set accesses might surpass ldxr
            "isb\n\t"

            // traverse the eviction set - in forward direction
            "L_fwd_traverse_P1S1P1_llsc_naive_traverse:\n\t"
            "ldr %[fwd_iter], [%[fwd_iter]]\n\t"
            "cbnz %[fwd_iter], L_fwd_traverse_P1S1P1_llsc_naive_traverse\n\t"

            // this instruction barrier is necessary
            // store-conditional seems to have a slight chance surpass prior loads
            "isb\n\t"

            // store-conditional
            "dsb sy\n\t"
            "stxrb %w[failed], %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"

            // this instruction barrier is necessary in theory, but in practice it seems unnecessary
            // store-conditional seems to execute late, so w/o this barrier, younger loads might surpass stxr
            /*"isb\n\t"*/
            : [val] "+r" (load_val), [failed] "+r" (fail) //, [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr),
              [fwd_iter] "r" (eviction_set->list_of_cachelines->head)
            : "memory"); // memory must be added to the clobbed list here, o/w weird bug

        total_sc_fail += fail;
    }

    delete_eviction_set(eviction_set);

    return (float)total_sc_fail / num_counts;
}

float P1S2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t" // TODO: this isb is unncessary
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t" // TODO: this isb is unnecessary
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr));

        wakeup_child_traverse_set_and_wait();

        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");


        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    delete_eviction_set(eviction_set);

    return (float) total_l2_evict / num_counts;
}

float P1S2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t" // TODO: this isb is unncessary
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t" // TODO: this isb is unnecessary
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        wakeup_child_traverse_set_and_wait();

        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    delete_eviction_set(eviction_set);

    return (float)total_fail / num_counts;
}

float P1S1Y1P1_timer_naive_traverse(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_evict = 0;
    uint32_t threshold;

    if (evrate_to_ret == RET_L1_EVRATE)
        threshold = L1_HIT_MAX_LATENCY;
    else if (evrate_to_ret == RET_L2_EVRATE)
        threshold = L2_MISS_MIN_LATENCY;
    else {
        fprintf(stderr, "ERROR: evrate_to_ret = %d. Should be RET_L1_EVRATE or RET_L2_EVRATE\n", evrate_to_ret);
        exit(1);
    }

    for (int i = 0; i < num_counts; i++) {

        asm volatile (
            // load
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"

            // traverse the eviction set
            "L_fwd_traverse_P1S1Y1P1_timer_naive_traverse:\n\t"
            "ldr %[fwd_iter], [%[fwd_iter]]\n\t"
            "cbnz %[fwd_iter], L_fwd_traverse_P1S1Y1P1_timer_naive_traverse\n\t"
            "dsb sy\n\t"
            : [val] "+r" (load_val)
            : [pivot_addr] "r" (pivot_addr),
              [fwd_iter] "r" (eviction_set->list_of_cachelines->head)
            : "memory");

        // we somehow need this instruction barrier
        // o/w occationally we can see eviction rate = 50%
        asm volatile ("isb\n\t");

        // emulate victim behavior that includes accessing victim addr
        victim_func(victim_addr);

        // measure the 2nd load
        asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "+r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr)
            : "x9", "x10", "memory");

        total_evict += (latency > threshold);
    }

    delete_eviction_set(eviction_set);

    return (float)total_evict / num_counts;
}

float P1S1Y1P1_llsc_naive_traverse(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    for (int i = 0; i < num_counts; i++) {
        // load-linked
        asm volatile (
            // load-linked
            "dsb sy\n\t"
            "ldxrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"

            // this instruction barrier is necessary
            // load-linked seems to execute late, so w/o this barrier, subsequent eviction set accesses might surpass ldxr
            "isb\n\t"

            // traverse the eviction set - in forward direction
            "L_fwd_traverse_P1S1Y1P1_llsc_naive_traverse:\n\t"
            "ldr %[fwd_iter], [%[fwd_iter]]\n\t"
            "cbnz %[fwd_iter], L_fwd_traverse_P1S1Y1P1_llsc_naive_traverse\n\t"
            : [val] "+r" (load_val)
            : [pivot_addr] "r" (pivot_addr),
              [fwd_iter] "r" (eviction_set->list_of_cachelines->head)
            : "memory");

        // we somehow need this instruction barrier
        // o/w we occationally see fail rate = 50%
        asm volatile ("isb\n\t");

        // emulate victim behavior that includes accessing victim addr
        victim_func(victim_addr);

        asm volatile (
            // this instruction barrier is necessary
            // store-conditional seems to have a slight chance surpass prior loads
            "isb\n\t"

            // store-conditional
            "dsb sy\n\t"
            "stxrb %w[fail], %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            : [fail] "+r" (fail) //, [latency] "=r" (latency)
            : [val] "r" (load_val),
              [pivot_addr] "r" (pivot_addr)
            : "memory");

        total_fail += fail;
    }

    delete_eviction_set(eviction_set);

    return (float)total_fail / num_counts;
}

float P1S1Y2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_access_addr(victim_addr);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
        dual_traverse_roundtrip(eviction_set, 8);
        /*traverse_eviction_set(eviction_set);*/

/*#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set(eviction_set);*/
/*#endif*/

/*#if defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set(eviction_set);*/
/*#endif*/

        asm volatile ("isb\n\t");

        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dmb sy\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                "dmb sy\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");

        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_access_addr_thread();
    delete_eviction_set(eviction_set);

    return (float) total_l2_evict / num_counts;
}

float P1S1Y2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_access_addr(victim_addr);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
        dual_traverse_roundtrip(eviction_set, 8);
        /*traverse_eviction_set(eviction_set);*/

/*#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set(eviction_set);*/
/*#endif*/

/*#if defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set(eviction_set);*/
/*#endif*/

        asm volatile ("isb\n\t");

        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");
        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        // store-conditional and see if llsc_addr is evicted
        asm volatile (
                "dmb sy\n\t"
                "isb\n\t"
                "dsb sy\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "dmb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    terminate_child_access_addr_thread();
    delete_eviction_set(eviction_set);

    return (float)total_fail / num_counts;
}

float P1S1Y2S1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_access_addr(victim_addr);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr));

        // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
        traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        asm volatile ("isb\n\t");

        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // traverse the eviction set again. This seems to add accuracy
        traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");

        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_access_addr_thread();
    delete_eviction_set(eviction_set);

    return (float) total_l2_evict / num_counts;
}

float P1S1Y2S1P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_access_addr(victim_addr);

    // create child thread, which is responsible for traversing eviction set
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        // then traverse the rest of the eviction set. This will turn llsc_addr into LRU
        traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        asm volatile ("isb\n\t");

        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // traverse the eviction set again. This seems to add accuracy
        traverse_eviction_set(eviction_set);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set);
#endif

        // store-conditional and see if llsc_addr is evicted
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    terminate_child_access_addr_thread();
    delete_eviction_set(eviction_set);

    return (float)total_fail / num_counts;
}


float P1S2Y3P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {
    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);
    set_child_access_addr(victim_addr);

    // create child threads. One for helping attacker traversing the eviction set,
    // the other represents the victim accessing the victim address
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_3_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr));

        // let helper thread to traverse eviction set
        wakeup_child_traverse_set_and_wait();

        asm volatile ("isb\n\t");

        // let victim thread access victim_addr
        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");

        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    terminate_child_access_addr_thread();

    delete_eviction_set(eviction_set);

    return (float) total_l2_evict / num_counts;
}

float P1S2Y3P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {

    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);
    set_child_access_addr(victim_addr);

    // create child threads. One for helping attacker traversing the eviction set,
    // the other represents the victim
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_3_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        // let helper thread to traverse eviction set
        wakeup_child_traverse_set_and_wait();

        asm volatile ("isb\n\t");

        // let victim thread access victim_addr
        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        asm volatile (
                "dsb sy\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    terminate_child_access_addr_thread();

    delete_eviction_set(eviction_set);
    return (float)total_fail / num_counts;
}

float P1S2Y3S2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {
    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);
    set_child_access_addr(victim_addr);

    // create child threads. One for helping attacker traversing the eviction set,
    // the other represents the victim accessing the victim address
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_3_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr));

        // let helper thread to traverse eviction set
        wakeup_child_traverse_set_and_wait();

        asm volatile ("isb\n\t");

        // let victim thread access victim_addr
        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // let helper thread to traverse eviction set again
        wakeup_child_traverse_set_and_wait();

        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "x9", "x10");

        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    terminate_child_access_addr_thread();

    delete_eviction_set(eviction_set);

    return (float) total_l2_evict / num_counts;
}

float P1S2Y3S2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr) {
    eviction_set_t* eviction_set = create_eviction_set(evset_cache_lines);

    set_child_eviction_set(eviction_set);
    set_child_access_addr(victim_addr);

    // create child threads. One for helping attacker traversing the eviction set,
    // the other represents the victim accessing the victim address
    create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_3_ID);

    // warm up the instruction cache
    wakeup_child_traverse_set_and_wait();
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint32_t total_fail = 0, fail = 0;
    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldxrb %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [addr] "r" (pivot_addr)
                : "memory");

        // let helper thread to traverse eviction set
        wakeup_child_traverse_set_and_wait();

        asm volatile ("isb\n\t");

        // let victim thread access victim_addr
        wakeup_child_access_addr_and_wait();

        asm volatile ("isb\n\t");

        // let helper thread to traverse eviction set again
        wakeup_child_traverse_set_and_wait();

        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                "dsb sy\n\t"
                : [fail] "+r" (fail)
                : [addr] "r" (pivot_addr), [val] "r" (load_val)
                : "memory");

        total_fail += fail;
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();
    terminate_child_access_addr_thread();

    delete_eviction_set(eviction_set);
    return (float)total_fail / num_counts;
}

void P1Sb1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_b_cache_lines, float* l1_evict_rate, float* l2_evict_rate) {

    eviction_set_t* eviction_set_part_b = create_eviction_set(evset_part_b_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l2_evict = 0, l2_evict = 0;
    uint32_t total_l1_evict = 0, l1_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        asm volatile (
                "isb\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[pivot_addr]]\n\t"
                "dsb sy\n\t"
                "isb\n\t"
                : [val] "=r" (load_val)
                : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER1
        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_b);

#ifdef BARRIER2
        // this delay is crucial, o/w we will get bogus measurement results
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[out], [%[pivot_addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [out] "=r" (load_val)
                : [pivot_addr] "r" (pivot_addr)
                : "x9", "x10");



        l1_evict = (latency > L1_HIT_MAX_LATENCY);
        total_l1_evict += l1_evict;
        l2_evict = (latency > L2_MISS_MIN_LATENCY);
        total_l2_evict += l2_evict;
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set_part_b);

    *l1_evict_rate = (float)total_l1_evict / num_counts;
    *l2_evict_rate = (float)total_l2_evict / num_counts;
}

void P1Sa1P1Sb1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, float* l1_evict_rate, float* l2_evict_rate) {
    eviction_set_t* eviction_set_part_a = create_eviction_set(evset_part_a_cache_lines);
    eviction_set_t* eviction_set_part_b = create_eviction_set(evset_part_b_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l1_evict = 0, total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER1
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_a);

        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER2
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_b);

        asm volatile ("isb\n\t");

        for (volatile int k = 0; k < 1000; k++) {}

        // time load pivot addr
        asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "=r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr)
            : "x9", "x10", "memory");

        total_l1_evict += (latency > L1_HIT_MAX_LATENCY);
        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set_part_a);
    delete_eviction_set(eviction_set_part_b);

    *l1_evict_rate = (float)total_l1_evict / num_counts;
    *l2_evict_rate = (float)total_l2_evict / num_counts;
}

void P1Sa1P1Sb1Y1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate) {

    eviction_set_t* eviction_set_part_a = create_eviction_set(evset_part_a_cache_lines);
    eviction_set_t* eviction_set_part_b = create_eviction_set(evset_part_b_cache_lines);

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l1_evict = 0, total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER1
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_a);

        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER2
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_b);

        // load victim addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[victim_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [victim_addr] "r" (victim_addr));

        for (volatile int k = 0; k < 1000; k++) {}

        // time load pivot addr
        asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "=r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr)
            : "x9", "x10", "memory");

        total_l1_evict += (latency > L1_HIT_MAX_LATENCY);
        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    delete_eviction_set(eviction_set_part_a);
    delete_eviction_set(eviction_set_part_b);

    *l1_evict_rate = (float)total_l1_evict / num_counts;
    *l2_evict_rate = (float)total_l2_evict / num_counts;
}

void P1Sa1P1Sb1Y2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate) {

    eviction_set_t* eviction_set_part_a = create_eviction_set(evset_part_a_cache_lines);
    eviction_set_t* eviction_set_part_b = create_eviction_set(evset_part_b_cache_lines);

    set_child_access_addr(victim_addr);

    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l1_evict = 0, total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER1
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_a);

        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER2
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_b);

        wakeup_child_access_addr_and_wait();

        for (volatile int k = 0; k < 1000; k++) {}

        // time load pivot addr
        asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "=r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr)
            : "x9", "x10", "memory");

        total_l1_evict += (latency > L1_HIT_MAX_LATENCY);
        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_access_addr_thread();

    delete_eviction_set(eviction_set_part_a);
    delete_eviction_set(eviction_set_part_b);

    *l1_evict_rate = (float)total_l1_evict / num_counts;
    *l2_evict_rate = (float)total_l2_evict / num_counts;
}

void P1Sa1P1Sb1Sc2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, cache_line_set_t* evset_part_c_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate) {

    eviction_set_t* eviction_set_part_a = create_eviction_set(evset_part_a_cache_lines);
    eviction_set_t* eviction_set_part_b = create_eviction_set(evset_part_b_cache_lines);
    eviction_set_t* eviction_set_part_c = create_eviction_set(evset_part_c_cache_lines);

    set_child_eviction_set(eviction_set_part_c);

    create_child_thread(CHILD_NAIVELY_TRAVERSE_SET, P_CORE_2_ID);

    // warm up the instruction cache
    wakeup_child_access_addr_and_wait();

    uint8_t load_val = 0;
    uint64_t latency = 0;
    uint32_t total_l1_evict = 0, total_l2_evict = 0;

    for (int i = 0; i < num_counts; i++) {
        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER1
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_a);

        // load pivot addr
        asm volatile (
            "isb\n\t"
            "dsb sy\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "dsb sy\n\t"
            "isb\n\t"
            : [val] "=r" (load_val)
            : [pivot_addr] "r" (pivot_addr));

#ifdef BARRIER2
        for (volatile int k = 0; k < 1000; k++) {}
#endif

        traverse_small_eviction_set(eviction_set_part_b);

        wakeup_child_traverse_set_and_wait();

        for (volatile int k = 0; k < 1000; k++) {}

        // time load pivot addr
        asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[val], [%[pivot_addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            "dsb sy\n\t"
            : [val] "=r" (load_val), [latency] "=r" (latency)
            : [pivot_addr] "r" (pivot_addr)
            : "x9", "x10", "memory");

        total_l1_evict += (latency > L1_HIT_MAX_LATENCY);
        total_l2_evict += (latency > L2_MISS_MIN_LATENCY);
        /*flush_cache();*/
    }

    terminate_child_traverse_set_thread();

    delete_eviction_set(eviction_set_part_a);
    delete_eviction_set(eviction_set_part_b);

    *l1_evict_rate = (float)total_l1_evict / num_counts;
    *l2_evict_rate = (float)total_l2_evict / num_counts;
}
