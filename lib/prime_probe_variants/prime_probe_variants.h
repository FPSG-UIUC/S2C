#ifndef __PRIME_PROBE_VARIANTS_H__
#define __PRIME_PROBE_VARIANTS_H__

#include <stdint.h>
#include <sys/types.h>

#include "../basics/cache_line_set.h"
#include "../eviction_set/eviction_set.h"

#define RET_L1_EVRATE 1
#define RET_L2_EVRATE 2

void PrimeY1(uint8_t* victim_addr);

int ProbeY1(uint8_t* victim_addr);

void PrimeP1S1(uint8_t* pivot_addr, eviction_set_t* eviction_set);

int ProbeS1P1(uint8_t* pivot_addr, eviction_set_t* eviction_set);

/*** For multiple tripwires test ***/
void PrimeP1_Load(uint64_t* pivot_addr);

void PrimeP1S1_Load(uint64_t* pivot_addr, eviction_set_t* eviction_set);

uint64_t ProbeP1_Load(uint64_t* pivot_addr);

/*** General testing P+P routines ***/
float P1S1P1_timer(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);

float P1S1P1_timer_naive_traverse(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);

float P1S1P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);

float P1S1P1_llsc_naive_traverse(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);

float P1S2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);

float P1S2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines);


/*** Same-thread P+P attack ***/
float P1S1Y1P1_timer_naive_traverse(int evrate_to_ret, uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S1Y1P1_llsc_naive_traverse(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);


/*** Cross-thread P+P attack variants used for "L2 is 2 L1" ***/
float P1S1Y2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S1Y2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S1Y2S1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S1Y2S1P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);


/*** Cross-thread P+P attack variants used for "diff thread" ***/
float P1S2Y3P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S2Y3P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S2Y3S2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);

float P1S2Y3S2P1_llsc(uint8_t* pivot_addr, cache_line_set_t* evset_cache_lines, uint8_t* victim_addr);


/*** Cross-thread P+P attack variants used for PrimeScope inspired strategy ***/
void P1Sb1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_b_cache_lines, float* l1_evict_rate, float* l2_evict_rate);

void P1Sa1P1Sb1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, float* l1_evict_rate, float* l2_evict_rate);

void P1Sa1P1Sb1Y1P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate);

void P1Sa1P1Sb1Y2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate);

void P1Sa1P1Sb1Sc2P1_timer(uint8_t* pivot_addr, cache_line_set_t* evset_part_a_cache_lines, cache_line_set_t* evset_part_b_cache_lines, cache_line_set_t* evset_part_c_cache_lines, uint8_t* victim_addr, float* l1_evict_rate, float* l2_evict_rate);


#endif
