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

#define DELAY_MACRO(count)  asm volatile (      \
                    "mov x9, %[val]\n\t"        \
                    "L_delay%=:\n\t"            \
                    "sub x9, x9, #1\n\t"        \
                    "cbnz x9, L_delay%=\n\t"    \
                    :                           \
                    : [val] "r" (count)         \
                    : "x9")                     \


int l1_hit = 0, l2_hit = 0, l2_miss = 0;

uint64_t seed_fwd = 0xdeadbeefbeefdead;
uint64_t seed_bwd = 0xbeefdeaddeadbeef;

int main(int argc, char** argv) {

    srand(time(NULL));
    pin_p_core(P_CORE_1_ID);

    allocate_cache_flush_buffer();

    uint8_t* victim_page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(victim_page, 0xff, PAGE_SIZE);
    uint8_t* victim_addr = victim_page + rand() % PAGE_SIZE;

    num_counts = 10;

    cache_line_set_t* l1_congruent_cache_lines = find_L1_congruent_cache_lines(victim_addr, L1_NWAYS);
    eviction_set_t* l1_eviction_set = create_eviction_set(l1_congruent_cache_lines);

    cache_line_set_t* l2_congruent_cache_lines_1 = find_L2_eviction_set_using_timer(victim_addr);
    cache_line_set_t* l2_congruent_cache_lines_2 = find_L2_eviction_set_using_timer(victim_addr);

    cache_line_set_t* l2_congruent_cache_lines = merge_two_cache_line_sets(l2_congruent_cache_lines_1,
                                                                           l2_congruent_cache_lines_2);

    num_counts = 10;
    num_shuffles = 10;

    create_child_thread(CHILD_NAIVELY_TRAVERSE_SET, P_CORE_2_ID);
    /*create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);*/

    for (int evset_size = 10; evset_size <= 20 /*2*L1_NWAYS*/; evset_size++) {

        l1_hit = 0; l2_hit = 0; l2_miss = 0;

        cache_line_set_t* l2_evset_cache_lines = reduce_cache_line_set(l2_congruent_cache_lines, evset_size);

        /*for (int i = 0; i < evset_size/2; i++)*/
        for (int i = evset_size/2; i < evset_size; i++)
            l2_evset_cache_lines->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines->cache_lines[i]);

        /*for (int i = 0; i < evset_size; i++)*/
            /*l2_evset_cache_lines->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines->cache_lines[i]);*/

        float* latencies = (float*)malloc(sizeof(float) * num_shuffles);

        for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
            shuffle_cache_line_set(l2_evset_cache_lines);
            eviction_set_t* l2_eviction_set = create_eviction_set(l2_evset_cache_lines);
            /*hash_eviction_set(l2_eviction_set, seed_fwd, seed_bwd);*/


            // create child thread, which is responsible for traversing eviction set
            /*create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);*/
            set_child_eviction_set(l2_eviction_set);
            set_child_traverse_rep_count(6);

            // warm up the instruction cache
            wakeup_child_traverse_set_and_wait();

            uint8_t load_val = 0;
            uint64_t latency = 0;
            uint64_t total_latency = 0;

            for (int i = 0; i < num_counts; i++) {
                asm volatile (
                    "isb\n\t" // TODO: this isb is unncessary
                    "dsb sy\n\t"
                    "ldrb %w[val], [%[addr]]\n\t"
                    "dsb sy\n\t"
                    "isb\n\t" // TODO: this isb is unnecessary
                    : [val] "=r" (load_val)
                    : [addr] "r" (victim_addr));

                /*asm volatile ("isb\n\t");*/
                /*single_traverse_roundtrip(l1_eviction_set, 1);*/

                // this delay is crucial, o/w we will get bogus measurement results
                /*for (volatile int k = 0; k < 1000; k++) {}*/
                /*asm volatile ("isb\n\t");*/
                DELAY_MACRO(2000);

                wakeup_child_traverse_set_and_wait();
                /*traverse_eviction_set(l2_eviction_set);*/
                /*traverse_eviction_set(l2_eviction_set);*/

                // this delay is crucial, o/w we will get bogus measurement results
                /*for (volatile int k = 0; k < 1000; k++) {}*/
                /*asm volatile ("isb\n\t");*/
                DELAY_MACRO(2000);

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
                    : [addr] "r" (victim_addr)
                    : "x9", "x10");

                l1_hit += (latency < L1_HIT_MAX_LATENCY);
                l2_hit += (latency > L1_HIT_MAX_LATENCY) && (latency < L2_MISS_MIN_LATENCY);
                l2_miss += (latency > L2_MISS_MIN_LATENCY);
                total_latency += latency;

                flush_cache();
            }

            delete_eviction_set(l2_eviction_set);
            latencies[shuffle] = (float)total_latency / num_counts;
        }
        printf("eviction set size: %d L1 hit: %d L2 hit: %d L2 miss: %d avg_latency: %f, std: %f\n", 
                evset_size, l1_hit, l2_hit, l2_miss, mean_f(latencies, num_shuffles), std_f(latencies, num_shuffles));
    }
    terminate_child_traverse_set_thread();

    printf("global_junk = %u\n", global_junk);

    return 0;
}
