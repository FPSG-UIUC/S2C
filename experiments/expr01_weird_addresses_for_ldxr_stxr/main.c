#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>

#include <basics/arch.h>

// Goal of this experiment: try different strange addresses for LDXR/STXR
// Result:
//  - if address is unaligned but doesn't cross L1 cache line, such as
//    0xXXXX0037 for 64b LDXR/STXR, LDXR and STXR have no problem
//  - if address is unaligned and cross L1 cache line, such as
//    0xXXXX0039 for 64b LDXR/STXR, both LDXR and STXR will trigger bus error



int main() {
    // allocate the lock
    void* base = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert (base);
    memset(base, 0xff, PAGE_SIZE);

    uint32_t failed = 0;
    volatile uint64_t lock_val;

    uint64_t unaligned_nolinecross_lock_addr = (uint64_t)base + 0x37;;
    printf("try ldxr and stxr with unaligned address 0x%lx which doesn't cross cache line\n", unaligned_nolinecross_lock_addr);
    asm volatile ("ldxr %[lock_val], [%[lock_addr]]\n"
                : [lock_val] "=r" ((lock_val))
                : [lock_addr] "r" ((unaligned_nolinecross_lock_addr)));

    asm volatile ("stxr %w[failed], %[lock_val], [%[lock_addr]]\n"
                : [failed] "+r" (failed)
                : [lock_val] "r" ((lock_val)), [lock_addr] "r" ((unaligned_nolinecross_lock_addr))
                : "memory");

    printf("stxr returns %d\n", failed);
    printf("If you see this, this means the previous address doesn't cause fault.\n");

    uint64_t unaligned_linecross_lock_addr = (uint64_t)base + 0x39;
    printf("try ldxr and stxr with unaligned address 0x%lx which crosses cache line. You should see a bus error next.\n", unaligned_linecross_lock_addr);
    asm volatile ("ldxr %[lock_val], [%[lock_addr]]\n"
                : [lock_val] "=r" ((lock_val))
                : [lock_addr] "r" ((unaligned_linecross_lock_addr)));

    asm volatile ("stxr %w[failed], %[lock_val], [%[lock_addr]]\n"
                : [failed] "+r" (failed)
                : [lock_val] "r" ((lock_val)), [lock_addr] "r" ((unaligned_linecross_lock_addr))
                : "memory");
    printf("stxr returns %d\n", failed);
    printf("If you see this, this means the previous address doesn't cause fault.\n");

    /*void* base = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);*/
    /*assert (base);*/
    /*memset(base, 0xff, PAGE_SIZE);*/

    /*uint64_t lock1_addr, lock2_addr;*/

    /*uint32_t failed_lock1, failed_lock2;*/

    /*// experiment #1*/
    /*uint64_t first_success_lock2_addr = 0, last_success_lock2_addr = 0;*/
    /*lock1_addr = (uint64_t)base + PAGE_SIZE / 4 + rand() % (PAGE_SIZE / 2);*/
    /*for (lock2_addr = lock1_addr - 0x100; lock2_addr < lock1_addr + 0x100; lock2_addr++) {*/
        /*load_linked((uint8_t*)lock1_addr);*/
        /*asm volatile ("dsb sy");*/
        /*failed_lock2 = store_conditional((uint8_t*)lock2_addr);*/

        /*if (!failed_lock2) {*/
            /*printf("stxr returns failed_lock2 = %d, %lx, %lx\n", failed_lock2, lock1_addr, lock2_addr);*/
            /*if (first_success_lock2_addr == 0)*/
                /*first_success_lock2_addr = lock2_addr;*/

            /*last_success_lock2_addr = lock2_addr;*/
        /*}*/

        /*load_linked((uint8_t*)lock1_addr);*/
        /*asm volatile ("dsb sy");*/
        /*failed = store_conditional((uint8_t*)lock1_addr);*/
        /*printf("failed: %d\n", failed);*/
    /*}*/

    /*uint64_t erg_measured_by_fix_ldxr_addr = last_success_lock2_addr + 1 - first_success_lock2_addr;*/
    /*printf("Fix ldxr addr, scan stxr addr: ERG = 0x%lx\n", erg_measured_by_fix_ldxr_addr);*/

    /*// experiment #2*/
    /*uint64_t first_success_lock1_addr = 0, last_success_lock1_addr = 0;*/
    /*lock2_addr = (uint64_t)base + PAGE_SIZE / 4 + rand() % (PAGE_SIZE / 2);*/
    /*for (lock1_addr = lock2_addr - 0x100; lock1_addr < lock2_addr + 0x100; lock1_addr++) {*/
        /*load_linked((uint8_t*)lock1_addr);*/
        /*asm volatile ("dsb sy");*/
        /*failed_lock2 = store_conditional((uint8_t*)lock2_addr);*/

        /*if (!failed_lock2) {*/
            /*if (first_success_lock1_addr == 0)*/
                /*first_success_lock1_addr = lock1_addr;*/

            /*last_success_lock1_addr = lock1_addr;*/
        /*}*/
    /*}*/

    /*uint64_t erg_measured_by_fix_stxr_addr = last_success_lock1_addr + 1 - first_success_lock1_addr;*/
    /*printf("Fix stxr addr, scan ldxr addr: ERG = 0x%lx\n", erg_measured_by_fix_stxr_addr);*/

    /*if (erg_measured_by_fix_ldxr_addr != L1_LINE_SIZE || erg_measured_by_fix_stxr_addr != L1_LINE_SIZE) {*/
        /*printf("WARNING: ERG is not L1_LINE_SIZE.\n");*/
    /*}*/
    /*else {*/
        /*printf("ERG is L1_LINE_SIZE.\n");*/
    /*}*/
}
