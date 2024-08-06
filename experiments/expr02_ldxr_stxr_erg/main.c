#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

#include <basics/arch.h>
#include <basics/params.h>

// Goal of this experiment: measure the granularity of exclusive access
//
// Based on ARM's specification, each LDREX/STREX applies to an entire ERG (Exclusive Region Granular), 
// instead of only to the 4-byte or 8-byte data at the accessed location
// In other words, even if LDXR addr1 and STXR addr2 point to different addresses (with non-overlapping bytes),
// as long as they're within one ERG, STXR will return SUCCESS
//
// Result: On Apple M1, ERG is 64 bytes (L1 cache line size)

static inline void load_linked(uint8_t* lock_addr) {
    asm volatile ("ldxrb %w[lock_val], [%[lock_addr]]"
            : [lock_val] "=r" (global_junk)
            : [lock_addr] "r" (lock_addr)
    );
}


static inline uint32_t store_conditional(uint8_t* lock_addr) {
    uint32_t failed;
    asm volatile ("stxrb %w[failed], %w[lock_val], [%[lock_addr]]"
            : [failed] "+r" (failed), [lock_val]"+r"(global_junk)
            : [lock_addr]"r"(lock_addr)
            /*: [failed] "+r" (failed)*/
            /*: [lock_val]"r"(global_junk), [lock_addr]"r"(lock_addr)*/
            : "memory"
    );
    return failed;
}


int main() {

    void* base = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (base);
    memset(base, 0xff, PAGE_SIZE);

    uint64_t lock1_addr, lock2_addr;

    uint32_t sc_failed;

    // experiment #1
    uint64_t first_success_lock2_addr = 0, last_success_lock2_addr = 0;
    lock1_addr = (uint64_t)base + PAGE_SIZE / 4 + rand() % (PAGE_SIZE / 2);
    for (lock2_addr = lock1_addr - 0x100; lock2_addr < lock1_addr + 0x100; lock2_addr++) {
        load_linked((uint8_t*)lock1_addr);
        asm volatile ("dsb sy");
        sc_failed = store_conditional((uint8_t*)lock2_addr);

        if (!sc_failed) {
            if (first_success_lock2_addr == 0)
                first_success_lock2_addr = lock2_addr;

            last_success_lock2_addr = lock2_addr;
        }
    }

    uint64_t erg_measured_by_fix_ldxr_addr = last_success_lock2_addr + 1 - first_success_lock2_addr;
    printf("Fix ldxr addr, scan stxr addr: ERG = 0x%lx\n", erg_measured_by_fix_ldxr_addr);

    // experiment #2
    uint64_t first_success_lock1_addr = 0, last_success_lock1_addr = 0;
    lock2_addr = (uint64_t)base + PAGE_SIZE / 4 + rand() % (PAGE_SIZE / 2);
    for (lock1_addr = lock2_addr - 0x100; lock1_addr < lock2_addr + 0x100; lock1_addr++) {
        load_linked((uint8_t*)lock1_addr);
        asm volatile ("dsb sy");
        sc_failed = store_conditional((uint8_t*)lock2_addr);

        if (!sc_failed) {
            if (first_success_lock1_addr == 0)
                first_success_lock1_addr = lock1_addr;

            last_success_lock1_addr = lock1_addr;
        }
    }

    uint64_t erg_measured_by_fix_stxr_addr = last_success_lock1_addr + 1 - first_success_lock1_addr;
    printf("Fix stxr addr, scan ldxr addr: ERG = 0x%lx\n", erg_measured_by_fix_stxr_addr);

    if (erg_measured_by_fix_ldxr_addr != L1_LINE_SIZE || erg_measured_by_fix_stxr_addr != L1_LINE_SIZE) {
        printf("WARNING: ERG is not L1_LINE_SIZE.\n");
    }
    else {
        printf("ERG is L1_LINE_SIZE.\n");
    }
    return 0;
}
