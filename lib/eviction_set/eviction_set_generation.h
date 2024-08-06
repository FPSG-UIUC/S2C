#ifndef __EVICTION_SET_GENERATION_H__
#define __EVICTION_SET_GENERATION_H__

#include <stdint.h>
#include "../basics/cache_line_set.h"

typedef struct {
    uint8_t* addr;
    cache_line_set_t* cache_lines;
    uint8_t* extra_addr;
} addr_cache_lines_pair_t;

// this function outputs a set of cache lines (with size = set_size) that map to the same L1 set as victim_addr
cache_line_set_t* find_L1_congruent_cache_lines(uint8_t* victim_addr, int set_size);

// this function outputs a set of cache lines that map to the same L2 set as victim_addr, using the classic algorithm
// the output set is a good eviction set for victim_addr, and the set size is exactly L2_NWAYS
cache_line_set_t* find_L2_eviction_set_using_timer(uint8_t* victim_addr);

// this function outputs a 32-element L2 eviction set for victim_addr, by using the pre-computed database and adjusting the
// congruent addresses for the victim address. The user can test the efficacy of this 32-element L2 eviction set,
// however it is not supposed to be used directly for the attack, because LL/SC only monitors one single address.
// As a result, the user must choose one address to be "pivot address", and another smaller set of eviction set addresses
// that won't evict pivot_addr from L2, but with one more victim_addr access, the pivot_addr will be evicted.
// Such pivot_addr and smaller eviction set should be computed by calling the function below.
cache_line_set_t* find_L2_congruent_cache_lines_using_llsc(uint8_t* writable_victim_addr);


// this function outputs the best L2 eviction set ordering with minimum cache lines
cache_line_set_t* find_L2_eviction_set_using_llsc(uint8_t* writable_victim_addr);

// this function outputs the best L2 eviction set ordering, with the chosen pivot_addr, that works best for victim_addr, 
// the algorithm is directly created based on expr10.
// Input:
//      victim_addr: victim address that we will try to produce the best eviction set ordering
// Output:
//      pivot_addr: address from l2_evset_cache_lines, that we will be using for LDREX/STREX
//      optimal l2_evset_cache_lines_reduced: a 11-element eviction set. This set will promise to not evict pivot_addr from L1/L2,
//                                            and one more access to victim_addr will evict pivot_addr from L2, hence from L1 too
addr_cache_lines_pair_t* find_L2_eviction_set_and_pivot_addr_using_llsc(uint8_t* readonly_victim_addr);



#endif
