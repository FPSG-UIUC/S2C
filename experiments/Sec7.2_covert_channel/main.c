#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

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

#define SCALE 10
#define INTERVAL 8000
#define NUM_BITS 100

static inline uint64_t get_cycle() {
    uint64_t cycle;
    asm volatile (
            "isb\n\t"
            "mrs %[cycle], S3_2_c15_c0_0\n\t"
            "isb\n\t"
            : [cycle] "=r" (cycle) : :);

    return cycle;
}

static inline void delay(int n) {
    asm volatile (
            "isb\n\t"
            "Start_delay%=:\n\t"
            "sub %w[n], %w[n], #1\n\t"
            "cbnz %w[n], Start_delay%=\n\t"
            "isb\n\t"
            : [n] "+r" (n) ::);
}


uint8_t* BIT;
uint8_t* DUMMY;

// used for indicating the end of preparation phase
volatile uint8_t* sender_start;
volatile uint8_t* receiver_start;
volatile uint8_t* sender_end;
volatile uint8_t* receiver_end;

volatile eviction_set_t* l2_eviction_set_bit            = NULL;
volatile eviction_set_t* l2_eviction_set_dummy          = NULL;

uint64_t time0;

uint64_t time_diff(uint64_t t) {
    return (t - time0) % 2000000;
}

uint64_t sender_start_time = 0;
uint64_t receiver_start_time = 0;
uint64_t sender_end_time = 0;
uint64_t receiver_end_time = 0;



#define RATIO 1

uint8_t* bits_to_send;

#define DBG

// sender (child)
uint64_t sender_start_round[NUM_BITS];
uint64_t sender_receive_ready[NUM_BITS];
uint64_t sender_access_addr[NUM_BITS];
uint64_t sender_end_round[NUM_BITS];

uint64_t receiver_start_round[NUM_BITS];
uint64_t receiver_ends_prime[NUM_BITS];
uint64_t receiver_start_probe[NUM_BITS];
uint64_t receiver_end_round[NUM_BITS];

void sender() {
    pin_p_core(P_CORE_1_ID);

    maccess(BIT);
    traverse_eviction_set(l2_eviction_set_bit);

    // sync with receiver
    *sender_start = 1;
    while (*receiver_start == 0) {}

        // Sender Loop
        //  PrimeY(READY)
        //  delay
        //  evict = ProbeY(READY)
        //
        //  if evict, continue
        //  else, go back to Sender Loop
        //
        //  delay
        //  access eviction set of BIT
        //
        //  back to Sender Loop
    sender_start_time= get_cycle();
    uint64_t next_sender_act_time = sender_start_time + INTERVAL;

    for (int i = 0; i < NUM_BITS; i++) {
        while (get_cycle() < next_sender_act_time) {}

#ifdef DBG
        sender_start_round[i] = get_cycle();
#endif

        // wait till the receiver finish the eviction set traversal
        /*delay(800 * RATIO);*/

#ifdef DBG
        sender_access_addr[i] = get_cycle();
#endif

        if (bits_to_send[i]) {
            asm volatile ("isb");
            /*maccess(BIT);*/
            /*maccess(BIT);*/
            traverse_eviction_set(l2_eviction_set_bit);
        }
        /*else {*/
            /*asm volatile ("isb");*/
            /*maccess(DUMMY);*/
        /*}*/

#ifdef DBG
        sender_end_round[i] = get_cycle();
#endif

        next_sender_act_time = next_sender_act_time + INTERVAL;
    }

    sender_end_time = get_cycle();

    *sender_end = 1;
    while (*(volatile uint8_t*)receiver_end == 0) {}
}

int main(int argc, char** argv) {

    srand(time(NULL));
    allocate_cache_flush_buffer();
    flush_cache();

    uint8_t* shared_page = (uint8_t*)mmap(NULL, PAGE_SIZE*40, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, 0, 0);
    assert (shared_page);
    memset(shared_page, 0xff, PAGE_SIZE * 40);

    // [Preparation] Step 1: prepare the data
    sender_start = shared_page;
    receiver_start = shared_page + 1;
    sender_end = shared_page + 2;
    receiver_end = shared_page + 3;
    *sender_start = 0;
    *receiver_start = 0;
    *sender_end = 0;
    *receiver_end = 0;

    bits_to_send = shared_page + 4;
    for (int i = 0; i < NUM_BITS; i++)
        bits_to_send[i] = rand() % 2;
        /*bits_to_send[i] = 1;*/

    /*READY = shared_page + 11 * (PAGE_SIZE + L2_LINE_SIZE);*/
    BIT = shared_page + 22 * (PAGE_SIZE + L2_LINE_SIZE);
    /*DUMMY = shared_page + 33 * (PAGE_SIZE + L2_LINE_SIZE);*/

    // [Preparation] Step 2: find eviction sets
    cache_line_set_t* l2_evset_cache_lines_bit   = find_L2_eviction_set_using_timer(BIT);
    for(int i = 0; i < 6; i++)
       l2_evset_cache_lines_bit->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines_bit->cache_lines[i]); 
    /*cache_line_set_t* l2_evset_cache_lines_dummy = find_L2_eviction_set_using_timer(DUMMY);*/
    /*float l2_evrate = P1S1P1_timer(RET_L2_EVRATE, BIT, l2_evset_cache_lines_bit);*/
    /*printf("%f\n", l2_evrate);*/

    l2_eviction_set_bit   = create_eviction_set(l2_evset_cache_lines_bit);
    /*l2_eviction_set_dummy = create_eviction_set(l2_evset_cache_lines_dummy);*/

    pthread_t sender_tid;
    pthread_attr_t attr;
    size_t stacksize;

    pthread_attr_init(&attr);
    pthread_attr_getstacksize(&attr, &stacksize);
    stacksize = 0x1000000;
    pthread_attr_setstacksize(&attr, stacksize);

    pthread_create(&sender_tid, &attr, (void*)sender, 0);

    time0 = get_cycle();

    int bits_received[NUM_BITS];

    // receiver
    pin_p_core(P_CORE_0_ID);

    // warmup
    maccess(BIT);
    traverse_eviction_set(l2_eviction_set_bit);

    // sync with sender
    *receiver_start = 1;
    while (*sender_start == 0) {}

    // wait
    // Receiver Loop:
    //   access eviction set of READY
    //   PrimeY(BIT)
    //   delay
    //   ProbeY(BIT)
    //   delay
    //   back to Receiver Loop

    receiver_start_time = get_cycle();
    uint64_t next_receiver_act_time = receiver_start_time + INTERVAL;

    for (int i = 0; i < NUM_BITS; i++) {
        while (get_cycle() < next_receiver_act_time) {}

#ifdef DBG
        receiver_start_round[i] = get_cycle();
#endif

        /*single_traverse_roundtrip(l2_eviction_set_bit);*/
        /*traverse_eviction_set(l2_eviction_set_bit);*/
        maccess(BIT);

#ifdef DBG
        receiver_ends_prime[i] = get_cycle();
#endif

        /*for (volatile int k = 0; k < 2000; k++) {}*/
        delay(6000 * RATIO);
        /*maccess(BIT);*/
        /*maccess(BIT);*/
        /*maccess(BIT);*/

#ifdef DBG
        receiver_start_probe[i] = get_cycle();
#endif

        uint64_t latency = 0;
        uint8_t load_val = 0;
        asm volatile (
                "dsb sy\n\t"
                "isb\n\t"
                "mrs x9, S3_2_c15_c0_0\n\t"
                "ldrb %w[val], [%[addr]]\n\t"
                "isb\n\t"
                "mrs x10, S3_2_c15_c0_0\n\t"
                "sub %[latency], x10, x9\n\t"
                "dsb sy\n\t"
                : [latency] "=r" (latency), [val] "=r" (load_val)
                : [addr]  "r" (BIT)
                : "x9", "x10");

        bits_received[i] = latency > L2_MISS_MIN_LATENCY;
        next_receiver_act_time = next_receiver_act_time + INTERVAL;

#ifdef DBG
        receiver_end_round[i] = get_cycle();
#endif
    }

    receiver_end_time = get_cycle();
    /*printf("bit is %d\n", bit);*/
    *receiver_end = 1;
    while (*(volatile uint8_t*)sender_end == 0) {}


#ifdef DBG
    for (int i = 0; i < NUM_BITS; i++) {
        printf("%d: sender starts round: %ld, sender starts sending: %ld, sender finishes send: %ld\n",
                i, 
                time_diff(sender_start_round[i]) / SCALE, 
                time_diff(sender_access_addr[i]) / SCALE, 
                time_diff(sender_end_round[i]) / SCALE);
        printf("%d: receiver starts round: %ld, receiver ends prime: %ld, receiver starts probe: %ld, receiver finish round: %ld\n",
                i, 
                time_diff(receiver_start_round[i]) / SCALE, 
                time_diff(receiver_ends_prime[i]) / SCALE, 
                time_diff(receiver_start_probe[i]) / SCALE,
                time_diff(receiver_end_round[i]) / SCALE);
    }
#endif

    printf("receiver bandwidth: %d K bit/second\n", 3 * 1000 * 1000 / ((receiver_end_time - receiver_start_time) / NUM_BITS));
    printf("sender bandwidth: %d K bit/second\n", 3 * 1000 * 1000 / ((sender_end_time - sender_start_time) / NUM_BITS));


    printf("%d bits sent:     ", NUM_BITS);
    for (int i = 0; i < NUM_BITS; i++)
        printf("%d ", bits_to_send[i]);
    printf("\n");
    printf("%d bits recevied: ", NUM_BITS);
    for (int i = 0; i < NUM_BITS; i++)
        printf("%d ", bits_received[i]);
    printf("\n");

    int error = 0;
    for (int i = 0; i < NUM_BITS; i++)
        error += (bits_to_send[i] != bits_received[i]);
    printf("accuracy: %f\n", 1 - (float)error / NUM_BITS);

}
