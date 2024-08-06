#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
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

int main(int argc, char** argv) {

    pin_p_core(P_CORE_1_ID);

    srand(time(NULL));

    // create victim address
    uint8_t* victim_page = (uint8_t*)mmap(NULL, 2*PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(victim_page, 0xff, 2*PAGE_SIZE);

    size_t page_offset = rand() % PAGE_SIZE;
    uint8_t* victim_addr = victim_page + page_offset;
    uint64_t victim_l1_line_offset = (uint64_t)victim_addr % L1_LINE_SIZE;
    uint64_t victim_l1_line_offset_diff_l1_half = (victim_l1_line_offset + L1_LINE_SIZE/2) % L1_LINE_SIZE;
    uint8_t* victim_addr_same_l1_line = victim_addr - victim_l1_line_offset + victim_l1_line_offset_diff_l1_half;
    uint8_t* victim_addr_diff_l1_line = victim_addr + L1_LINE_SIZE;
    uint8_t* victim_addr_diff_l2_line = victim_addr + L2_LINE_SIZE;
    uint8_t* victim_addr_diff_page_same_l1_line_offset = victim_addr + PAGE_SIZE;

    printf("victim_addr: %p\n", victim_addr);

    // Find a L1 eviction set for victim_addr
    cache_line_set_t* l1_evset_cache_lines = find_L1_congruent_cache_lines(victim_addr, L1_NWAYS);

    // Check if this is a good eviction set
    float P1S1P1_timer_l1_evict_rate = P1S1P1_timer(RET_L1_EVRATE, victim_addr, l1_evset_cache_lines);
    float P1S1P1_llsc_l1_evict_rate = P1S1P1_llsc(victim_addr, l1_evset_cache_lines);

    if (P1S1P1_timer_l1_evict_rate > 0.95 && P1S1P1_llsc_l1_evict_rate > 0.95)
        printf("found a good L1 eviction set.\n");
    else
        printf("Not a good L1 eviction set.\n");

    uint8_t* pivot_addr = (uint8_t*)pop_cache_line_from_set(l1_evset_cache_lines);

    // Step 3: Perform the P+P attack against different addresses
    // try attack different addresses
    printf("L1 P+P for victim_addr %p: L1 evict rate: timer: %f, llsc: %f (Should be 1)\n",
            victim_addr,
            P1S1Y1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_evset_cache_lines, victim_addr),
            P1S1Y1P1_llsc_naive_traverse(pivot_addr, l1_evset_cache_lines, victim_addr));

    printf("L1 P+P for victim_addr_same_l1_line %p: L1 evict rate: timer: %f, llsc: %f (Should be 1)\n",
            victim_addr_same_l1_line,
            P1S1Y1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_evset_cache_lines, victim_addr_same_l1_line),
            P1S1Y1P1_llsc_naive_traverse(pivot_addr, l1_evset_cache_lines, victim_addr_same_l1_line));

    printf("L1 P+P for victim_addr_diff_l1_line %p: L1 evict rate: timer: %f, llsc: %f (Should be 0)\n",
            victim_addr_diff_l1_line,
            P1S1Y1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_evset_cache_lines, victim_addr_diff_l1_line),
            P1S1Y1P1_llsc_naive_traverse(pivot_addr, l1_evset_cache_lines, victim_addr_diff_l1_line));

    printf("L1 P+P for victim_addr_diff_l2_line %p: L1 evict rate: timer: %f, llsc: %f (Should be 0)\n",
            victim_addr_diff_l2_line,
            P1S1Y1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_evset_cache_lines, victim_addr_diff_l2_line),
            P1S1Y1P1_llsc_naive_traverse(pivot_addr, l1_evset_cache_lines, victim_addr_diff_l2_line));

    printf("L1 P+P for victim_addr_diff_page_same_l1_line_offset %p: L1 evict rate: timer: %f, llsc: %f (Should be 1)\n",
            victim_addr_diff_page_same_l1_line_offset,
            P1S1Y1P1_timer_naive_traverse(RET_L1_EVRATE, pivot_addr, l1_evset_cache_lines, victim_addr_diff_page_same_l1_line_offset),
            P1S1Y1P1_llsc_naive_traverse(pivot_addr, l1_evset_cache_lines, victim_addr_diff_page_same_l1_line_offset));

    delete_cache_line_set(l1_evset_cache_lines);

    return 0;
}

