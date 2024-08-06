#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "eviction_set.h"

#include "../basics/arch.h"
#include "../basics/allocator.h"
#include "../basics/params.h"
#include "../basics/cache_line_set.h"
#include "../basics/linked_list.h"
#include "../basics/math_utils.h"


eviction_set_t* create_eviction_set(cache_line_set_t* cache_line_set) {
    eviction_set_t* new_eviction_set = (eviction_set_t*)malloc(sizeof(eviction_set_t));
    new_eviction_set->list_of_cachelines = create_linked_list();
    new_eviction_set->hashed = 0;
    new_eviction_set->seed_fwd = 0;
    new_eviction_set->seed_bwd = 0;

    // when we create an eviction set with a list of congruent addresses, each eviction set element
    // is a linked_list_t node. Each node is 0x10 (16) byte, containing one forward pointer and backward pointer
    // so we want each node to be L1 cache line aligned

    for (int i = 0; i < cache_line_set->num_cache_lines; i++)  {
        /*size_t l1_line_aligned_addr = cache_line_set->cache_lines[i] - */
                                      /*(cache_line_set->cache_lines[i] % L1_LINE_SIZE);*/
        size_t l1_line_aligned_addr = cache_line_set->cache_lines[i] -
                                      (cache_line_set->cache_lines[i] % sizeof(node_t));
        /*size_t l1_line_aligned_addr = cache_line_set->cache_lines[i];*/
        add_preallocated_node_to_linked_list(new_eviction_set->list_of_cachelines, (node_t*)l1_line_aligned_addr);
    }

    return new_eviction_set;
}

void delete_eviction_set(eviction_set_t *eviction_set) {
    delete_linked_list(eviction_set->list_of_cachelines);
    free(eviction_set);
}

void print_eviction_set(eviction_set_t* eviction_set) {
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;
    printf("# cache lines: %d\n", num_nodes);
    if (num_nodes == 0)
        return;

    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    printf("head: %p, tail: %p\n", head, tail);
    node_t* node = head;
    int i = 0;
    while (1) {
        printf("%d: addr: %p (%p, %p), next: %p, last: %p\n", 
                i, node, &(node->next), &(node->last), node->next, node->last);
        if (node == tail)
            break;
        node = node->next;
        i = i + 1;
    }
}

uint8_t traverse_eviction_set_naive(eviction_set_t* eviction_set) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    volatile node_t* iter = 0;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;

    for (int i = 0; i < 2; i++) {
        // Perform a forward traversal over linked list
        // iterate over eviction set using iter and lagging iter
        iter = (uint64_t)iter + head;
        asm volatile (
            "L_fwd_traverse_naive%=:\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "isb\n\t"
            "CBNZ %[iter], L_fwd_traverse_naive%=\n\t"
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");
        asm volatile ("dsb sy\n\t");

        // Perform a backward traversal over linked list
        iter = (uint64_t)iter + tail;

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_traverse_naive%=:\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "isb\n\t"
            "CBNZ %[iter], L_bwd_traverse_naive%="
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");
        asm volatile ("dsb sy\n\t");
    }

    return 0;
}

uint8_t traverse_small_eviction_set(eviction_set_t* eviction_set) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    volatile node_t* iter;
    volatile node_t* lagging_iter;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < 2; i++) {
        // Perform a forward traversal over linked list
        iter = head;
        lagging_iter = head;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 4 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            : [iter] "+r" (iter)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_small%=\n\t" // if iter reaches the end
            "isb\n\t" // need to make sure the memory ordering is right
            /*"dsb sy\n\t"*/ // dsb is useless here
            "LDR %[iter], [%[iter]]\n\t"
            "L_skip_iter_in_fwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_small%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/


        // Perform a backward traversal over linked list
        iter = tail;
        lagging_iter = tail;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 4 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            : [iter] "+r" (iter)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd_small%=\n\t" // if iter reaches the end
            "isb\n\t" // need to make sure the memory ordering is right
            /*"dsb sy\n\t"*/ // dsb is useless here
            "LDR %[iter], [%[iter], #8]\n\t"
            "L_skip_iter_in_bwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse_small%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/
    }

    return 0;
}

uint8_t traverse_large_eviction_set(eviction_set_t* eviction_set) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    volatile node_t* iter;
    volatile node_t* lagging_iter;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < 2; i++) {
        // Perform a forward traversal over linked list
        iter = head;
        lagging_iter = head;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 12 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            : [iter] "+r" (iter)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_large%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_large%=\n\t" // if iter reaches the end
            "isb\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "L_skip_iter_in_fwd_large%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_large%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");

        // Perform a backward traversal over linked list
        iter = tail;
        lagging_iter = tail;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 12 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            : [iter] "+r" (iter)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_traverse_large%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd_large%=\n\t" // if iter reaches the end
            "isb\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "L_skip_iter_in_bwd_large%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse_large%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        /*asm volatile ("isb\n\t");*/
        asm volatile ("dsb sy\n\t");
    }

    return 0;
}

uint8_t traverse_eviction_set(eviction_set_t *eviction_set) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    volatile node_t* iter;
    volatile node_t* lagging_iter;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < 2; i++) {
        // Perform a forward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = head;
        lagging_iter = head;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 8 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"    // 1st
            "LDR %[iter], [%[iter]]\n\t"    // 2nd
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"    // 8th
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t"); // this isb seems to be critical
        /*asm volatile ("dsb sy\n\t");*/

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd%=\n\t" // if iter reaches the end
            "isb\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "L_skip_iter_in_fwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // Perform a backward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = tail;
        lagging_iter = tail;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 8 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t" // 1st
            "LDR %[iter], [%[iter], #8]\n\t" // 2nd
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 8th
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t"); // this isb seems to be critical
        /*asm volatile ("dsb sy\n\t");*/

        // iterater over eviction set with iter and lagging_iter
        asm volatile (
            "L_bwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd%=\n\t" // if iter reaches the end
            "isb\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "L_skip_iter_in_bwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/
    }

    return 0;
}

static inline uint64_t evict_and_time_once(uint8_t* victim_addr, eviction_set_t* eviction_set) {
    /*eviction_set_traversed_by_child = eviction_set;*/
    // preload the victim_addr
    uint64_t latency;
    volatile uint8_t junk = 0;
    asm volatile (
            "dsb sy\n\t"
            "ldrb %w[out], [%[addr]]\n\t"
            "dsb sy\n\t"
            : [out] "=r" (junk)
            : [addr] "r" (victim_addr));

    asm volatile ("dsb sy\n\t");
    junk ^= traverse_eviction_set(eviction_set);
    asm volatile ("dsb sy\n\t");

    asm volatile (
            "dsb sy\n\t"
            "isb\n\t"
            "mrs x9, S3_2_c15_c0_0\n\t"
            "ldrb %w[out], [%[addr]]\n\t"
            "isb\n\t"
            "mrs x10, S3_2_c15_c0_0\n\t"
            "sub %[latency], x10, x9\n\t"
            : [latency]"=r"(latency), [out]"=r"(junk)
            : [addr]"r"(victim_addr)
            : "x9", "x10");

    global_junk ^= junk;
    return latency;
}

uint64_t evict_and_time(uint8_t* victim_addr, cache_line_set_t* cache_line_set) {

    uint64_t timings[NUM_TESTS];

    for (int i = 0; i < NUM_TESTS; i++) {
        // Randomize the set of cache lines
        shuffle(cache_line_set->cache_lines, cache_line_set->num_cache_lines, sizeof(size_t));

        // Construct an eviction set. allocator field is ignored
        eviction_set_t* eviction_set = create_eviction_set(cache_line_set);
        /*print_eviction_set(eviction_set);*/

        // Evict and time the victim access
        timings[i] = evict_and_time_once(victim_addr, eviction_set);

        delete_eviction_set(eviction_set);
    }

    sort(timings, NUM_TESTS, sizeof(uint64_t));

    return timings[NUM_TESTS / 2];
}

void hash_eviction_set(eviction_set_t* eviction_set, uint64_t seed_fwd, uint64_t seed_bwd) {
    eviction_set->hashed = 1;
    eviction_set->seed_fwd = seed_fwd;
    eviction_set->seed_bwd = seed_bwd;
    node_t* this = eviction_set->list_of_cachelines->head;
    for (int i = 0; i < eviction_set->list_of_cachelines->num_nodes; i++) {
        node_t* next = this->next;
        this->next = (node_t*)((uint64_t)this->next ^ seed_fwd);
        this->last = (node_t*)((uint64_t)this->last ^ seed_bwd);
        this = next;
    }
}

uint8_t single_traverse_roundtrip(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return single_traverse_roundtrip_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter = 0;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // iterate over eviction set using iter and lagging iter
        iter = head;
        asm volatile (
            "L_fwd_naive%=:\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "CBNZ %[iter], L_fwd_naive%=\n\t"
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");

        // Perform a backward traversal over linked list
        iter = tail;

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_naive%=:\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "CBNZ %[iter], L_bwd_naive%="
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t single_traverse_roundtrip_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter = 0;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    uint64_t seed_fwd = eviction_set->seed_fwd;
    uint64_t seed_bwd = eviction_set->seed_bwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // iterate over eviction set using iter and lagging iter
        iter = head;
        asm volatile (
            "L_fwd_naive%=:\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "CBNZ %[iter], L_fwd_naive%=\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");

        // Perform a backward traversal over linked list
        iter = tail;

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_naive%=:\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "CBNZ %[iter], L_bwd_naive%="
            : [iter] "+r" (iter)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t single_traverse_fwd(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return single_traverse_fwd_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter = 0;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // iterate over eviction set using iter and lagging iter
        iter = head;
        asm volatile ("isb\n\t");
        asm volatile (
            "L_fwd_naive%=:\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "CBNZ %[iter], L_fwd_naive%=\n\t"
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t single_traverse_fwd_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter = 0;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    uint64_t seed_fwd = eviction_set->seed_fwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // iterate over eviction set using iter and lagging iter
        iter = head;
        asm volatile (
            "L_fwd_naive%=:\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "CBNZ %[iter], L_fwd_naive%=\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t single_traverse_bwd(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return single_traverse_bwd_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a backward traversal over linked list
        iter = tail;

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_naive%=:\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "CBNZ %[iter], L_bwd_naive%="
            : [iter] "+r" (iter)
            : : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t single_traverse_bwd_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    uint64_t seed_bwd = eviction_set->seed_bwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a backward traversal over linked list
        iter = tail;

        // iterater over eviction set with iter and lagging iter
        asm volatile (
            "L_bwd_naive%=:\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "CBNZ %[iter], L_bwd_naive%="
            : [iter] "+r" (iter)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}

uint8_t dual_traverse_roundtrip(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return dual_traverse_roundtrip_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter = 0;
    volatile node_t* lagging_iter = 0;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // lagging behind n steps to perform dual-chasing
        iter = head;
        lagging_iter = head;
        /*iter = head + (uint64_t)iter;*/
        /*lagging_iter = head + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 4 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            : [iter] "+r" (iter)
            :
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_small%=\n\t" // if iter reaches the end
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter]]\n\t"
            "L_skip_iter_in_fwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_small%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            :
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // Perform a backward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = tail;
        lagging_iter = tail;
        /*iter = tail + (uint64_t)iter;*/
        /*lagging_iter = tail + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 8 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t" // 1st
            "LDR %[iter], [%[iter], #8]\n\t" // 2nd
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 8th
            : [iter] "+r" (iter)
            :
            : );

        asm volatile ("isb\n\t");  // this isb seems to be critical
        /*asm volatile ("dsb sy\n\t");*/

        // iterater over eviction set with iter and lagging_iter
        asm volatile (
            "L_bwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd%=\n\t" // if iter reaches the end
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter], #8]\n\t"
            "L_skip_iter_in_bwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            :
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t dual_traverse_roundtrip_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter;
    volatile node_t* lagging_iter;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    uint64_t seed_fwd = eviction_set->seed_fwd;
    uint64_t seed_bwd = eviction_set->seed_bwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // lagging behind n steps to perform dual-chasing
        iter = head;
        lagging_iter = head;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 4 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "EOR %[lagging_iter], %[lagging_iter], %[seed]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_small%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "L_skip_iter_in_fwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_small%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // Perform a backward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = tail;
        lagging_iter = tail;
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 8 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t" // 1st
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 2nd
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 8th
            "EOR %[iter], %[iter], %[seed]\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("isb\n\t");  // this isb seems to be critical
        /*asm volatile ("dsb sy\n\t");*/

        // iterater over eviction set with iter and lagging_iter
        asm volatile (
            "L_bwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "EOR %[lagging_iter], %[lagging_iter], %[seed]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "L_skip_iter_in_bwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("dsb sy\n\t");

    }

    return 0;
}
uint8_t dual_traverse_fwd(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return dual_traverse_fwd_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter = 0;
    volatile node_t* lagging_iter = 0;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // lagging behind n steps to perform dual-chasing
        iter = head;
        lagging_iter = head;
        /*iter = head + (uint64_t)iter;*/
        /*lagging_iter = head + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 4 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            : [iter] "+r" (iter)
            :
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_small%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter]]\n\t"
            "L_skip_iter_in_fwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_small%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            :
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/
    }

    return 0;
}
uint8_t dual_traverse_fwd_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter = 0;
    volatile node_t* lagging_iter = 0;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;
    uint64_t seed_fwd = eviction_set->seed_fwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a forward traversal over linked list
        // lagging behind n steps to perform dual-chasing
        iter = head;
        lagging_iter = head;
        /*iter = head + (uint64_t)iter;*/
        /*lagging_iter = head + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->next" 4 times
        asm volatile (
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/

        // iterate over eviction set using iter and lagging iter
        asm volatile (
            "L_fwd_traverse_small%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter]]\n\t"
            "EOR %[lagging_iter], %[lagging_iter], %[seed]\n\t"
            "CBZ %[iter], L_skip_iter_in_fwd_small%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter]]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "L_skip_iter_in_fwd_small%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_fwd_traverse_small%=\n\t"
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : [seed] "r" (seed_fwd)
            : );

        asm volatile ("isb\n\t");
        /*asm volatile ("dsb sy\n\t");*/
    }

    return 0;
}
uint8_t dual_traverse_bwd(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if (eviction_set->hashed)
        return dual_traverse_bwd_dmpoff(eviction_set, num_repeat);

    volatile node_t* iter = 0;
    volatile node_t* lagging_iter = 0;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a backward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = tail;
        lagging_iter = tail;
        /*iter = tail + (uint64_t)iter;*/
        /*lagging_iter = tail + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 8 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t" // 1st
            "LDR %[iter], [%[iter], #8]\n\t" // 2nd
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 8th
            : [iter] "+r" (iter)
            :
            : );

        /*asm volatile ("isb\n\t");  this isb seems to be critical*/
        /*asm volatile ("dsb sy\n\t");*/

        // iterater over eviction set with iter and lagging_iter
        asm volatile (
            "L_bwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter], #8]\n\t"
            "L_skip_iter_in_bwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            :
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
uint8_t dual_traverse_bwd_dmpoff(eviction_set_t* eviction_set, int num_repeat) {
    if ( __builtin_expect(!eviction_set->list_of_cachelines->head, 0) )
        exit(0);

    if ( __builtin_expect(!eviction_set->hashed, 0) )
        exit(1);

    volatile node_t* iter = 0;
    volatile node_t* lagging_iter = 0;
    int64_t num_cache_lines;
    node_t* head = eviction_set->list_of_cachelines->head;
    node_t* tail = eviction_set->list_of_cachelines->tail;
    int num_nodes = eviction_set->list_of_cachelines->num_nodes;

    uint64_t seed_bwd = eviction_set->seed_bwd;

    for (int i = 0; i < num_repeat; i++) {
        // Perform a backward traversal over two linked lists with one cursor
        // lagging behind n steps to perform dual-chasing
        iter = tail;
        lagging_iter = tail;
        /*iter = tail + (uint64_t)iter;*/
        /*lagging_iter = tail + (uint64_t)lagging_iter;*/
        num_cache_lines = num_nodes;

        // repeat "iter = iter->last" 8 times
        asm volatile (
            "LDR %[iter], [%[iter], #8]\n\t" // 1st
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 2nd
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "LDR %[iter], [%[iter], #8]\n\t" // 8th
            "EOR %[iter], %[iter], %[seed]\n\t"
            : [iter] "+r" (iter)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("isb\n\t"); // this isb seems to be critical
        /*asm volatile ("dsb sy\n\t");*/

        // iterater over eviction set with iter and lagging_iter
        asm volatile (
            "L_bwd_traverse%=:\n\t"
            "LDR %[lagging_iter], [%[lagging_iter], #8]\n\t"
            "EOR %[lagging_iter], %[lagging_iter], %[seed]\n\t"
            "CBZ %[iter], L_skip_iter_in_bwd%=\n\t" // if iter reaches the end
            /*"isb\n\t"*/
            /*"dsb sy\n\t"*/
            "LDR %[iter], [%[iter], #8]\n\t"
            "EOR %[iter], %[iter], %[seed]\n\t"
            "L_skip_iter_in_bwd%=:\n\t"
            "SUB %[num_cache_lines], %[num_cache_lines], #1\n\t"
            "CBNZ %[num_cache_lines], L_bwd_traverse%="
            : [iter] "+r" (iter), [lagging_iter] "+r" (lagging_iter),
              [num_cache_lines] "+r" (num_cache_lines)
            : [seed] "r" (seed_bwd)
            : );

        asm volatile ("isb\n\t");
    }

    return 0;
}
