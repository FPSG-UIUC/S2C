#ifndef __EVICTION_SET_H__
#define __EVICTION_SET_H__

#include "../basics/allocator.h"
#include "../basics/cache_line_set.h"
#include "../basics/linked_list.h"

#include <stdint.h>

#define NUM_TESTS 100

// the eviction set consists of a linked list of cache lines
// that can be traversed multiple times in an attempt to evict 
// certain cache lines
typedef struct {
    linked_list_t* list_of_cachelines;
    int hashed;
    uint64_t seed_fwd;
    uint64_t seed_bwd;
} eviction_set_t;

// Input:
//   [cache_line_set]: set of addresses of cache lines that form an eviction set
//   [allocator]: the allocator that provides the cache lines.
eviction_set_t* create_eviction_set(cache_line_set_t* cache_line_set);

void delete_eviction_set(eviction_set_t* eviction_set);

void print_eviction_set(eviction_set_t* eviction_set);

// Input:
//   [eviction_set]    : eviction set
//   [cache_line_addr] : address of a cache line to be added into the eviction set
void add_line_to_eviction_set(eviction_set_t* eviction_set, size_t cache_line_addr);

// Traverse the eviction set elements
uint8_t traverse_eviction_set(eviction_set_t* eviction_set);

// Traverse a small eviction set
uint8_t traverse_small_eviction_set(eviction_set_t* eviction_set);

// Traverse a large eviction set
uint8_t traverse_large_eviction_set(eviction_set_t* eviction_set);

// Traverse an eviction set in a naive way
uint8_t traverse_eviction_set_naive(eviction_set_t* eviction_set);

// TODO: move this function to somewhere else
// Test the selected cache lines as a potential eviction set for a victim address
//
// Procedure:
//  load victim_addr
//  call traverse_eviction_set to traverse the candidate eviction set (cache_line_set)
//  time load victim_addr
//
// Return the latency number
//
// Input:
//   [victim_addr]  : address of the victim data that we want to test the eviction set against
//   [cache_line_set] : set of cache lines that we use to craft potential eviction set
// Output:
//   Medium access latency of victim_addr after trying to evict it with cache_line_set
uint64_t evict_and_time(uint8_t* victim_addr, cache_line_set_t* cache_line_set);

// Same traverse functions used for experimental purposes
uint8_t single_traverse_fwd(eviction_set_t* eviction_set, int num_repeat);
uint8_t single_traverse_bwd(eviction_set_t* eviction_set, int num_repeat);
uint8_t single_traverse_roundtrip(eviction_set_t* eviction_set, int num_repeat);

uint8_t dual_traverse_fwd(eviction_set_t* eviction_set, int num_repeat);
uint8_t dual_traverse_bwd(eviction_set_t* eviction_set, int num_repeat);
uint8_t dual_traverse_roundtrip(eviction_set_t* eviction_set, int num_repeat);

// used when we want to eliminate the effect of DMP

void hash_eviction_set(eviction_set_t* eviction_set, uint64_t seed_fwd, uint64_t seed_bwd);

uint8_t single_traverse_fwd_dmpoff(eviction_set_t* eviction_set, int num_repeat);
uint8_t single_traverse_bwd_dmpoff(eviction_set_t* eviction_set, int num_repeat);
uint8_t single_traverse_roundtrip_dmpoff(eviction_set_t* eviction_set, int num_repeat);

uint8_t dual_traverse_fwd_dmpoff(eviction_set_t* eviction_set, int num_repeat);
uint8_t dual_traverse_bwd_dmpoff(eviction_set_t* eviction_set, int num_repeat);
uint8_t dual_traverse_roundtrip_dmpoff(eviction_set_t* eviction_set, int num_repeat);

#endif
