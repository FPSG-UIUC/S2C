#ifndef __ALLOCATOR_H__
#define __ALLOCATOR_H__

#include <stdlib.h>
#include <stdint.h>

#define MAX_PAGES_IN_ALLOCATOR      0x100000
#define MAX_CACHELINES_IN_ALLOCATOR 0x1000000

#define ALLOC_REG_PAGES     1
#define ALLOC_HUGE_PAGES    2

typedef struct {
    // allocated pages
    int64_t num_pages;
    size_t pages[MAX_PAGES_IN_ALLOCATOR];

    // base addresses of all cache_lines in allocated pages
    int64_t num_cache_lines;
    size_t cache_lines[MAX_CACHELINES_IN_ALLOCATOR];

    // page offset
    int offset;

    // stride (supposed to be page size) - distance between consecutive cache lines
    int stride;

    // base address of the 1st allocated page. Only useful for mode == ALLOC_HUGE_PAGES
    size_t base_addr;

    // ALLOC_REG_PAGES or ALLOC_HUGE_PAGES
    int mode;

    // reference counter for # cache_line_set_t objects use this allocator
    // allocator is automatically deleted when this counter decrements to 0
    int ref_count;

} allocator_t;

allocator_t* create_allocator(int offset, int stride);

void delete_allocator(allocator_t* allocator);

void set_allocator_mode(allocator_t* allocator, int mode);

size_t pop_cache_line_from_allocator(allocator_t* allocator);

void allocate_page(allocator_t* allocator);

void allocate_hugepage(allocator_t* allocator);

#endif
