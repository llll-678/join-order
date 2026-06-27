/**
 * @file    multiwaymergebench.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Tue Dec 18 20:20:10 2012
 * @version $Id $
 *
 * @brief   A microbenchmark for multi-way merging routines.
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */
#include <stdio.h>
#include <sys/time.h>           /* gettimeofday */
#include <stdlib.h>             /* qsort */
#include <string.h>             /* memcpy */

#include "affinity.h"
#include "testutil.h"           /* malloc_aligned() */
#include "avx_multiwaymerge.h"  /* avx_multiway_merge() */
#include "scalar_multiwaymerge.h"  /* scalar_multiway_merge() */

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

/** A size greater than the L3 cache for cooling the L3 cache */
#define MAX_L3_CACHE_SIZE (24*1024*1024)

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

/** Cache line aligned memory allocation method */
extern void *
malloc_aligned(size_t sz);

/**
 * @addtogroup MicroBenchmarks
 * Microbenchmarks for 2-way AVX merging kernels of different size.
 * @{
 */

/**
 * A microbenchmark for multi-way merging routines.
 *
 * @param chunksize size of each run to merge
 * @param fanIn fan-in of multiway merging
 * @param L3buffersize size of L3 size reserved for merging buffers
 */
void
bench_multiwaymerge(uint32_t chunksize, uint32_t fanIn, uint32_t L3buffersize)
{
    struct timeval start, end;
    relation_t outrel;

    uint64_t ntuples = chunksize * fanIn;

    relation_t chunks[fanIn];
    relation_t chunks_cpy[fanIn];

    uint64_t i;
    int seed = time(NULL);
    srand(seed);

    for(i = 0; i < fanIn; i++){
        chunks[i].num_tuples = chunksize;
        posix_memalign((void**)&chunks[i].tuples, CACHE_LINE_SIZE, chunksize*sizeof(tuple_t));
        chunks[i].tuples = generate_rand_ordered_tuples(chunksize);


        chunks_cpy[i].num_tuples = chunksize;
        chunks_cpy[i].tuples = chunks[i].tuples;
    }

    relation_t * chunkptrs[fanIn];

    for(i = 0; i < fanIn; i++) {
        chunkptrs[i] = &chunks[i];
    }

    uint32_t bufntuples = L3buffersize/sizeof(tuple_t);
    tuple_t * fifobuffer = (tuple_t *) malloc_aligned(L3buffersize);

    tuple_t * dummybuffer = (tuple_t *) malloc_aligned(MAX_L3_CACHE_SIZE);

    outrel.num_tuples = ntuples;
    posix_memalign((void**)&outrel.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

    /* cool-down the caches */
    intkey_t garbage = 0;
    for(i = 0; i < (MAX_L3_CACHE_SIZE/sizeof(tuple_t)); i++){
        garbage += dummybuffer[i].key;
        dummybuffer[i].key = garbage;
    }

    /* AVX Multi-way Merge */
    fprintf(stderr, "[INFO ] Running single core AVX Multi-Way Merge ...\n");
#ifdef PERF_COUNTERS
    PCM_initPerformanceMonitor("pcm.cfg", NULL);
    PCM_start();
#endif

    gettimeofday(&start, NULL);

    avx_multiway_merge(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);

    gettimeofday(&end, NULL);

#ifdef PERF_COUNTERS
    PCM_stop();
    PCM_log("========== Multiway-Merge Profiling results ==========\n");
    PCM_printResults();
#endif

    if(is_sorted_tuples(outrel.tuples, outrel.num_tuples)) {
        fprintf(stderr, "[INFO ] Output relation is now sorted. (%d)\n", garbage);
    }
    else {
        fprintf(stderr, "[ERROR] Output relation is not sorted. (%d)\n", garbage);
    }


    char impl[256];

    sprintf(impl, "%s", "avx_multiway_merge()");
    double diff_usec = ((end.tv_sec*1000000L + end.tv_usec)
                        - (start.tv_sec*1000000L + start.tv_usec));
    double tput = (ntuples*1000000L/diff_usec);

    free(outrel.tuples);

    /* Scalar multi-way merge */
    posix_memalign((void**)&outrel.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));
    for(i = 0; i < fanIn; i++){
        chunks[i] = chunks_cpy[i];
    }

    /* cool-down the caches */
    for(i = 0; i < (MAX_L3_CACHE_SIZE/sizeof(tuple_t)); i++){
        garbage += dummybuffer[i].key;
        dummybuffer[i].key = garbage;
    }

    fprintf(stderr, "[INFO ] Running single core scalar Multi-Way Merge ...\n");

#ifdef PERF_COUNTERS
    PCM_start();
#endif

    gettimeofday(&start, NULL);

    scalar_multiway_merge(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);

    gettimeofday(&end, NULL);

#ifdef PERF_COUNTERS
    PCM_stop();
    PCM_log("========== Scalar-Multiway-Merge Profiling results ==========\n");
    PCM_printResults();
#endif


    if(is_sorted_tuples(outrel.tuples, outrel.num_tuples)) {
        fprintf(stderr, "[INFO ] Output relation is now sorted. (%d)\n", garbage);
    }
    else {
        fprintf(stderr, "[ERROR] Output relation is not sorted. (%d)\n", garbage);
    }

    sprintf(impl, "%s", "scalar_multiway_merge()");
    double scalar_diff_usec = ((end.tv_sec*1000000L + end.tv_usec)
                        - (start.tv_sec*1000000L + start.tv_usec));
    double scalar_tput = (ntuples*1000000L/scalar_diff_usec);

    free(outrel.tuples);

    /* Just plain memcpy() instead of merge */
    relation_t relcpy;
    uint64_t offset = 0;
    posix_memalign((void**)&relcpy.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

    /* cool-down the caches */
    for(i = 0; i < (MAX_L3_CACHE_SIZE/sizeof(tuple_t)); i++){
        garbage += dummybuffer[i].key;
        dummybuffer[i].key = garbage;
    }

#ifdef PERF_COUNTERS
    PCM_start();
#endif

    gettimeofday(&start, NULL);
    for(i = 0; i < fanIn; i++) {
        memcpy(relcpy.tuples + offset, chunks_cpy[i].tuples,
               chunks_cpy[i].num_tuples * sizeof(tuple_t));
        offset += chunks_cpy[i].num_tuples;
    }
    gettimeofday(&end, NULL);

#ifdef PERF_COUNTERS
    PCM_stop();
    PCM_log("========== Multiway-Merge Memcpy() Profiling results ==========\n");
    PCM_printResults();
    PCM_cleanup();
#endif

    double memcpy_diff_usec = ((end.tv_sec*1000000L + end.tv_usec)
                        - (start.tv_sec*1000000L + start.tv_usec));
    double memcpy_tput = (ntuples*1000000L/memcpy_diff_usec);


    fprintf(stderr, "[INFO ] %s ==> %d\n", impl, garbage);
    fprintf(stderr,"#NCHUNKS BUFSIZE CHUNKSIZE AVX-MWAY-TIME(usecs) AVX-MWAY-TPUT AVX-MWAY-MB/s SCALAR-TIME(usecs) SCALAR-TPUT SCALAR-MB/s MEMCPY-TIME(usecs) MEMCPY-TPUT MEMCPY-MB/s\n");
    fflush(stderr);
    fprintf(stdout, "%d %lu %d ", fanIn, bufntuples*sizeof(tuple_t), chunksize);
    fprintf(stdout, "%.2lf %.2lf %.2lf %.2lf %.2lf %.2lf %.2lf %.2lf %.2lf\n",
            diff_usec, tput, tput*2*sizeof(tuple_t)/1024.0/1024.0,
            scalar_diff_usec, scalar_tput, scalar_tput*2*sizeof(tuple_t)/1024.0/1024.0,
            memcpy_diff_usec, memcpy_tput, memcpy_tput*2*sizeof(tuple_t)/1024.0/1024.0);
    fflush(stdout);

    /* clean-up temporary space */
    free(relcpy.tuples);
    free(fifobuffer);
    free(dummybuffer);
    for(i = 0; i < fanIn; i++){
        free(chunks_cpy[i].tuples);
    }
}

/** @} */

/** Benchmark Multi-Way Merge */
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

    int seed = time(NULL);
    fprintf(stderr, "[INFO ] seed = %d\n", seed);
    srand(seed);

    uint32_t chunksize, fanIn, L3buffersize;

    if(argc != 4){
        printf("[INFO ] Usage: %s [chunksize(numtuples)] [fanIn] [L3buffersize(bytes)]\n",
               argv[0]);
        return 0;
    }

    chunksize    = atoi(argv[1]);
    fanIn        = atoi(argv[2]);
    L3buffersize = atoi(argv[3]);

    bench_multiwaymerge(chunksize, fanIn, L3buffersize);

    return 0;
}
