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
#include <basics/sys_utils.h>
#include <basics/cache_line_set.h>
#include <eviction_set/eviction_set.h>
#include <eviction_set/eviction_set_generation.h>
#include <prime_probe_variants/prime_probe_variants.h>


int num_evsets_per_size = 100;


// Result:
//  when |S| <= 7, t = L1 access latency, fail = 0. S cannot evict X.
//  when |S| >= 8, t = L2 access latency, fail = 1. S can evict X.
int main(int argc, char** argv) {

    pin_p_core(P_CORE_1_ID);

    // create target address
    uint8_t* page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(page, 0xff, PAGE_SIZE);
    uint8_t* pivot_addr = page + rand() % PAGE_SIZE;
    /*printf("pivot_address:  @ %p\n", pivot_addr);*/

    for (int evset_size = 4; evset_size < 10; evset_size++) {
        for (int n = 0; n < num_evsets_per_size; n++) {
            cache_line_set_t* l1_congruent_cache_lines = find_L1_congruent_cache_lines(pivot_addr, evset_size);
            for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
                shuffle_cache_line_set(l1_congruent_cache_lines);

                // the naive_traverse version of P1S1P1_timer() and P1S1P1_llsc() make sure no accesses between prime and probe aside from eviction set traversal
                // also basic P1S1P1_timer() and P1S1P1_llsc() uses traverse_eviction_set() that assumes an eviction set size >= 8

                float P1S1P1_timer_naive_traverse_l1_evict_rate = P1S1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_congruent_cache_lines);

                float P1S1P1_llsc_naive_traverse_l1_evict_rate = P1S1P1_llsc_naive_traverse(pivot_addr, l1_congruent_cache_lines);

                // we make sure that when |S| < 7, l1_evict_rate < 5%, and when |S| >= 8, l1_evict_rate > 95%
                if (evset_size < 8 && (P1S1P1_timer_naive_traverse_l1_evict_rate > 0.05 || P1S1P1_llsc_naive_traverse_l1_evict_rate > 0.05)) {
                    printf("Bogus result:\nevset size %d, evset #%d, shuffle #%d: timer_naive_traverse %f, llsc_naive_traverse %f\n", evset_size, n, shuffle, P1S1P1_timer_naive_traverse_l1_evict_rate, P1S1P1_llsc_naive_traverse_l1_evict_rate);
                }
                else if (evset_size >= 8 && (P1S1P1_timer_naive_traverse_l1_evict_rate < 0.95 || P1S1P1_llsc_naive_traverse_l1_evict_rate < 0.95)) {
                    printf("Bogus result:\nevset size %d, evset #%d, shuffle #%d: timer_naive_traverse %f, llsc_naive_traverse %f\n", evset_size, n, shuffle, P1S1P1_timer_naive_traverse_l1_evict_rate, P1S1P1_llsc_naive_traverse_l1_evict_rate);
                }
            }

            delete_cache_line_set(l1_congruent_cache_lines);
        }
    }

    printf("If nothing is printed above, then we confirm that:\n");
    printf(" - when L1 eviction set size <= 7, no L1 eviction;\n");
    printf(" - when L1 eviction set size >= 8, always L1 eviction.\n");
    printf("L1 eviction set addresses are simply addresses with the same page offset as the randomly-generated victim address.\n");

    return 0;
}
