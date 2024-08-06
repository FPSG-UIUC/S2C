#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#define PAGE_SIZE 0x4000

static inline uint64_t get_time0() {
    uint64_t time;
    asm volatile ("mrs %[time], S3_2_c15_c0_0" : [time] "=r" (time));
    return time;
}

int main() {
    /*int fd = open("/dev/pmc", O_RDWR);*/
    /*if (fd < 0) {*/
        /*perror("open");*/
        /*return 1;*/
    /*}*/

    uint8_t* addr = (uint8_t*)mmap(NULL, 100*PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
    for (int i = 0; i < 100*PAGE_SIZE; i++) {
        addr[i] = i;
        asm volatile ("dsb sy\n\t");
    }

    /*uint64_t pmuserenr_el0_val = 0;*/
    /*asm volatile ("mrs %0, PMUSERENR_EL0\n\t" : "=r"(pmuserenr_el0_val));*/
    /*printf("old pmuserenr_el0_val: 0x%llx\n", pmuserenr_el0_val);*/
    /*pmuserenr_el0_val = pmuserenr_el0_val | (1<<0);*/
    /*pmuserenr_el0_val = pmuserenr_el0_val | (1<<2);*/
    /*printf("new pmuserenr_el0_val: 0x%llx\n", pmuserenr_el0_val);*/
    /*asm volatile ("msr PMUSERENR_EL0, %0\n\t" : : "r"(pmuserenr_el0_val));*/

    /*int rc = ioctl(fd, 1, 2);*/
    /*if (rc < 0) {*/
        /*perror("ioctl");*/
    /*}*/

    int p = 1;
    asm volatile ("isb");
    uint64_t t0 = get_time0();
    p = addr[0];
    asm volatile ("isb");
    uint64_t t1 = get_time0();
    printf("%lld, %d\n", t1 - t0, p );


    uint8_t junk = 0;
    for (int i = 0; i < 10*PAGE_SIZE; i++)
        junk ^= addr[i];
    printf("%d\n", junk);
    /*(void) close(fd);*/
    return 0;
}
