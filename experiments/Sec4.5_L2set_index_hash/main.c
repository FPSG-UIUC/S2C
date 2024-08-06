#include <alloca.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <basics/defs.h>
#include <basics/arch.h>
#include <basics/params.h>
#include <basics/cache_line_set.h>
#include <basics/cache_utils.h>
#include <basics/sys_utils.h>
#include <basics/math_utils.h>
#include <prime_probe_variants/prime_probe_variants.h>
#include <prime_probe_variants/same_thread.h>
#include <prime_probe_variants/cross_thread.h>
#include <eviction_set/eviction_set_generation.h>


typedef struct {
    uint64_t pfn : 55;
    unsigned int soft_dirty : 1;
    unsigned int file_page : 1;
    unsigned int swapped : 1;
    unsigned int present : 1;
} PagemapEntry;


/* Parse the pagemap entry for the given virtual address.
 *
 * @param[out] entry      the parsed entry
 * @param[in]  pagemap_fd file descriptor to an open /proc/pid/pagemap file
 * @param[in]  vaddr      virtual address to get entry for
 * @return 0 for success, 1 for failure
 */
int pagemap_get_entry(PagemapEntry *entry, int pagemap_fd, uintptr_t vaddr)
{
    size_t nread;
    ssize_t ret;
    uint64_t data;
    uintptr_t vpn;

    vpn = vaddr / sysconf(_SC_PAGE_SIZE);
    nread = 0;
    while (nread < sizeof(data)) {
        ret = pread(pagemap_fd, ((uint8_t*)&data) + nread, sizeof(data) - nread, vpn * sizeof(data) + nread);
        nread += ret;
        if (ret <= 0) {
            return 1;
            }
    }
    entry->pfn = data & (((uint64_t)1 << 55) - 1);
    entry->soft_dirty = (data >> 55) & 1;
    entry->file_page = (data >> 61) & 1;
    entry->swapped = (data >> 62) & 1;
    entry->present = (data >> 63) & 1;
    return 0;
}

/* Convert the given virtual address to physical using /proc/PID/pagemap.
 *
 * @param[out] paddr physical address
 * @param[in]  pid   process to convert for
 * @param[in] vaddr virtual address to get entry for
 * @return 0 for success, 1 for failure
 */
int virt_to_phys_user(uintptr_t *paddr, pid_t pid, uintptr_t vaddr)
{
    char pagemap_file[BUFSIZ];
    int pagemap_fd;

    snprintf(pagemap_file, sizeof(pagemap_file), "/proc/%ju/pagemap", (uintmax_t)pid);
    pagemap_fd = open(pagemap_file, O_RDONLY);
    if (pagemap_fd < 0) {
        return 1;
    }
    PagemapEntry entry;
    if (pagemap_get_entry(&entry, pagemap_fd, vaddr)) {
        return 1;
    }
    close(pagemap_fd);
    *paddr = (entry.pfn * sysconf(_SC_PAGE_SIZE)) + (vaddr % sysconf(_SC_PAGE_SIZE));
    return 0;
}

void print_header() {
    printf("\t\t\t\t\t| high order | hugepoff  |regpoff|lineoff|\n");
    printf("\t\t\t\t\t|ba9876543210|a9876543210|6543210|6543210|\n");
}

#define tot_addrs  (1+12+12)
#define hash_width 15
int curr = 0;
int xor[1<<hash_width][tot_addrs];

void print_paddr(uint64_t vaddr, pid_t pid) {
    uintptr_t paddr = 0;
    if (virt_to_phys_user(&paddr, pid, vaddr)) {
        fprintf(stderr, "ERROR: cannot translate.\n");
        exit(1);
    }
    printf("0x%lx |0x%lx|0x%lx|0x%lx|0x%lx|", paddr,HIGH(paddr), HPO(paddr), RPO(paddr), CLO(paddr));
    int max_pow = 12;

    printf("\t|");
    while (max_pow > 0) {
        max_pow -= 1;
        printf("%u", HIGH(paddr) & (1 << max_pow) ? 1 : 0);
    }
    printf("|");

    max_pow = HPO_nbits;
    while (max_pow > 0) {
        max_pow -= 1;
        printf("%u", HPO(paddr) & (1 << max_pow) ? 1 : 0);
    }
    printf("|");

    max_pow = RPO_nbits;
    while (max_pow > 0) {
        max_pow -= 1;
        printf("%u", RPO(paddr) & (1 << max_pow) ? 1 : 0);
    }
    printf("|");

    max_pow = CLO_nbits;
    while (max_pow > 0) {
        max_pow -= 1;
        printf("%u", CLO(paddr) & (1 << max_pow) ? 1 : 0);
    }
    printf("|\t");

    int hi7 = HIGH(paddr) & (1 << 7) ? 1 : 0;
    int hi6 = HIGH(paddr) & (1 << 6) ? 1 : 0;
    int hi5 = HIGH(paddr) & (1 << 5) ? 1 : 0;
    int hi4 = HIGH(paddr) & (1 << 4) ? 1 : 0;
    int hi3 = HIGH(paddr) & (1 << 3) ? 1 : 0;
    int hi2 = HIGH(paddr) & (1 << 2) ? 1 : 0;
    int hi1 = HIGH(paddr) & (1 << 1) ? 1 : 0;
    int hi0 = HIGH(paddr) & (1 << 0) ? 1 : 0;

    int hpo9 = HPO(paddr) & (1 << 9) ? 1 : 0;
    int hpo8 = HPO(paddr) & (1 << 8) ? 1 : 0;
    int hpo0 = HPO(paddr) & (1 << 0) ? 1 : 0;

    int hpo10 = HPO(paddr) & (1 << 10) ? 1 : 0;
    int hpo7  = HPO(paddr) & (1 << 7) ? 1 : 0;
    int hpo6  = HPO(paddr) & (1 << 6) ? 1 : 0;
    int hpo1  = HPO(paddr) & (1 << 1) ? 1 : 0;

    int bits[hash_width] = {hi7,hi6,hi5,hi4,hi3,hi2,hi1,hi0,hpo10,hpo9,hpo8,hpo7,hpo6,hpo1,hpo0};
    for (int hash = 1; hash < (1<<hash_width)-1; hash++) {
        int _hash = hash;
        int _xor = 0;
        for (int n = 0; n < hash_width; n++)
            if ((_hash >> n) % 2)
                _xor ^= bits[hash_width-1-n];
        xor[hash][curr] = _xor;
    }
    curr += 1;

    printf("\n");
}
int main(int argc, char** argv) {
    pid_t pid = getpid();

    srand(time(NULL));
    pin_p_core(P_CORE_1_ID);

    allocate_cache_flush_buffer();

    uint8_t* victim_page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    memset(victim_page, 0xff, PAGE_SIZE);
    uint8_t* victim_addr = victim_page + rand() % PAGE_SIZE;

    num_counts = 10;

    cache_line_set_t* l2_congruent_cache_lines_1 = find_L2_eviction_set_using_timer(victim_addr);
    cache_line_set_t* l2_congruent_cache_lines_2 = find_L2_eviction_set_using_timer(victim_addr);

    print_header();
    print_paddr((uint64_t)victim_addr, pid);

    for (int i = 0; i < l2_congruent_cache_lines_1->num_cache_lines; i++)
        print_paddr(l2_congruent_cache_lines_1->cache_lines[i], pid);
    for (int i = 0; i < l2_congruent_cache_lines_2->num_cache_lines; i++)
        print_paddr(l2_congruent_cache_lines_2->cache_lines[i], pid);

    for (int hash = 1; hash < (1<<hash_width)-1; hash++) {
        int xor_0 = xor[hash][0];
        int good_hash = 1;
        for (int j = 1; j < tot_addrs; j++) {
            if (xor[hash][j] != xor_0)
                good_hash = 0;
        }
        char* bitnames[hash_width] = {"hi7","hi6","hi5","hi4","hi3","hi2","hi1","hi0","hpo10","hpo9","hpo8","hpo7","hpo6","hpo1","hpo0"};
        if (good_hash) {
            printf("good hash: 0x%x --> ", hash);
            int _hash = hash;
            for (int n = 0; n < hash_width; n++)
                if ((_hash >> n) % 2)
                    printf("%s ", bitnames[hash_width-1-n]);
            printf("\n");
        }
    }
    return 0;
}
