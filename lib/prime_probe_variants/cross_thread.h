#ifndef __CROSS_THREAD_H__
#define __CROSS_THREAD_H__

#include "../eviction_set/eviction_set.h"

#define CHILD_ACCESS_ADDR 0
#define CHILD_TRAVERSE_SET 1
#define CHILD_NAIVELY_TRAVERSE_SET 2
#define CHILD_ACCESS_ADDR_AND_TRAVERSE_SET 3

// dedicated child thread to handle some memory accesses
void create_child_thread(int child_type, int core_id);

// set cache line set traversed by child
void set_child_cache_line_set(cache_line_set_t* cache_line_set);

// set eviction set traversed by child
void set_child_eviction_set(eviction_set_t* eviction_set);

// set address accessed by child
void set_child_access_addr(uint8_t* addr);

// Parent blocks itself and wakes up child to traverse set.
// Child blocks itself and wakes up parent when finished.
void wakeup_child_traverse_set_and_wait();

// Parent blocks itself and wakes up child to access the requested address
// Child blocks itself and wakes up parent when finished.
void wakeup_child_access_addr_and_wait();

void wakeup_child_access_addr_nowait();
void wait_child_access_addr();

// Parent blocks itself and wakes up child to access the requested address + traverse set
// Child blocks itself and wakes up parent when finished
void wakeup_child_access_addr_and_traverse_set_and_wait();

// Parent wakes up child to access the requested address + traverse set w/o waiting
void wakeup_child_access_addr_and_traverse_set_nowait();

// terminate the child thread that accesses address
void terminate_child_access_addr_thread();

// terminate the child thread that traverses set
void terminate_child_traverse_set_thread();

// terminate the child thread that accesses address + traverses set
void terminate_child_access_addr_and_traverse_set_thread();

void child_access_addr_func(void* core_id);

void child_naive_traverse_set_func(void* core_id);

void child_traverse_set_func(void* core_id);

void child_access_addr_and_traverse_set_func(void* core_id);

void set_child_traverse_rep_count(int rep_count);

#endif
