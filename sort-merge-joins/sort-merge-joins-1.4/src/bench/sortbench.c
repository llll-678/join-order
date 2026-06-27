/**
 * @file    sortbench.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Tue Dec 18 23:20:10 2012
 * @version $Id $
 *
 * @brief   A microbenchmark for single-threaded sorting routines.
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */
#include <stdio.h>
#include <sys/time.h>           /* gettimeofday */
#include <stdlib.h>             /* qsort */
#include <string.h>             /* memcpy */

#if defined(__cplusplus)
#include <algorithm>            /* sort() */
#endif

#include "testutil.h" /* is_sorted_tuples() */
#include "affinity.h"
#include "types.h" /* relation_t, tuple_t */
#include "generator.h"
#include "avxsort.h"
#include "avxsort_multiway.h"

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

/** A size greater than the L3 cache for cooling the L3 cache */
#define MAX_L3_CACHE_SIZE (24*1024*1024)

#ifndef L2_CACHE_SIZE
#define L2_CACHE_SIZE (256*1024)
#endif

/** Number of tuples that can fit into L2 cache divided by 2 */
#ifndef BLOCKSIZE
#define BLOCKSIZE (L2_CACHE_SIZE / (2 * sizeof(tuple_t)))
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

void
print_timing2(uint64_t numtuples, struct timeval * start, struct timeval * end,
             FILE * out)
{
    double diff_usec = (((*end).tv_sec*1000000L + (*end).tv_usec)
                        - ((*start).tv_sec*1000000L+(*start).tv_usec));
    fprintf(stdout, "\nMY-TOTAL-TIME-USECS3: %.4lf \n", diff_usec);
    fprintf(out, "NUM-TUPLES = %lld TOTAL-TIME-USECS = %.4lf ", numtuples, diff_usec);
    fprintf(out, "TUPLES-PER-SECOND = ");
    fflush(out);

    fprintf(out, "%.4lf ", (numtuples/(diff_usec/1000000L)));
    fflush(out);
}

static inline int __attribute__((always_inline))
tuplekeycmp(const void * k1, const void * k2)
{
    int val = ((tuple_t *)k1)->key - ((tuple_t *)k2)->key;
    return val;
}

/**
 * @addtogroup MicroBenchmarks
 * Microbenchmarks for single-thread AVX sort routines.
 * @{
 */

/**
 * A microbenchmark for different single-threaded AVX sort routines.
 * @param ntuples
 * @param what
 * @return
 */
int
bench_avxsort(uint32_t ntuples,
              int what /* 0: avxsort() , 1: avxsort_multiwaymerge(),
                          2: avxsort_aligned() */)
{
    struct timeval start, end;
    relation_t rel, outrel, relcpy, outrel2;
    tuple_t * dummybuffer = (tuple_t *) malloc_aligned(MAX_L3_CACHE_SIZE);

    outrel.num_tuples = ntuples;
    posix_memalign((void**)&outrel.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));
    outrel2.num_tuples = ntuples;
    posix_memalign((void**)&outrel2.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

    fprintf(stdout,
            "[INFO ] Creating relation with size = %.3lf MiB, #tuples = %d : ",
            (double) sizeof(tuple_t) * ntuples/1024.0/1024.0,
            ntuples);
    seed_generator(12345);
    create_relation_pk(&rel, ntuples);
    /* parallel_create_relation(&rel, ntuples, 1, ntuples); */
    fprintf(stdout, " OK\n");
    fflush(stdout);

    /* What about qsort() and std::sort() ? */
    relcpy.num_tuples = ntuples;
    posix_memalign((void**)&relcpy.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

#if 0
    memcpy(relcpy.tuples, rel.tuples, ntuples * sizeof(tuple_t));
    gettimeofday(&start, NULL);
    qsort (relcpy.tuples, ntuples, sizeof (int64_t), tuplekeycmp);
    gettimeofday(&end, NULL);
    fprintf(stdout, "[INFO ] C stdlib qsort() ==> ");
    print_timing2(ntuples, &start, &end, stdout);
#endif

#if defined(__cplusplus)
    /* C++ std::sort() */
    memcpy(relcpy.tuples, rel.tuples, ntuples * sizeof(tuple_t));
    gettimeofday(&start, NULL);
    std::sort((int64_t *) relcpy.tuples, (int64_t *)(relcpy.tuples + ntuples));
    gettimeofday(&end, NULL);
    fprintf(stdout, "[INFO ] C++ std::sort()  ==> ");
    print_timing2(ntuples, &start, &end, stdout);
    fprintf(stdout, ", ");
#endif

    free(relcpy.tuples);

    /* cool-down the caches */
    intkey_t garbage = 0xdeadbeef;
    for(int i = 0; i < (MAX_L3_CACHE_SIZE/sizeof(tuple_t)); i++){
        garbage += dummybuffer[i].key;
        dummybuffer[i].key = garbage;
    }

    /* AVX sort */
    fprintf(stdout, "[INFO ] Running single core AVX sort...\n");
#ifdef PERF_COUNTERS
    PCM_initPerformanceMonitor("pcm.cfg", NULL);
    PCM_start();
#endif

    gettimeofday(&start, NULL);

    if(what == 0 || what == 2){
        /* chooses aligned version depending on input alignment */
        avxsort_tuples(&rel.tuples, &outrel.tuples, ntuples);
    }
    else if(what == 1){
        /* if size is larger than L3-size; then uses multi-way merging */
        avxsortmultiway_tuples(&rel.tuples, &outrel.tuples, ntuples);
    }

    gettimeofday(&end, NULL);

#ifdef PERF_COUNTERS
    PCM_stop();
    PCM_log("========== Profiling results ==========\n");
    PCM_printResults();
    PCM_cleanup();
#endif

    char impl[256];

    if(what == 0)
        sprintf(impl, "%s", "avxsort()");
    else if(what == 1)
        sprintf(impl, "%s", "avxsort_multiwaymerge()");
    else if(what == 2)
        sprintf(impl, "%s", "avxsort_aligned()");
    else
        sprintf(impl, "%s", "none()");

    fprintf(stdout, "[INFO ] %s ==> ", impl);
    print_timing2(ntuples, &start, &end, stdout);
    fprintf(stdout, "\n");

    int rv = 1;

    if(is_sorted_tuples(outrel.tuples, outrel.num_tuples)) {
        fprintf(stdout, "[INFO ] Output relation is now sorted. (garbage=%d)\n", garbage%4);
    }
    else {
        fprintf(stdout, "[ERROR] Output relation is not sorted.\n");
        rv = 0;
    }


    /* clean-up temporary space */
    free(rel.tuples);
    free(outrel.tuples);
    free(dummybuffer);

    return rv;
}

/** @} */

/** Benchmark sorting over numa spread data by using numactl [options] */
int
bench_sort_over_numa()
{
    struct timeval start, end;
    relation_t rel, relcpy, outrel;
    int32_t ntuples = 50000000;
    int rv = 1;

    outrel.num_tuples = ntuples;
    posix_memalign((void**)&outrel.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

    fprintf(stdout,
            "[INFO ] Creating relation with size = %.3lf MiB, #tuples = %d : ",
            (double) sizeof(tuple_t) * ntuples/1024.0/1024.0,
            ntuples);
    fflush(stdout);
    seed_generator(12345);
    create_relation_pk(&rel, ntuples);
    fprintf(stdout, " OK\n");

    /* What about qsort() and std::sort() ? */
    relcpy.num_tuples = ntuples;
    posix_memalign((void**)&relcpy.tuples, 64, ntuples * sizeof(tuple_t));
    memcpy(relcpy.tuples, rel.tuples, ntuples * sizeof(tuple_t));
    gettimeofday(&start, NULL);
    qsort (relcpy.tuples, ntuples, sizeof (tuple_t), tuplekeycmp);
    gettimeofday(&end, NULL);
    fprintf(stdout, "[INFO ] C stdlib qsort() ==> ");
    print_timing2(ntuples, &start, &end, stdout);


#if defined(__cplusplus)
    /* C++ std::sort() */
    memcpy(relcpy.tuples, rel.tuples, ntuples * sizeof(tuple_t));
    printf("[INFO ] Press enter to start C++ sort()...\n");getchar();
    gettimeofday(&start, NULL);
    std::sort((int64_t *) relcpy.tuples, (int64_t *)(relcpy.tuples + ntuples));
    gettimeofday(&end, NULL);
    fprintf(stdout, "[INFO ] C++ std::sort()  ==> ");
    print_timing2(ntuples, &start, &end, stdout);
#endif
    free(relcpy.tuples);


    /* clean-up temporary space */
    free(rel.tuples);
    free(outrel.tuples);

    return rv;
}

/**
 * Benchmark Multi-Way Merge
 *
 * Arguments:
 * [1]: number of tuples in 2^20;
 * [2]: 0=>avxsort(), 1=>avxsort_multiwaymerge(), 2:
 * [3]: is number of tuples a power of 2 ? 0 or 1
 */
int
main(int argc, char ** argv)
{
    /* start initially on CPU-0 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) <0) {
        perror("sched_setaffinity");
    }
    if(argc != 4){
        printf("[INFO ] Usage: %s [numtuples in 2^20] [0=>avxsort(), 1=>avxsort_multiwaymerge(), 2=>aligned] [numtuples pow2?]\n",
               argv[0]);
        return 0;
    }

    int seed = 20121984;/*time(NULL);*/
    fprintf(stdout, "[INFO ] seed = %d\n", seed);
    srand(seed);

    int32_t ntuples = 1024*1024;
    int what = 0; /* 0: avxsort() , 1: avxsort_multiwaymerge(), 2: */

    ntuples *= atoi(argv[1]);
    what = atoi(argv[2]);
    if(atoi(argv[3]) == 0)
        ntuples +=  rand() % BLOCKSIZE;

    bench_avxsort(ntuples, what);

    /* numa-bench */
#if 0
    fprintf(stdout, "[INFO ] Sorting over NUMA-spread data benchmark...\n");
    bench_sort_over_numa();
    fprintf(stdout, "[INFO ] ----------------------\n");
#endif

    return 0;
}
