#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <time.h>

#include "allocator.h"
#include "math_utils.h"
#include "arch.h"

#define _dprintf(...)

allocator_t* create_allocator(int offset, int stride) {
    allocator_t* new_allocator = (allocator_t*)malloc(sizeof(allocator_t));

    new_allocator->num_pages = 0;
    new_allocator->num_cache_lines = 0;

    new_allocator->offset = offset;
    new_allocator->stride = stride;
    new_allocator->mode = 0;
    new_allocator->base_addr = 0;
    new_allocator->ref_count = 0;

    return new_allocator;
}

void delete_allocator(allocator_t* allocator) {
    // free up the allocated pages
    for (int i = 0; i < allocator->num_pages; i++) {
        if (allocator->mode == ALLOC_HUGE_PAGES)
            munmap((void*)allocator->pages[i], HUGEPAGE_SIZE);
        else if (allocator->mode == ALLOC_REG_PAGES)
            munmap((void*)allocator->pages[i], PAGE_SIZE);
    }

    // free up the allocator
    free(allocator);
}

void allocate_page(allocator_t* allocator) {
    if (allocator->mode != ALLOC_REG_PAGES) {
        fprintf(stderr, "ERROR: allocator mode is %d, not %d (ALLOC_REG_PAGES)\n", allocator->mode, ALLOC_REG_PAGES);
        exit(1);
    }

    size_t page = (size_t)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
    assert (page);
    if (page % PAGE_SIZE != 0) {
        fprintf(stderr, "ERROR: allocated page = 0x%lx\n", page);
        exit(1);
    }
    if (__builtin_expect(allocator->num_pages == MAX_PAGES_IN_ALLOCATOR, 0)) {
        fprintf(stderr, "ERROR: allocator has max page number!\n");
        exit(1);
    }
    allocator->pages[allocator->num_pages] = page;
    allocator->num_pages++;

    memset((void*)page, 0x01, PAGE_SIZE);

    size_t cache_line_addr = page + allocator->offset;
    while (cache_line_addr < page + PAGE_SIZE) {
        if ( __builtin_expect(allocator->num_cache_lines == MAX_CACHELINES_IN_ALLOCATOR, 0) ) {
            fprintf(stderr, "ERROR: allocator has max cache lines!\n");
            exit(1);
        }
        allocator->cache_lines[allocator->num_cache_lines] = cache_line_addr;
        allocator->num_cache_lines++;
        cache_line_addr += allocator->stride;
    }

    // shuffle the cache lines
    shuffle(allocator->cache_lines, allocator->num_cache_lines, sizeof(size_t));
}

void allocate_hugepage(allocator_t* allocator) {

#ifdef __APPLE__

    fprintf(stderr, "ERROR: %s should not be used in MacOS!\n", __func__);
    exit(1);

#endif

#ifdef __linux__

    if (allocator->mode != ALLOC_HUGE_PAGES) {
        fprintf(stderr, "ERROR: allocator mode is %d, not %d (ALLOC_HUGE_PAGES)\n", allocator->mode, ALLOC_HUGE_PAGES);
        exit(1);
    }

    if (allocator->num_pages > 0) {
        fprintf(stderr, "ERROR: can only allocate 1 huge page to a allocator!\n");
        exit(1);
    }
    /*size_t hugepage_expected_addr = BASE_HUGEPAGE_VADDR; // + num_hugepages * HUGEPAGE_SIZE;*/
    /*size_t hugepage = (size_t)mmap((void*)hugepage_expected_addr, HUGEPAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, -1, 0);*/
    /*if (hugepage != hugepage_expected_addr) {*/
        /*printf("ERROR: cannot allocate huge page at 0x%lx\n", hugepage_expected_addr);*/
        /*exit(1);*/
    /*}*/
    size_t hugepage = (size_t)mmap(NULL, HUGEPAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON|MAP_HUGETLB, -1, 0);

    assert (hugepage);
    assert (hugepage % HUGEPAGE_SIZE == 0);

    if ( __builtin_expect(allocator->num_pages == MAX_PAGES_IN_ALLOCATOR, 0) ) {
        fprintf(stderr, "ERROR: allocator has max page number!\n");
        exit(1);
    }

    allocator->base_addr = hugepage;
    allocator->pages[allocator->num_pages] = hugepage;
    allocator->num_pages++;

    // copy-on-write
    memset((void*)hugepage, 0x01, HUGEPAGE_SIZE);

    size_t cache_line_addr = hugepage + allocator->offset;
    while (cache_line_addr < hugepage + HUGEPAGE_SIZE) {
        if ( __builtin_expect(allocator->num_cache_lines == MAX_CACHELINES_IN_ALLOCATOR, 0) ) {
            fprintf(stderr, "ERROR: allocator has max cache lines!\n");
            exit(1);
        }
        allocator->cache_lines[allocator->num_cache_lines] = cache_line_addr;
        allocator->num_cache_lines++;
        cache_line_addr += allocator->stride;
    }

    // shuffle the cache lines
    shuffle(allocator->cache_lines, allocator->num_cache_lines, sizeof(size_t));

#endif

}

void set_allocator_mode(allocator_t* allocator, int mode) {
    if (allocator->mode != 0) {
        fprintf(stderr, "ERROR: allocator mode is currently %d, cannot change mode.\n", allocator->mode);
        exit(1);
    }
    allocator->mode = mode;

    // notice that one allocator can only allocate one huge page
    // regular pages are allocated on demand
    if (allocator->mode == ALLOC_HUGE_PAGES)
        allocate_hugepage(allocator);
}

size_t pop_cache_line_from_allocator(allocator_t* allocator) {
    if (allocator->num_cache_lines == 0)
        /*allocate_hugepage(allocator);*/
        allocate_page(allocator);

    allocator->num_cache_lines--;
    return allocator->cache_lines[allocator->num_cache_lines];
}
