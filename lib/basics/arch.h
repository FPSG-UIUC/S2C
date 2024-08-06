#ifndef __ARCH_H__
#define __ARCH_H__

#define KB 1024ULL
#define MB 1024*KB

#define PAGE_SIZE               (16 * KB)
#define HUGEPAGE_SIZE           (32 * MB)

#define BASE_HUGEPAGE_VADDR     0xffff00000000ULL
#define MAX_NUM_HUGEPAGES       80

#define M1_L2_NWAYS     12
#define M2_L2_NWAYS     16

#define L1_NWAYS        8
#define L1_LINE_SIZE    64
#define L2_NWAYS        M1_L2_NWAYS // M1
#define L2_LINE_SIZE    128

#define MASK(x)     ((1 << x) - 1)
#define L1_SET_INDEX(addr) ((((uint64_t)addr) % PAGE_SIZE) / L1_LINE_SIZE)

#define HPO_nbits   11
#define RPO_nbits   7
#define CLO_nbits   7

#define HIGH(vaddr)   ( (vaddr >> (CLO_nbits + RPO_nbits + HPO_nbits)) )
#define HPO(vaddr)  ( (vaddr >> (CLO_nbits + RPO_nbits)) & MASK(HPO_nbits) )
#define RPO(vaddr)  ( (vaddr >> CLO_nbits) & MASK(CLO_nbits) )
#define CLO(vaddr)  ( (vaddr) & MASK(CLO_nbits) )

#define E_CORE_0_ID     0
#define E_CORE_1_ID     1
#define E_CORE_2_ID     2
#define E_CORE_3_ID     3

#define P_CORE_0_ID     4
#define P_CORE_1_ID     5 // we will isolate this core. main thread runs on this core
#define P_CORE_2_ID     6 // we will isolate this core. 1st child thread runs on this core
#define P_CORE_3_ID     7

#define L1_HIT_MAX_LATENCY  36
#define L2_MISS_MIN_LATENCY 130

#endif
