#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

#include "arch.h"
#include "cache_utils.h"


int       cache_flush_buf_size = 0x2000000;
uint64_t* cache_flush_buf = NULL;
volatile  uint64_t flush_junk = 0;

void allocate_cache_flush_buffer() {
    cache_flush_buf = (uint64_t*)mmap((void*)cache_flush_buf, cache_flush_buf_size * sizeof(uint64_t), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (cache_flush_buf);

    memset(cache_flush_buf, rand() % 0xff, cache_flush_buf_size * sizeof(uint64_t));
}

void flush_cache() {
    assert (cache_flush_buf);

    for (int i = 0; i < cache_flush_buf_size; i += 4) {
        flush_junk ^= cache_flush_buf[i];
    }
}

void evict_addr_from_L1(void* addr) {
    assert (cache_flush_buf);

    uint64_t page_offset = (uint64_t) addr % PAGE_SIZE;
    uint64_t congruent_addr = (uint64_t)cache_flush_buf + page_offset;

    /*printf("victim_addr: %p\n", addr);*/
    for (int i = 0; i < L1_NWAYS+1; i++) {
        /*printf("congruent_addr: 0x%lx\n", congruent_addr);*/
        flush_junk ^= *(uint64_t*)congruent_addr;
        asm volatile ("isb");
        congruent_addr = congruent_addr + PAGE_SIZE;
    }
}

size_t flip_l2_offset(size_t addr) {
    size_t l2_offset = addr % L2_LINE_SIZE;
    size_t l2_offset_diff_l2_half = (l2_offset + L1_LINE_SIZE) % L2_LINE_SIZE;
    size_t addr_diff_l2_half = addr - l2_offset + l2_offset_diff_l2_half;

    return addr_diff_l2_half;
}
