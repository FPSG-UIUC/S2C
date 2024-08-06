#include <alloca.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <basics/defs.h>
#include <basics/arch.h>
#include <basics/params.h>
#include <basics/cache_line_set.h>
#include <basics/cache_utils.h>
#include <basics/sys_utils.h>
#include <basics/math_utils.h>
#include <prime_probe_variants/prime_probe_variants.h>
#include <prime_probe_variants/same_thread.h>
#include <prime_probe_variants/cross_thread.h>
#include <eviction_set/eviction_set_generation.h>

#ifndef NUM_TRIPWIRES
#define NUM_TRIPWIRES 2
#endif
#define MAX_NUM_TRIPWIRES 16

#define NUM_EVSET_LINE_IN_SAME_L1 4

#define hash  0xdeadbeef12340000

#define DELAY1 10000
#define DELAY2 100

#define ADD_DELAY
#define DIFF (NUM_TRIPWIRES * 7 + 2)

#define NUM_REPS 20

#ifdef VERBOSE
#define vprintf(...) printf(...)
#else
#define vprintf(...)
#endif

#define DELAY_MACRO(count)  asm volatile (      \
                    "mov x9, %[val]\n\t"        \
                    "L_delay%=:\n\t"            \
                    "sub x9, x9, #1\n\t"        \
                    "cbnz x9, L_delay%=\n\t"    \
                    :                           \
                    : [val] "r" (count)         \
                    : "x9")                     \

#define POINTER_CHASING_0_MACRO node = (uint64_t*)((*node) ^ hash);

#define POINTER_CHASING_1_MACRO POINTER_CHASING_0_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_2_MACRO POINTER_CHASING_1_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_3_MACRO POINTER_CHASING_2_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_4_MACRO POINTER_CHASING_3_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_5_MACRO POINTER_CHASING_4_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_6_MACRO POINTER_CHASING_5_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_7_MACRO POINTER_CHASING_6_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_8_MACRO POINTER_CHASING_7_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_9_MACRO POINTER_CHASING_8_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_10_MACRO POINTER_CHASING_9_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_11_MACRO POINTER_CHASING_10_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_12_MACRO POINTER_CHASING_11_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_13_MACRO POINTER_CHASING_12_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_14_MACRO POINTER_CHASING_13_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_15_MACRO POINTER_CHASING_14_MACRO POINTER_CHASING_0_MACRO
#define POINTER_CHASING_16_MACRO POINTER_CHASING_15_MACRO POINTER_CHASING_0_MACRO

#define POINTER_CHASING_FUNCTION_DEFINER(i) \
    uint64_t* POINTER_CHASING_GET_ ## i ## _FUNC(uint64_t* node) { POINTER_CHASING_## i ##_MACRO(node); return node; }

POINTER_CHASING_FUNCTION_DEFINER(0)
POINTER_CHASING_FUNCTION_DEFINER(1)
POINTER_CHASING_FUNCTION_DEFINER(2)
POINTER_CHASING_FUNCTION_DEFINER(3)
POINTER_CHASING_FUNCTION_DEFINER(4)
POINTER_CHASING_FUNCTION_DEFINER(5)
POINTER_CHASING_FUNCTION_DEFINER(6)
POINTER_CHASING_FUNCTION_DEFINER(7)
POINTER_CHASING_FUNCTION_DEFINER(8)
POINTER_CHASING_FUNCTION_DEFINER(9)
POINTER_CHASING_FUNCTION_DEFINER(10)
POINTER_CHASING_FUNCTION_DEFINER(11)
POINTER_CHASING_FUNCTION_DEFINER(12)
POINTER_CHASING_FUNCTION_DEFINER(13)
POINTER_CHASING_FUNCTION_DEFINER(14)
POINTER_CHASING_FUNCTION_DEFINER(15)
POINTER_CHASING_FUNCTION_DEFINER(16)

void* pointer_chasing_fptrs[NUM_TRIPWIRES+1] = {
    POINTER_CHASING_GET_0_FUNC,
    POINTER_CHASING_GET_1_FUNC,
    POINTER_CHASING_GET_2_FUNC,
    POINTER_CHASING_GET_3_FUNC,
    POINTER_CHASING_GET_4_FUNC,
    POINTER_CHASING_GET_5_FUNC,
    POINTER_CHASING_GET_6_FUNC,
    POINTER_CHASING_GET_7_FUNC,
    POINTER_CHASING_GET_8_FUNC,
    POINTER_CHASING_GET_9_FUNC,
    POINTER_CHASING_GET_10_FUNC,
    POINTER_CHASING_GET_11_FUNC,
    POINTER_CHASING_GET_12_FUNC,
    POINTER_CHASING_GET_13_FUNC,
    POINTER_CHASING_GET_14_FUNC,
    POINTER_CHASING_GET_15_FUNC,
    POINTER_CHASING_GET_16_FUNC
};
uint64_t* POINTER_CHASING_GET_X(uint64_t* start) {
    uint64_t* (*func_ptr)(uint64_t*) = pointer_chasing_fptrs[NUM_TRIPWIRES];
    return (*func_ptr)(start);
}

volatile uint8_t junk = 0;

extern int child_record;
extern int child_idx;

int main(int argc, char** argv) {

    printf("NUM_TRIPWIRES = %d , DIFF = %d\n", NUM_TRIPWIRES, DIFF);
    if (NUM_TRIPWIRES > MAX_NUM_TRIPWIRES) {
        fprintf(stderr, "num tripwires = %d exceeds max %d!\n", NUM_TRIPWIRES, MAX_NUM_TRIPWIRES);
        exit(0);
    }
        

    srand(time(NULL));
    pin_p_core(P_CORE_1_ID);

    allocate_cache_flush_buffer();

    uint8_t* victim_page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(victim_page, 0xff, PAGE_SIZE);
    uint64_t* start = victim_page;

    uint8_t* victim_addr_x = victim_page + (rand() % 20 + 2) * L2_LINE_SIZE * 2;
    uint8_t* victim_addr_x_diff_l2 = victim_addr_x + L2_LINE_SIZE;

    uint8_t* victim_addr_y = victim_addr_x + 5 * L2_LINE_SIZE;
    uint8_t* victim_addr_z = victim_addr_x + 10 * L2_LINE_SIZE;

    uint8_t* victim_addrs[NUM_TRIPWIRES];
    uint8_t* victim_addrs_diff_l2[NUM_TRIPWIRES];
    for (int i = 0; i < NUM_TRIPWIRES; i++) {
        victim_addrs[i] = victim_addr_z + 5 * i * L2_LINE_SIZE;
        victim_addrs_diff_l2[i] = victim_addrs[i] + L2_LINE_SIZE;
    }

    num_counts = 10;

    cache_line_set_t* l2_congruent_cache_lines_x = find_L2_eviction_set_using_timer(victim_addr_x);

    cache_line_set_t* l2_congruent_cache_lines_victim[NUM_TRIPWIRES];
    for (int i = 0; i < NUM_TRIPWIRES; i++)
        l2_congruent_cache_lines_victim[i] = find_L2_eviction_set_using_timer(victim_addrs[i]);

    eviction_set_t* assisted_l2_evset_x = create_eviction_set(find_L2_eviction_set_using_timer(victim_addr_x));

    uint64_t* pivot_addr_x;
    eviction_set_t* l2_evset_x;
    uint64_t* pivot_addrs_victim[NUM_TRIPWIRES];
    eviction_set_t* l2_evset_victim[NUM_TRIPWIRES];

    int search_done = 0;
    int time_out_count = 0, time_out_max = 50;
    while (!search_done) {
        if (time_out_count++ == time_out_max)
            exit(0);
        cache_line_set_t* l2_congruent_cache_lines_x_copy = copy_cache_line_set(l2_congruent_cache_lines_x);
        cache_line_set_t* l2_congruent_cache_lines_victim_copy[NUM_TRIPWIRES];
        for (int i = 0; i < NUM_TRIPWIRES; i++)
            l2_congruent_cache_lines_victim_copy[i] = copy_cache_line_set(l2_congruent_cache_lines_victim[i]);

        shuffle_cache_line_set(l2_congruent_cache_lines_x_copy);
        for (int i = 0; i < NUM_TRIPWIRES; i++)
            shuffle_cache_line_set(l2_congruent_cache_lines_victim_copy[i]);

        pivot_addr_x = (uint64_t*)pop_cache_line_from_set(l2_congruent_cache_lines_x_copy);
        for (int n = NUM_EVSET_LINE_IN_SAME_L1; n < l2_congruent_cache_lines_x_copy->num_cache_lines; n++)
            l2_congruent_cache_lines_x_copy->cache_lines[n] = flip_l2_offset(l2_congruent_cache_lines_x_copy->cache_lines[n]);

        for (int i = 0; i < NUM_TRIPWIRES; i++) {
            pivot_addrs_victim[i] = (uint64_t*)pop_cache_line_from_set(l2_congruent_cache_lines_victim_copy[i]);
            for (int n = NUM_EVSET_LINE_IN_SAME_L1; n < l2_congruent_cache_lines_victim_copy[i]->num_cache_lines; n++) {
                l2_congruent_cache_lines_victim_copy[i]->cache_lines[n] = flip_l2_offset(l2_congruent_cache_lines_victim_copy[i]->cache_lines[n]);
            }
        }

        *start = (uint64_t)pivot_addrs_victim[0] ^ hash;

        for (int i = 0; i < NUM_TRIPWIRES-1; i++)
            *pivot_addrs_victim[i] = (uint64_t)pivot_addrs_victim[i+1] ^ hash;

        *pivot_addrs_victim[NUM_TRIPWIRES-1] = (uint64_t)pivot_addr_x ^ hash;
        printf("%p\n", pivot_addr_x); 

        *pivot_addr_x = 1234;

        num_shuffles = 100;
        num_counts = 100;
        int satisfied = 1;
        int num_fails = 0;
        uint8_t load_val;
        int latency;

        /* Search eviction set for A */
        for (int i = 0; i < NUM_TRIPWIRES; i++) {
            uint64_t* (*pointer_chasing_get)(uint64_t*) = pointer_chasing_fptrs[i];
            for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
                shuffle_cache_line_set(l2_congruent_cache_lines_victim_copy[i]);
                l2_evset_victim[i] = create_eviction_set(l2_congruent_cache_lines_victim_copy[i]);

                create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);
                satisfied = 1;
                vprintf("----- find %d shuffle %d  ---\n", i, shuffle);
                for (int rep = 0; rep < 10; rep++) {
                    flush_cache();

                    set_child_access_addr(victim_addrs[i]);
                    wakeup_child_access_addr_and_wait();
                    num_fails = 0;
                    for (int n = 0; n < num_counts; n++) {
                        asm volatile ("isb\n\t");

                        PrimeP1S1_Load(pivot_addrs_victim[i], l2_evset_victim[i]);

                        /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                        DELAY_MACRO(DELAY1);

                        asm volatile ("isb\n\t");
                        wakeup_child_access_addr_and_wait();
                        asm volatile ("isb\n\t");

                        /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                        DELAY_MACRO(DELAY1);

                        dual_traverse_roundtrip(l2_evset_victim[i], 2);

                        uint64_t* _pivot_addr = (*pointer_chasing_get)(start);

                        asm volatile (
                            "dsb sy\n\t"
                            "isb\n\t"
                            "mrs x9, S3_2_c15_c0_0\n\t"
                            "ldrb %w[val], [%[pivot_addr]]\n\t"
                            "isb\n\t"
                            "mrs x10, S3_2_c15_c0_0\n\t"
                            "sub %[latency], x10, x9\n\t"
                            : [val] "+r" (load_val), [latency] "=r" (latency)
                            : [pivot_addr] "r" (_pivot_addr)
                            : "x9", "x10", "memory");

                        num_fails += (latency > L2_MISS_MIN_LATENCY);

                        asm volatile ("isb\n\t");
                    }
                    float evict_rate = (float)num_fails / num_counts;

                    set_child_access_addr(victim_addrs_diff_l2[i]);
                    wakeup_child_access_addr_and_wait();
                    num_fails = 0;
                    for (int n = 0; n < num_counts; n++) {
                        asm volatile ("isb\n\t");

                        PrimeP1S1_Load(pivot_addrs_victim[i], l2_evset_victim[i]);

                        /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                        DELAY_MACRO(DELAY1);

                        asm volatile ("isb\n\t");
                        wakeup_child_access_addr_and_wait();
                        asm volatile ("isb\n\t");

                        /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                        DELAY_MACRO(DELAY1);

                        dual_traverse_roundtrip(l2_evset_victim[i], 2);

                        /*uint64_t* _pivot_addr_a = (uint64_t*)((*start) ^ hash0);*/
                        uint64_t* _pivot_addr = (*pointer_chasing_get)(start);

                        asm volatile (
                            "dsb sy\n\t"
                            "isb\n\t"
                            "mrs x9, S3_2_c15_c0_0\n\t"
                            "ldrb %w[val], [%[pivot_addr]]\n\t"
                            "isb\n\t"
                            "mrs x10, S3_2_c15_c0_0\n\t"
                            "sub %[latency], x10, x9\n\t"
                            : [val] "+r" (load_val), [latency] "=r" (latency)
                            : [pivot_addr] "r" (_pivot_addr)
                            : "x9", "x10", "memory");

                        num_fails += (latency > L2_MISS_MIN_LATENCY);

                        asm volatile ("isb\n\t");
                    }
                    float non_evict_rate = (float) num_fails / num_counts;

                    vprintf("evict: %f, non-evict: %f\n", evict_rate, non_evict_rate);
                    if (evict_rate < 0.9 || non_evict_rate > 0.1) {
                        satisfied = 0;
                        break;
                    }

                } // for (rep)
                terminate_child_access_addr_thread();
                if (satisfied) {
                    printf("Got perfect evset for victim %d\n", i);
                    break;
                }
                delete_eviction_set(l2_evset_victim[i]);
            }

            if (!satisfied) {
                continue;
            }

            hash_eviction_set(l2_evset_victim[i], hash, hash);
        } // for (NUM_TRIPWIRES)


        for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
            shuffle_cache_line_set(l2_congruent_cache_lines_x_copy);
            l2_evset_x = create_eviction_set(l2_congruent_cache_lines_x_copy);

            create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);
            satisfied = 1;
            vprintf("----- find x shuffle %d  ---\n", shuffle);
            for (int rep = 0; rep < 6; rep++) {
                flush_cache();

                set_child_access_addr(victim_addr_x);
                wakeup_child_access_addr_and_wait();
                num_fails = 0;
                for (int i = 0; i < num_counts; i++) {
                    asm volatile ("isb\n\t");

                    // this is important. It evicts all elements from the L2 set
                    // otherwise the pivot_addr may have same priority as (maybe even lower than)
                    // victim_addr loaded in the last iteration,
                    // causing pivot_addr to get evicted after PrimeP1S1(pivot_addr, l2_evset)
                    dual_traverse_roundtrip(assisted_l2_evset_x, 2);

                    PrimeP1S1(pivot_addr_x, l2_evset_x);
                    for (int n = 0; n < NUM_TRIPWIRES; n++)
                        PrimeP1S1_Load(pivot_addrs_victim[n], l2_evset_victim[n]);

                    /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                    DELAY_MACRO(DELAY1);

                    asm volatile ("isb\n\t");
                    wakeup_child_access_addr_and_wait();
                    asm volatile ("isb\n\t");

                    /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                    DELAY_MACRO(DELAY1);

                    for (int n = 0; n < NUM_TRIPWIRES; n++)
                        dual_traverse_roundtrip(l2_evset_victim[n], 2);


                    /*uint64_t* _pivot_addr_a = (uint64_t*)((*start) ^ hash0);*/
                    /*uint64_t* _pivot_addr_b = (uint64_t*)((*_pivot_addr_a) ^ hash1);*/
                    /*uint64_t* _pivot_addr_c = (uint64_t*)((*_pivot_addr_b) ^ hash2);;*/
                    /*uint64_t* _pivot_addr_d = (uint64_t*)((*_pivot_addr_c) ^ hash3);*/
                    /*uint64_t* _pivot_addr_x = (uint64_t*)((*_pivot_addr_d) ^ hash4);*/
                    uint64_t* _pivot_addr = POINTER_CHASING_GET_X(start);

                    int fail = ProbeY1(_pivot_addr);
                    num_fails += fail;

                    asm volatile ("isb\n\t");
                }
                float evict_rate = (float)num_fails / num_counts;

                set_child_access_addr(victim_addr_x_diff_l2);
                wakeup_child_access_addr_and_wait();
                num_fails = 0;
                for (int i = 0; i < num_counts; i++) {
                    asm volatile ("isb\n\t");

                    // this is important. It evicts all elements from the L2 set
                    // otherwise the pivot_addr may have same priority as (maybe even lower than)
                    // victim_addr loaded in the last iteration,
                    // causing pivot_addr to get evicted after PrimeP1S1(pivot_addr, l2_evset)
                    dual_traverse_roundtrip(assisted_l2_evset_x, 2);

                    PrimeP1S1(pivot_addr_x, l2_evset_x);
                    for (int n = 0; n < NUM_TRIPWIRES; n++)
                        PrimeP1S1_Load(pivot_addrs_victim[n], l2_evset_victim[n]);

                    /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                    DELAY_MACRO(DELAY1);

                    asm volatile ("isb\n\t");
                    wakeup_child_access_addr_and_wait();
                    asm volatile ("isb\n\t");

                    /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                    DELAY_MACRO(DELAY1);

                    for (int n = 0; n < NUM_TRIPWIRES; n++)
                        dual_traverse_roundtrip(l2_evset_victim[n], 2);


                    /*uint64_t* _pivot_addr_a = (uint64_t*)((*start) ^ hash0);*/
                    /*uint64_t* _pivot_addr_b = (uint64_t*)((*_pivot_addr_a) ^ hash1);*/
                    /*uint64_t* _pivot_addr_c = (uint64_t*)((*_pivot_addr_b) ^ hash2);*/
                    /*uint64_t* _pivot_addr_d = (uint64_t*)((*_pivot_addr_c) ^ hash3);*/
                    /*uint64_t* _pivot_addr_x = (uint64_t*)((*_pivot_addr_d) ^ hash4);*/
                    uint64_t* _pivot_addr = POINTER_CHASING_GET_X(start);

                    int fail = ProbeY1(_pivot_addr);
                    num_fails += fail;

                    asm volatile ("isb\n\t");
                }
                float non_evict_rate = (float) num_fails / num_counts;

                vprintf("evict: %f, non-evict: %f\n", evict_rate, non_evict_rate);
                if (evict_rate < 0.9 || non_evict_rate > 0.1) {
                    satisfied = 0;
                    break;
                }

            } // for (rep)
            terminate_child_access_addr_thread();
            if (satisfied) {
                printf("Got perfect eviction set for x\n");
                break;
            }
            delete_eviction_set(l2_evset_x);
        }

        if (!satisfied) {
            continue;
        }

        search_done = 1;

        hash_eviction_set(l2_evset_x, hash, hash);
    } // while (!search_done)

    /*return 0;*/
    ////////////// SEARCH DONE /////////////////


    uint64_t stamps0[NUM_REPS * num_counts];
    uint64_t stamps1[NUM_REPS * num_counts];
    uint64_t stamps2[NUM_REPS * num_counts];
    uint64_t stamps3[NUM_REPS * num_counts];
    int idx = 0;

    float fail_rate_no =  0.0;
    create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);
    set_child_access_addr(victim_addr_x);
    wakeup_child_access_addr_and_wait();
    for (int rep = 0; rep < NUM_REPS; rep++) {
        flush_cache();
        int num_fails = 0;
        for (int i = 0; i < num_counts; i++) {
            /*set_child_access_addr(victim_addr_c);*/
            asm volatile ("isb\n\t");

            // this is important. It evicts all elements from the L2 set
            // otherwise the pivot_addr_c may have same priority as (maybe even lower than)
            // victim_addr_c loaded in the last iteration,
            // causing pivot_addr_c to get evicted after PrimeP1S1(pivot_addr_c, l2_evset_c)
            dual_traverse_roundtrip(assisted_l2_evset_x, 2);

            PrimeP1S1(     pivot_addr_x, l2_evset_x);
            for (int n = 0; n < NUM_TRIPWIRES; n++)
                PrimeP1S1_Load(pivot_addrs_victim[n], l2_evset_victim[n]);

            asm volatile ("isb\n\t");

            /*for (volatile int k = 0; k < DELAY1; k++) {}*/
            DELAY_MACRO(DELAY1);

            /*for (volatile int k = 0; k < DELAY1; k++) {}*/
            DELAY_MACRO(DELAY1);

            for (int n = 0; n < NUM_TRIPWIRES; n++)
                dual_traverse_roundtrip(l2_evset_victim[n], 2);

            asm volatile ("isb\n\t");

#ifdef MEASURE_No
            uint64_t ts0 = 0;
            asm volatile (
                    "isb\n\t"
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts0)
                    : : "memory");
#endif

            /*for (volatile int i = 0; i < DELAY2; i++) {}*/
            DELAY_MACRO(DELAY2);

            /*uint64_t* _pivot_addr_a = (uint64_t*)((*start) ^ hash0);*/
            /*uint64_t* _pivot_addr_b = (uint64_t*)((*_pivot_addr_a) ^ hash1);*/
            /*uint64_t* _pivot_addr_c = (uint64_t*)((*_pivot_addr_b) ^ hash2);*/
            /*uint64_t* _pivot_addr_d = (uint64_t*)((*_pivot_addr_c) ^ hash3);*/
            /*uint64_t* _pivot_addr_x = (uint64_t*)((*_pivot_addr_d) ^ hash4);*/
            uint64_t* _pivot_addr = POINTER_CHASING_GET_X(start);

            uint8_t load_val = 0;
            int fail = 0;

            asm volatile (
                "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                : [fail] "+r" (fail), [val] "+r" (load_val) // prevent fail and val from
                                                            // getting mapped to the same reg
                : [addr] "r" (_pivot_addr)
                : "memory"); // if pointer chasing has no miss, it takes ~120 cycles to get here

#ifdef ADD_DELAY
            DELAY_MACRO(DIFF);
#endif

            uint8_t zero = *victim_addr_x; // miss, needs 1000 cycles
            junk ^= zero;

#ifdef MEASURE_No
            uint64_t ts1;
            asm volatile (
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts1));
            ts1 = ts1 + zero;

            uint64_t ts2 = 0;
            asm volatile (
                    "isb\n\t"
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts2)
                    : : "memory");

            stamps0[idx] = ts0;
            stamps1[idx] = ts1;
            stamps2[idx] = ts2;
            idx++;
#endif

            num_fails += fail;
            /*wait_child_access_addr();*/

            asm volatile ("isb\n\t");
        }
        float fail_rate = (float) num_fails / num_counts;
        vprintf("rep %d: evict none, pointer chasing x race x: fail rate = %f (expect 0\%)\n", rep, fail_rate);
        fail_rate_no += fail_rate;
    }
    fail_rate_no /= NUM_REPS;
    terminate_child_access_addr_thread();

#ifdef MEASURE_No
    printf("!!%d, %d\n", idx, child_idx);
    for (int i = 0; i < 20 * num_counts; i++) {
        printf("start: 0x%lx, reference load %ld, all end %ld\n", 
                stamps0[i], 
                stamps1[i] - stamps0[i], 
                stamps2[i] - stamps0[i]);
    }
#endif

    float test(uint8_t* victim_addr) {
        idx = 0; child_idx = 0;

        float avg_fail_rate = 0.0;
        create_child_thread(CHILD_ACCESS_ADDR, P_CORE_2_ID);
        set_child_access_addr(victim_addr);
        wakeup_child_access_addr_and_wait();
        for (int rep = 0; rep < NUM_REPS; rep++) {
            flush_cache();
            int num_fails = 0;
            for (int i = 0; i < num_counts; i++) {
                asm volatile ("isb\n\t");
                // this is important. It evicts all elements from the L2 set
                // otherwise the pivot_addr may have same priority as (maybe even lower than)
                // victim_addr loaded in the last iteration,
                // causing pivot_addr to get evicted after PrimeP1S1(pivot_addr, l2_evset)
                dual_traverse_roundtrip(assisted_l2_evset_x, 2);

                PrimeP1S1(     pivot_addr_x, l2_evset_x);
                /*PrimeP1S1_Load(pivot_addr_a, l2_evset_a);*/
                /*PrimeP1S1_Load(pivot_addr_b, l2_evset_b);*/
                /*PrimeP1S1_Load(pivot_addr_c, l2_evset_c);*/
                /*PrimeP1S1_Load(pivot_addr_d, l2_evset_d);*/
                for (int n = 0; n < NUM_TRIPWIRES; n++)
                    PrimeP1S1_Load(pivot_addrs_victim[n], l2_evset_victim[n]);

                // seems that this delay is necessary for separating traversal and
                // access to victim_addr
                /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                DELAY_MACRO(DELAY1);

                asm volatile ("isb\n\t");
                wakeup_child_access_addr_and_wait(); // victim thread accesses victim_addr,
                                                 // which evict pivot_addr_a
                asm volatile ("isb\n\t");

                // seems that this delay is necessary for access to victim_addr
                // to take effect
                /*for (volatile int k = 0; k < DELAY1; k++) {}*/
                DELAY_MACRO(DELAY1);

                /*dual_traverse_roundtrip(l2_evset_a, 2);*/
                /*dual_traverse_roundtrip(l2_evset_b, 2);*/
                /*dual_traverse_roundtrip(l2_evset_c, 2);*/
                /*dual_traverse_roundtrip(l2_evset_d, 2);*/
                for (int n = 0; n < NUM_TRIPWIRES; n++)
                    dual_traverse_roundtrip(l2_evset_victim[n], 2);

                asm volatile ("isb\n\t");
                /*set_child_access_addr(victim_addr_c);*/

#ifdef MEASURE
                uint64_t ts0;
                asm volatile (
                    "isb\n\t"
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts0)
                    : : "memory");
#endif

                /*for (volatile int i = 0; i < DELAY2; i++) {}*/
                DELAY_MACRO(DELAY2);

                /*uint64_t* _pivot_addr_a = (uint64_t*)((*start) ^ hash0);*/
                /*uint64_t* _pivot_addr_b = (uint64_t*)((*_pivot_addr_a) ^ hash1);*/
                /*uint64_t* _pivot_addr_c = (uint64_t*)((*_pivot_addr_b) ^ hash2);*/
                /*uint64_t* _pivot_addr_d = (uint64_t*)((*_pivot_addr_c) ^ hash3);*/
                /*uint64_t* _pivot_addr_x = (uint64_t*)((*_pivot_addr_d) ^ hash4);*/
                uint64_t* _pivot_addr = POINTER_CHASING_GET_X(start);

                /*int fail = ProbeY1(_pivot_addr_c);*/
                uint8_t load_val = 0;
                int fail = 0;

                asm volatile (
                    "stxrb %w[fail], %w[val], [%[addr]]\n\t"
                    : [fail] "+r" (fail), [val] "+r" (load_val) // prevent fail and val from
                                                                // getting mapped to the same reg
                    : [addr] "r" (_pivot_addr)
                    : "memory");

#ifdef ADD_DELAY
                DELAY_MACRO(DIFF);
#endif

                uint8_t zero = *victim_addr_x;
                junk ^= zero;

#ifdef MEASURE
                uint64_t ts1;
                asm volatile (
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts1));
                ts1 = ts1 + zero;

                uint64_t ts2;
                asm volatile (
                    "isb\n\t"
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts2)
                    : : "memory");

                stamps0[idx] = ts0;
                stamps1[idx] = ts1;
                stamps2[idx] = ts2;
                idx++;
#endif

                num_fails += fail;

                asm volatile ("isb\n\t");
            }
            float fail_rate = (float) num_fails / num_counts;
            vprintf("rep %d: evict a, pointer chasing x race x, fail rate = %f (expect 100\%)\n", rep, fail_rate);
            avg_fail_rate += fail_rate;
        }
        avg_fail_rate /= NUM_REPS;
        terminate_child_access_addr_thread();

#ifdef MEASURE
        printf("!!%d, %d\n", idx, child_idx);
        for (int i = 0; i < 20 * num_counts; i++) {
            printf("start: 0x%lx, reference load: %ld, all end %ld\n", 
                stamps0[i], 
                stamps1[i] - stamps0[i], 
                stamps2[i] - stamps0[i]);
        }
#endif

        return avg_fail_rate;
    }

    /*float fail_rate_a = test(victim_addr_a);*/
    /*float fail_rate_b = test(victim_addr_b);*/
    /*float fail_rate_c = test(victim_addr_c);*/
    /*float fail_rate_d = test(victim_addr_d);*/
    float fail_rate[NUM_TRIPWIRES];
    for (int i = 0; i < NUM_TRIPWIRES; i++) {
        fail_rate[i] = test(victim_addrs[i]);
    }
    float fail_rate_y = test(victim_addr_y);

    printf("global_junk = %u\n", global_junk);

    for (int i = 0; i < NUM_TRIPWIRES; i++)
        printf("access victim_addrs[%d]: %f\n", i, fail_rate[i]);
    printf("access NONE: %f, access Y: %f\n",
            fail_rate_no, fail_rate_y);


    return 0;
}
