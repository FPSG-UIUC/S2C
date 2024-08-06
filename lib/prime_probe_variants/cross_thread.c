#include "cross_thread.h"
#include "../basics/arch.h"
#include "../basics/defs.h"
#include "../basics/allocator.h"
#include "../basics/cache_line_set.h"
#include "../basics/sys_utils.h"
#include "../eviction_set/eviction_set.h"

#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

cache_line_set_t* cache_line_set_traversed_by_child = NULL;
eviction_set_t* eviction_set_traversed_by_child = NULL;
uint8_t* victim_addr_accessed_by_child = NULL;

volatile int child_traverse_set_start = 0;
volatile int child_traverse_set_finish = 0;
volatile int child_traverse_set_terminate = 0;
pthread_t child_traverse_set_tid;

volatile int child_access_addr_start = 0;
volatile int child_access_addr_finish = 0;
volatile int child_access_addr_terminate = 0;
pthread_t child_access_addr_tid;

volatile int child_access_addr_and_traverse_set_start = 0;
volatile int child_access_addr_and_traverse_set_finish = 0;
volatile int child_access_addr_and_traverse_set_terminate = 0;
pthread_t child_access_addr_and_traverse_set_tid;

int traverse_rep_count = 0;

void set_child_cache_line_set(cache_line_set_t* cache_line_set) {
    cache_line_set_traversed_by_child = cache_line_set;
}

void set_child_eviction_set(eviction_set_t* eviction_set) {
    eviction_set_traversed_by_child = eviction_set;
}

void set_child_access_addr(uint8_t* addr) {
    victim_addr_accessed_by_child = addr;
}

void wakeup_child_access_addr_and_wait() {
    // let victim thread access victim_addr
    child_access_addr_start = 1;

    // wait for child to finish
    while (!child_access_addr_finish) {}
    child_access_addr_finish = 0;
}

volatile uint64_t zero = 0;

void wakeup_child_access_addr_nowait() {
    // let victim thread access victim_addr
    child_access_addr_start = 1;
}

void wait_child_access_addr() {
    while (!child_access_addr_finish) {}
    child_access_addr_finish = 0;
}

void wakeup_child_traverse_set_and_wait() {
    // let child thread run
    child_traverse_set_start = 1;

    // wait for child to finish
    while (!child_traverse_set_finish) {}
    child_traverse_set_finish = 0;
}

void wakeup_child_access_addr_and_traverse_set_and_wait() {
    // let child thread run
    child_access_addr_and_traverse_set_start = 1;

    // wait for child to finish
    while (!child_access_addr_and_traverse_set_finish) {}
    child_access_addr_and_traverse_set_finish = 0;
}

void wakeup_child_access_addr_and_traverse_set_nowait() {
    // let child thread run
    child_access_addr_and_traverse_set_start = 1;

    // no wait
}

volatile uint64_t child_ts[20 * 100] = {0};
volatile int child_idx = 0;
volatile int child_record = 0;

void child_access_addr_func(void* core_id) {
    uint8_t local_junk;
    pin_p_core((uint64_t)core_id);

    while (1) {
        while (!child_access_addr_start) {
            if (child_access_addr_terminate) {
                goto L_child_access_addr_exit;
            }
        }

        asm volatile ("isb\n\t");
        uint64_t ts = 0;
        if (child_record) {
            asm volatile (
                    "isb\n\t"
                    "mrs %[ts], S3_2_c15_c0_0\n\t"
                    : [ts] "=r" (ts)
                    : : "memory");
        }

        asm volatile (
                "dmb sy\n\t"
                "dsb sy\n\t"
                "ldrb %w[val], [%[victim_addr]]\n\t"
                "dsb sy\n\t"
                "dmb sy\n\t"
                : [val] "=r" (local_junk)
                : [victim_addr] "r" ((uint64_t)victim_addr_accessed_by_child));

        asm volatile ("isb\n\t");
        if (child_record) {
            child_ts[child_idx] = ts;
            child_idx++;
            child_record = 0;
        }

        child_access_addr_start = 0;
        child_access_addr_finish = 1;
    }

L_child_access_addr_exit:
    child_access_addr_terminate = 0;
    return;
}

volatile uint64_t val = 0;
volatile uint64_t traverse_duration = 0;
void child_naive_traverse_set_func(void* core_id) {
    pin_p_core((uint64_t)core_id);

    while (1) {
        while (!child_traverse_set_start) {
            if (child_traverse_set_terminate) {
                goto L_child_traverse_set_naive_exit;
            }
        }

        asm volatile ("dsb sy");
        asm volatile ("isb");


        /*for (int i = 0; i < cache_line_set_traversed_by_child->num_cache_lines; i++)*/
            /*val = val ^ *(uint64_t*)cache_line_set_traversed_by_child->cache_lines[i];*/
        /*for (int i = 0; i < cache_line_set_traversed_by_child->num_cache_lines; i++)*/
            /*val = val ^ *(uint64_t*)cache_line_set_traversed_by_child->cache_lines[i];*/
        uint64_t t1;
        asm volatile (  "isb\n\t"
                        "mrs %[t], S3_2_c15_c0_0\n\t"
                        : [t] "=r" (t1));

        single_traverse_fwd(eviction_set_traversed_by_child, traverse_rep_count);

        uint64_t t2;
        asm volatile (  "isb\n\t"
                        "mrs %[t], S3_2_c15_c0_0\n\t"
                        : [t] "=r" (t2));

        traverse_duration = t2 - t1;
        /*single_traverse_bwd(eviction_set_traversed_by_child, 1);*/
        /*single_traverse_roundtrip(eviction_set_traversed_by_child, 2);*/
        /*dual_traverse_fwd(eviction_set_traversed_by_child, 5);*/
        /*dual_traverse_bwd(eviction_set_traversed_by_child, 2);*/

        // only 4 repetitions can guarantee to evict the line
        /*dual_traverse_roundtrip(eviction_set_traversed_by_child, 3);*/

        /*traverse_eviction_set_naive(eviction_set_traversed_by_child);*/

/*#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set_naive(eviction_set_traversed_by_child);*/
/*#endif*/

/*#if defined(TRIPLE_TRAVERSE)*/
        /*traverse_eviction_set_naive(eviction_set_traversed_by_child);*/
/*#endif*/

        asm volatile ("dsb sy");

        child_traverse_set_start = 0;
        child_traverse_set_finish = 1;
    }

L_child_traverse_set_naive_exit:
    child_traverse_set_terminate = 0;
    return;

}

void child_traverse_set_func(void* core_id) {
    pin_p_core((uint64_t)core_id);

    while (1) {
        while (!child_traverse_set_start) {
            if (child_traverse_set_terminate) {
                goto L_child_traverse_set_exit;
            }
        }

        asm volatile ("dsb sy");

        traverse_eviction_set(eviction_set_traversed_by_child);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set_traversed_by_child);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set_traversed_by_child);
#endif

        asm volatile ("dsb sy");

        child_traverse_set_start = 0;
        child_traverse_set_finish = 1;
    }

L_child_traverse_set_exit:
    child_traverse_set_terminate = 0;
    return;
}

void child_access_addr_and_traverse_set_func(void* core_id) {
    uint8_t local_junk;
    pin_p_core((uint64_t)core_id);

    while (1) {
        while (!child_access_addr_and_traverse_set_start) {
            if (child_access_addr_and_traverse_set_terminate) {
                goto L_child_access_addr_and_traverse_set_exit;
            }
        }

        asm volatile ("isb\n\t");
        asm volatile (
                "dsb sy\n\t"
                "ldrb %w[val], [%[victim_addr]]\n\t"
                "dsb sy\n\t"
                : [val] "=r" (local_junk)
                : [victim_addr] "r" (victim_addr_accessed_by_child));
        asm volatile ("isb\n\t");


        asm volatile ("dsb sy");

        traverse_eviction_set(eviction_set_traversed_by_child);

#if defined(DOUBLE_TRAVERSE) || defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set_traversed_by_child);
#endif

#if defined(TRIPLE_TRAVERSE)
        traverse_eviction_set(eviction_set_traversed_by_child);
#endif

        asm volatile ("dsb sy");

        child_access_addr_and_traverse_set_start = 0;
        child_access_addr_and_traverse_set_finish = 1;
    }

L_child_access_addr_and_traverse_set_exit:
    child_access_addr_and_traverse_set_terminate = 0;
    return;
}

void create_child_thread(int child_type, int core_id) {
    pthread_attr_t attr;
    size_t stacksize;

    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stacksize);
    stacksize = 0x1000000;
    pthread_attr_setstacksize(&attr, stacksize);

    uint64_t child_core_id = core_id;

    if (child_type == CHILD_ACCESS_ADDR)
        pthread_create(&child_access_addr_tid, &attr, (void*)child_access_addr_func, (void*)child_core_id);
    else if (child_type == CHILD_TRAVERSE_SET)
        pthread_create(&child_traverse_set_tid, &attr, (void*)child_traverse_set_func, (void*)child_core_id);
    else if (child_type == CHILD_NAIVELY_TRAVERSE_SET)
        pthread_create(&child_traverse_set_tid, &attr, (void*)child_naive_traverse_set_func, (void*)child_core_id);
    else if (child_type == CHILD_ACCESS_ADDR_AND_TRAVERSE_SET) {
        pthread_create(&child_access_addr_and_traverse_set_tid, &attr, (void*)child_access_addr_and_traverse_set_func, (void*)child_core_id);
    }
    else {
        fprintf(stderr, "ERROR: unknown child thread type : %d.\n", child_type);
        exit(1);
    }
}

void terminate_child_access_addr_thread() {
    child_access_addr_terminate = 1;
    pthread_join(child_access_addr_tid, NULL);
}

void terminate_child_traverse_set_thread() {
    child_traverse_set_terminate = 1;
    pthread_join(child_traverse_set_tid, NULL);
}

void terminate_child_access_addr_and_traverse_set_thread() {
    child_access_addr_and_traverse_set_terminate = 1;
    pthread_join(child_access_addr_and_traverse_set_tid, NULL);
}

void set_child_traverse_rep_count(int rep_count) {
    traverse_rep_count = rep_count;
}
