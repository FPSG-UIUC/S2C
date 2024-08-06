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



extern uint64_t traverse_duration;

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

    num_counts = 100;
    num_shuffles = 100;

    uint64_t total_duration[5][16] = {0};

    create_child_thread(CHILD_NAIVELY_TRAVERSE_SET, P_CORE_2_ID);

    for (int n = 1; n < 5; n++) {
        uint8_t** lines = (uint8_t**)malloc(sizeof(uint8_t*) * n);

        for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
            shuffle_cache_line_set(l2_congruent_cache_lines);
            cache_line_set_t* l2_congruent_cache_lines_copy = copy_cache_line_set(l2_congruent_cache_lines);
            for (int i = 0; i < n; i++)
                lines[i] = pop_cache_line_from_set(l2_congruent_cache_lines_copy);


            for (int evset_size = 6; evset_size < 16; evset_size++) {
                uint64_t duration = 0;
                cache_line_set_t* l2_evset_cache_lines = reduce_cache_line_set(l2_congruent_cache_lines_copy, evset_size);
                for (int i = 0; i < evset_size / 2; i++)
                    l2_evset_cache_lines->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines->cache_lines[i]);

                eviction_set_t* l2_eviction_set = create_eviction_set(l2_evset_cache_lines);

                set_child_eviction_set(l2_eviction_set);
                set_child_traverse_rep_count(1);

                wakeup_child_traverse_set_and_wait();

                uint8_t load_val = 0;

                for (int i = 0; i < num_counts; i++) {
                    for (int x = 0; x < n; x++) {
                        asm volatile (
                            "isb\n\t" // TODO: this isb is unncessary
                            "dsb sy\n\t"
                            "ldrb %w[val], [%[addr]]\n\t"
                            "dsb sy\n\t"
                            "isb\n\t" // TODO: this isb is unnecessary
                            : [val] "=r" (load_val)
                            : [addr] "r" (lines[x]));
                    }

                    DELAY_MACRO(2000);

                    wakeup_child_traverse_set_and_wait();

                    DELAY_MACRO(2000);

                    /*flush_cache();*/

                    duration += traverse_duration;
                }

                total_duration[n][evset_size] += duration;

                delete_eviction_set(l2_eviction_set);
            }

            delete_cache_line_set(l2_congruent_cache_lines_copy);
        }
    }

    terminate_child_traverse_set_thread();
    for (int n = 1; n < 5; n++) {
        for (int evset_size = 6; evset_size < 16; evset_size++) {
            printf("%d locked evset_size %d duration = %ld\n", n, evset_size, total_duration[n][evset_size] / num_counts / num_shuffles);
        }
    }

    printf("global_junk = %u\n", global_junk);

    return 0;
}
