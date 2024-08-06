#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "eviction_set_generation.h"
#include "eviction_set.h"
#include "../basics/defs.h"
#include "../basics/arch.h"
#include "../basics/allocator.h"
#include "../basics/params.h"
#include "../basics/cache_utils.h"
#include "../basics/cache_line_set.h"
#include "../basics/math_utils.h"
#include "../basics/sys_utils.h"
#include "../prime_probe_variants/prime_probe_variants.h"


int num_congruent_addrs_share_l1set_with_pivot_addr = 4;

// Goal: find a set if cache lines that map to the same L1 set as victim_addr
// L1 congruent line addresses share the same L1 set index bits with victim_addr
cache_line_set_t* find_L1_congruent_cache_lines(uint8_t* victim_addr, int set_size) {

    uint64_t page_offset = (uint64_t)victim_addr % PAGE_SIZE;
    allocator_t* allocator = create_allocator(page_offset, PAGE_SIZE);
    set_allocator_mode(allocator, ALLOC_REG_PAGES);

    cache_line_set_t* congruent_cache_lines = build_empty_cache_line_set(allocator);

    for (int i = 0; i < set_size; i++) {
        allocate_page(allocator);
        size_t congruent_addr = allocator->pages[i] + page_offset;
        push_cache_line_to_set(congruent_cache_lines, congruent_addr);
    }

    assert (congruent_cache_lines->num_cache_lines == set_size);

    return congruent_cache_lines;
}


cache_line_set_t* build_l2_eviction_set_superset(allocator_t* allocator, uint8_t* victim_addr, int threshold) {
    cache_line_set_t* candidate_cache_line_set = build_empty_cache_line_set(allocator);

    while (1) {
        int num_cache_lines_to_add = max(16, candidate_cache_line_set->num_cache_lines);

        for (int i = 0; i < num_cache_lines_to_add; i++) {
            // pick a cache line from the allocated cache line pool
            size_t cache_line_candidate = pop_cache_line_from_allocator(allocator);

            // add this cache line to the superset
            push_cache_line_to_set(candidate_cache_line_set, cache_line_candidate);
        }

        int timing = evict_and_time(victim_addr, candidate_cache_line_set);
        _dprintf("latency = %d, candidate_cache_line_set size=%ld\n", timing, candidate_cache_line_set->num_cache_lines);

        if (timing > threshold)
            break; // we found the superset of eviction set
    }

    return candidate_cache_line_set;
}


void reduce_l2_eviction_set_superset(cache_line_set_t* eviction_set_superset, uint8_t* victim_addr, int threshold) {
    cache_line_set_t* reserved_cache_line_set = build_empty_cache_line_set(NULL);

    int max_wait = 20; // we want to at most spend 20 seconds doing the search
    time_t start, end;
    time(&start);

    int evicted = 0;

    // we want the result to be an eviction set with exactly L2_NWAYS elements
    // and the victim_addr can be stably evicted by the eviction set we found
    while (eviction_set_superset->num_cache_lines != L2_NWAYS || !evicted) {
        _dprintf("eviction set superset size: %ld, evicted: %d\n", eviction_set_superset->num_cache_lines, evicted);

        // the reduction process should be fairly quick (less than 10 seconds).
        // We halt if it takes too long (> 20 seconds)
        time(&end);
        int sec = difftime(end, start);
        if (sec > max_wait) {
            fprintf(stderr, "PANIC: takes >20 seconds reducing the eviction set. STOP.\n");
            exit(1);
        }

        // if we drop too many cache lines s.t. the number of cache lines is fewer
        // than the number of ways, we need to stay back and get more cache lines
        if (eviction_set_superset->num_cache_lines < L2_NWAYS) {

            shuffle_cache_line_set(reserved_cache_line_set);

            _dprintf("before poping from reserved: %ld\n", reserved_cache_line_set->num_cache_lines);
            for (int i = eviction_set_superset->num_cache_lines; i < 2 * L2_NWAYS; i++) {
                size_t reserved_cache_line = pop_cache_line_from_set(reserved_cache_line_set);
                push_cache_line_to_set(eviction_set_superset, reserved_cache_line);
            }
            _dprintf("after poping from reserved: %ld\n", reserved_cache_line_set->num_cache_lines);
        }

        // We split the eviction set into #ways + 1 bins of equal size.
        // We then construct an eviction set excluding one of the bins at a time,
        // and check how well each of the eviction sets performs.
        // We then pick those bins that ended up having the highest timings, as it indicates that
        // the eviction set without the bin is still capable of evicting the victim address,
        // i.e. the bin is not necessary

        // create L2_NWAYS+1 bins of indices
        int start_indices[L2_NWAYS+1];
        int end_indices[L2_NWAYS+1];
        for (int i = 0; i < L2_NWAYS+1; i++) {
            start_indices[i] = eviction_set_superset->num_cache_lines / (L2_NWAYS+1) * i;
            end_indices[i] = eviction_set_superset->num_cache_lines / (L2_NWAYS+1) * (i+1);
        }
        end_indices[L2_NWAYS] = eviction_set_superset->num_cache_lines;

        int best_timing = 0; // we want to find the L2_NWAYS bins that yield the max access latency
        int insignificant_bin = -1;

        for (int i = 0; i < L2_NWAYS+1; i++) {
            // create an eviction set that does not contain the i-th bin
            cache_line_set_t* eviction_set_reduced = build_empty_cache_line_set(NULL);

            int start_index = start_indices[i];
            int end_index = end_indices[i];
            for (int j = 0; j < eviction_set_superset->num_cache_lines; j++) {
                if (start_index <= j && j < end_index)
                    continue;
                push_cache_line_to_set(eviction_set_reduced, eviction_set_superset->cache_lines[j]);
            }

            // measure the time it takes to load the victim after accessing eviction_set_reduced
            int timing = evict_and_time(victim_addr, eviction_set_reduced);

            // skip this reduced eviction set if the timing is too fast, indicating that
            // we probably filtered out too many important cache lines
            if (timing < threshold)
                continue;

            // if the latency is greater than threshold, the bin we exclude in the current round of
            // evict_and_time is probably insignificant thus can be discarded
            if (timing >= best_timing) {
                // we found a higher timing. The current bin is more insignificant
                best_timing = timing;
                insignificant_bin = i;
            }

            delete_cache_line_set(eviction_set_reduced);
        }

        _dprintf("find insignificant bin = %d [%d-%d]\n", insignificant_bin, start_indices[insignificant_bin], end_indices[insignificant_bin]);
        _dprintf("current superset size = %ld\n", eviction_set_superset->num_cache_lines);

        // At this point we tested all bins
        if (insignificant_bin >= 0) {
            // We found a bin that doesn't contribute to the eviction set.  Remove this bin
            // First, move the bin to reserved
            for (int x = start_indices[insignificant_bin]; x != end_indices[insignificant_bin]; x++)
                push_cache_line_to_set(reserved_cache_line_set, eviction_set_superset->cache_lines[x]);

            // Second, remove the bin
            for (int x = start_indices[insignificant_bin], y = end_indices[insignificant_bin];
                y < eviction_set_superset->num_cache_lines;
                x++, y++)  {
                // remove the x-th cache line
                eviction_set_superset->cache_lines[x] = eviction_set_superset->cache_lines[y];
            }
            eviction_set_superset->num_cache_lines -= (end_indices[insignificant_bin] - start_indices[insignificant_bin]);
            _dprintf("after removing insignificant_bin %d, eviction set superset size = %ld, reserved set size = %ld\n", 
                    insignificant_bin, eviction_set_superset->num_cache_lines, reserved_cache_line_set->num_cache_lines);
        }
        else {
            // we cannot find any insignificant bin. This means all combination of bins cannot
            // constitute an eviction set. This is very likely that we removed some crucial elements
            // in a previous step. Thus we add the cache lines back from reserved_cache_line_set
            // until the eviction set size is doubled
            shuffle_cache_line_set(reserved_cache_line_set);

            int num_reserved_cache_lines_to_add = eviction_set_superset->num_cache_lines;

            _dprintf("before poping reserved: %ld\n", reserved_cache_line_set->num_cache_lines);
            for (int i = 0; i < num_reserved_cache_lines_to_add && reserved_cache_line_set->num_cache_lines > 0; i++) {
                size_t reserved_cache_line = pop_cache_line_from_set(reserved_cache_line_set);
                push_cache_line_to_set(eviction_set_superset, reserved_cache_line);
            }
            _dprintf("after poping reserved: %ld\n", reserved_cache_line_set->num_cache_lines);

            _dprintf("after adding reserved lines. eviction set superset size = %ld\n", eviction_set_superset->num_cache_lines);
        }

        // After we removed a bin, test the victim address again and see if the rest is an eviction set
        int timing = evict_and_time(victim_addr, eviction_set_superset);
        evicted = timing >= threshold;
        _dprintf("after removing insignificant_bin, timing = %d, evicted = %d\n", timing, evicted);
    }

    delete_cache_line_set(reserved_cache_line_set);
}

cache_line_set_t* find_L2_eviction_set_using_timer(uint8_t* victim_addr) {

    cache_line_set_t* evset_cache_lines;

    uint64_t page_offset = (uint64_t)victim_addr % PAGE_SIZE;
    allocator_t *allocator = create_allocator(page_offset, PAGE_SIZE);
    set_allocator_mode(allocator, ALLOC_REG_PAGES);

    // find the eviction set for victim_addr
    while (1) {
        evset_cache_lines = build_l2_eviction_set_superset(allocator, victim_addr, L2_MISS_MIN_LATENCY);

        // print the measured time when using the superset to evict victim_addr
        int timing_evicted = evict_and_time(victim_addr, evset_cache_lines);
        printf("try evicting %p with generated eviction set superset (size=%d): latency = %d\n", victim_addr, evset_cache_lines->num_cache_lines, timing_evicted);

        // reduce the superset into L2_NWAYS number of cache lines
        reduce_l2_eviction_set_superset(evset_cache_lines, victim_addr, L2_MISS_MIN_LATENCY);

        // test the reduced eviction set. We try 2 different orders to make sure no error
        float l2_evict_rate = P1S1P1_timer(RET_L2_EVRATE, victim_addr, evset_cache_lines);

        // we need both eviction rate to be > 99%
        if (l2_evict_rate > 0.99) {
            printf("\t %s found a good eviction set for P+P.\n", __func__);
            break;
        }
        else {
            printf("\t %s eviction rate doesn't meet the bar (0.99). Retry.\n", __func__);
            delete_cache_line_set(evset_cache_lines);
        }
    }

    return evset_cache_lines;
}

cache_line_set_t* find_L2_congruent_cache_lines_using_llsc(uint8_t* writable_victim_addr) {

    float max_l2_evict_rate = 0.0;
    int congruent_hpo = 0;
    cache_line_set_t* l2_congruent_cache_lines = NULL;

    uint64_t page_offset = (uint64_t)writable_victim_addr % PAGE_SIZE;
    allocator_t *allocator = create_allocator(page_offset, PAGE_SIZE);
    set_allocator_mode(allocator, ALLOC_HUGE_PAGES);

    int found = 0;
    for (int candidate_hpo = 0x0; candidate_hpo < 0x40; candidate_hpo++) {
        FILE* fp = open_database_file(candidate_hpo);

        cache_line_set_t* candidate_l2_congruent_cache_lines = build_empty_cache_line_set(allocator);

        // read the database file to craft the large conflict set
        int conflict_hpo = 0;
        while (fscanf(fp, "%x", &conflict_hpo) != EOF) {
            uint64_t candidate_congruent_addr = allocator->base_addr + conflict_hpo * PAGE_SIZE + RPO((uint64_t)writable_victim_addr) * L2_LINE_SIZE + CLO((uint64_t)writable_victim_addr);
            push_cache_line_to_set(candidate_l2_congruent_cache_lines, candidate_congruent_addr);
        }

        // the following assumes that victim_addr is writable.
        // without this assumption, the "stxr victim_addr" in probe step will fault
        // this is in most cases not applicable, because victim_addr is usually readable at best (e.g., data in shared library)
        float l2_evict_rate_llsc = P1S2P1_llsc(writable_victim_addr, reduce_cache_line_set(candidate_l2_congruent_cache_lines, 16));

        if (l2_evict_rate_llsc > 0.5) {
            found = 1;
            congruent_hpo = candidate_hpo;
            l2_congruent_cache_lines = candidate_l2_congruent_cache_lines;
            break;
        }
    }

    if (found) {
        // found the eviction set
        printf("Found the L2 congruent cache line set using LLSC only for addr %p: HPO = 0x%x, evict_rate = %f\n", writable_victim_addr, congruent_hpo, max_l2_evict_rate);
    }
    else {
        fprintf(stderr, "Didn't find the L2 congruent cache lines.\n");
        exit(1);
    }

    return l2_congruent_cache_lines;
}

float get_pivot_addr_l2_evset_score(uint8_t* pivot_addr, cache_line_set_t* l2_evset_cache_lines_reduced, uint8_t* victim_addr) {

    uint8_t* victim_addr_diff_l2 = victim_addr + L2_LINE_SIZE;

    float P1S1Y2S1P1_victim_right_l2set_llsc_evict_rate = P1S1Y2S1P1_llsc(pivot_addr, l2_evset_cache_lines_reduced, victim_addr);
    float P1S1Y2S1P1_victim_wrong_l2set_llsc_evict_rate = P1S1Y2S1P1_llsc(pivot_addr, l2_evset_cache_lines_reduced, victim_addr_diff_l2);

    return P1S1Y2S1P1_victim_right_l2set_llsc_evict_rate - P1S1Y2S1P1_victim_wrong_l2set_llsc_evict_rate;
}

cache_line_set_t* find_L2_eviction_set_using_llsc(uint8_t* writable_victim_addr) {

    num_counts = 10;

    // the goal is just find an L2 eviction set which can evict victim_addr
    // and a 13-entry eviction set works better than 12-entry eviction set
    int evset_reduced_size = L2_NWAYS + 1;
    int num_congruent_addrs_share_l1set_with_victim_addr = evset_reduced_size / 2;

    // first, find the 32-element L2 eviction set using LLSC
    cache_line_set_t* l2_evset_cache_lines = find_L2_congruent_cache_lines_using_llsc(writable_victim_addr);

    assert (l2_evset_cache_lines->num_cache_lines >= evset_reduced_size);

    // second, find the best 13-element L2 eviction set that absolutely evict victim_addr
    for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {
        shuffle_cache_line_set(l2_evset_cache_lines);

        cache_line_set_t* l2_evset_cache_lines_reduced = reduce_cache_line_set(l2_evset_cache_lines, evset_reduced_size);

        // we keep the first 6 addresses to be within the same L1 set as victim_addr
        // and flip the rest (starting from the 7-th) addresses to the other L2 half
        for (int i = num_congruent_addrs_share_l1set_with_victim_addr; i < l2_evset_cache_lines_reduced->num_cache_lines; i++)
            l2_evset_cache_lines_reduced->cache_lines[i] = flip_l2_offset(l2_evset_cache_lines_reduced->cache_lines[i]);

        float evict_rate = P1S2P1_llsc(writable_victim_addr, l2_evset_cache_lines_reduced);

        if (evict_rate > 0.95) {
            int good_evset = 1;

            // verify the results: check 10 more times
            int num_reps = 10;
            float total_rate = 0.0;
            for (int j = 0; j < num_reps; j++) {
                flush_cache();
                float rep_rate = P1S2P1_llsc(writable_victim_addr, l2_evset_cache_lines_reduced);
                total_rate += rep_rate;
                if (rep_rate < 0.95) {
                    good_evset = 0;
                    break;
                }
            }

            if (good_evset) {
                printf("Found the best L2 eviction set from another core, size = %d, evict rate = %f\n", evset_reduced_size, total_rate / num_reps);
                return l2_evset_cache_lines_reduced;
            }
        }
    }

    printf("ERROR: %s cannot find a optimal L2 eviction set. Please re-run or increase num_shuffles.\n", __func__);
    assert (0);
}

addr_cache_lines_pair_t* find_L2_eviction_set_and_pivot_addr_using_llsc(uint8_t* readonly_victim_addr) {

    num_counts = 100;
    num_shuffles = 100;

    uint64_t page_offset = (uint64_t)readonly_victim_addr % PAGE_SIZE;
    allocator_t *allocator = create_allocator(page_offset, PAGE_SIZE);
    set_allocator_mode(allocator, ALLOC_HUGE_PAGES);

    for (int candidate_hpo = 0x0; candidate_hpo < 0x40; candidate_hpo++) {
        FILE* fp = open_database_file(candidate_hpo);

        cache_line_set_t* candidate_l2_congruent_cache_lines = build_empty_cache_line_set(allocator);

        // read the database file to craft the large conflict set
        int conflict_hpo = 0;
        while (fscanf(fp, "%x", &conflict_hpo) != EOF) {
            uint64_t candidate_congruent_addr = allocator->base_addr + conflict_hpo * PAGE_SIZE + RPO((uint64_t)readonly_victim_addr) * L2_LINE_SIZE + CLO((uint64_t)readonly_victim_addr);
            push_cache_line_to_set(candidate_l2_congruent_cache_lines, candidate_congruent_addr);
        }

        // try to find if this congruent cache line set contains the eviction set and pivot addr
        for (int shuffle = 0; shuffle < num_shuffles; shuffle++) {

            shuffle_cache_line_set(candidate_l2_congruent_cache_lines);

            cache_line_set_t* candidate_l2_congruent_cache_lines_reduced = reduce_cache_line_set(candidate_l2_congruent_cache_lines, L2_NWAYS);

            uint8_t* pivot_addr = (uint8_t*)pop_cache_line_from_set(candidate_l2_congruent_cache_lines_reduced);

            // we keep the first 6 addresses in eviction set to be within the same L1 set as pivot_addr.
            // and flip the rest (starting from the 7-th) addresses to the other L2 half
            // in this case, all eviction set addresses will be cached in both L1 and L2
            for (int i = num_congruent_addrs_share_l1set_with_pivot_addr; i < candidate_l2_congruent_cache_lines_reduced->num_cache_lines; i++)
                candidate_l2_congruent_cache_lines_reduced->cache_lines[i] = flip_l2_offset(candidate_l2_congruent_cache_lines_reduced->cache_lines[i]);

            float evset_score = get_pivot_addr_l2_evset_score(pivot_addr, candidate_l2_congruent_cache_lines_reduced, readonly_victim_addr);

            if (evset_score > 0.95) {
                int good_evset = 1;

                // verify the results: check 10 more times. The score (good 0%-100% split) must be the same across all 10 repetitions
                int num_reps = 10;
                float total_score = 0.0;
                for (int j = 0; j < num_reps; j ++) {
                    flush_cache();
                    float rep_score = get_pivot_addr_l2_evset_score(pivot_addr, candidate_l2_congruent_cache_lines_reduced, readonly_victim_addr);
                    total_score += rep_score;
                    if (rep_score < 0.95) {
                        good_evset = 0;
                        break;
                    }
                }

                // return the results if we get a good_evset
                if (good_evset) {
                    addr_cache_lines_pair_t* pivot_addr_and_evset_cache_lines = (addr_cache_lines_pair_t*)malloc(sizeof(addr_cache_lines_pair_t));

                    pivot_addr_and_evset_cache_lines->addr = pivot_addr;
                    pivot_addr_and_evset_cache_lines->cache_lines = candidate_l2_congruent_cache_lines_reduced;
                    pivot_addr_and_evset_cache_lines->extra_addr = (uint8_t*)candidate_l2_congruent_cache_lines->cache_lines[L2_NWAYS];
                    printf("Found the best L2 eviction set ordering and pivot address, score = %f\n", total_score / num_reps);

                    return pivot_addr_and_evset_cache_lines;
                }
            }
        }
    }

    printf("ERROR: %s cannot find a optimal L2 eviction set ordering. Please re-run or increase num_shuffles.\n", __func__);
    assert (0);
}
