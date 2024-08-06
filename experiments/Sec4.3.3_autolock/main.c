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

    cache_line_set_t* l2_evset_cache_lines = find_L2_eviction_set_using_timer(victim_addr);
    eviction_set_t* l2_eviction_set_assisted = create_eviction_set(find_L2_eviction_set_using_timer(victim_addr));

    num_counts = 100;
    num_shuffles = 10;

    create_child_thread(CHILD_NAIVELY_TRAVERSE_SET, P_CORE_2_ID);
    /*create_child_thread(CHILD_TRAVERSE_SET, P_CORE_2_ID);*/


    for (int flip = 1; flip <= 6; flip++) {
        cache_line_set_t* l2_evset_cache_lines_copy = copy_cache_line_set(l2_evset_cache_lines);

        int l1_hit = 0, l2_hit = 0, l2_miss = 0;

        // #flip elements in l2_evset_cache_lines_copy will share the same L2 half as victim_addr
        // (12 - #flip) elements in l2_evset_cache_lines_copy will be located on the other L2 half
        for (int i = flip; i < l2_evset_cache_lines_copy->num_cache_lines; i++)
            l2_evset_cache_lines_copy->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines_copy->cache_lines[i]);

        for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
            shuffle_cache_line_set(l2_evset_cache_lines_copy);
            eviction_set_t* l2_eviction_set = create_eviction_set(l2_evset_cache_lines_copy);
            /*hash_eviction_set(l2_eviction_set, seed_fwd, seed_bwd);*/

            set_child_eviction_set(l2_eviction_set);
            set_child_traverse_rep_count(6);

            // warm up the instruction cache
            wakeup_child_traverse_set_and_wait();

            uint8_t load_val = 0;
            uint64_t latency = 0;
            uint64_t total_latency = 0;

            for (int i = 0; i < num_counts; i++) {
                dual_traverse_roundtrip(l2_eviction_set_assisted, 1);
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
        }
        printf("flip %d + not flip %d: L1 hit: %d L2 hit: %d L2 miss: %d \n", 
                flip, 12-flip, l1_hit, l2_hit, l2_miss);
    }
    terminate_child_traverse_set_thread();

    printf("global_junk = %u\n", global_junk);

    return 0;
}
