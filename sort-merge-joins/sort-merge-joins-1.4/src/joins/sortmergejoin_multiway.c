/**
 * @file    sortmergejoin_multiway.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:37:54 2012
 * @version $Id $
 *
 * @brief   m-way sort-merge-join algorithm with multi-way merging.
 *          It uses AVX-based sorting and merging if scalarsort and scalarmerge
 *          flags are not provided.
 *
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */

#include <stdlib.h> /* malloc() */
#include <math.h>   /* log2(), ceil() */

#include "rdtsc.h"              /* startTimer, stopTimer */
#include "barrier.h"            /* pthread_barrier_* */
#include "cpu_mapping.h"        /* cpu_id NUMA related methods */

#include "sortmergejoin_multiway.h"
#include "joincommon.h"
#include "partition.h"  /* partition_relation_optimized() */
#include "avxsort.h"    /* avxsort_tuples() */
#include "scalarsort.h" /* scalarsort_tuples() */
#include "avx_multiwaymerge.h"         /* avx_multiway_merge() */
#include "scalar_multiwaymerge.h"      /* scalar_multiway_merge() */

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

/** Number of tuples that fits into a single cache line */
#define TUPLESPERCACHELINE (CACHE_LINE_SIZE/sizeof(tuple_t))

/** Align N to number of tuples that is a multiple of cache lines */
#define ALIGN_NUMTUPLES(N) ((N+TUPLESPERCACHELINE-1) & ~(TUPLESPERCACHELINE-1))

/**
 * Determines the size of the multi-way merge buffer.
 * Ideally, it should match the size of the L3 cache.
 * @note this buffer is shared by active nr. of threads in a NUMA-region.
 */
#ifndef MWAY_MERGE_BUFFER_SIZE
#define MWAY_MERGE_BUFFER_SIZE (4*1024*1024) /* in bytes */
#endif

/**
 * Main thread of First Sort-Merge Join variant with partitioning and complete
 * sorting of both input relations. The merging step in this algorithm tries to
 * overlap the first merging and transfer of remote chunks. However, in compared
 * to other variants, merge phase still takes a significant amount of time.
 *
 * @param param parameters of the thread, see arg_t for details.
 *
 */
void *
sortmergejoin_multiway_thread(void * param);

result_t *
sortmergejoin_multiway(relation_t * relR, relation_t * relS, int nthreads)
{
    return sortmergejoin_initrun(relR, relS, nthreads,
                                 sortmergejoin_multiway_thread);
}

/**
 * TODO: FIXME
 * Various NUMA shuffling strategies as also described by NUMA-aware
 * shuffling paper.
 */
//#define NUMA_SHUFFLE_RANDOM 1
//#define NUMA_SHUFFLE_RING 1
int
numa_shuffle_strategy(int my_tid, int i, int nthreads)
{
#ifdef NUMA_SHUFFLE_RANDOM
    tuple_t select[64];
    relation_t ss;
    ss.tuples = (tuple_t*)select;
    ss.num_tuples = nthreads;
    for(int s=0; s < nthreads; s++)
      ss.tuples[s].key = s;
        knuth_shuffle(&ss);


    return select[i].key;
#elif defined NUMA_SHUFFLE_RING
        static int numa[64] = {
            0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
            1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61,
            2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 62,
            3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63 };
        return (numa[my_tid] + i) % nthreads; // --> NUMA-SHUFF-RING
#else
    return (my_tid + i) % nthreads; // --> NUMA-SHUFF-NEXT-THR
#endif
}

/**
 * Main execution thread of "m-way" sort-merge join.
 *
 * @param param
 */
void *
sortmergejoin_multiway_thread(void * param)
{
    arg_t * args   = (arg_t*) param;
    int32_t my_tid = args->my_tid;
    int i, rv;

    relation_t relR, relS;
    relation_t tmpR, tmpS;

    relR.tuples     = args->relR;
    relR.num_tuples = args->numR;
    relS.tuples     = args->relS;
    relS.num_tuples = args->numS;
    tmpR.tuples     = args->tmpR;
    tmpR.num_tuples = args->numR;
    tmpS.tuples     = args->tmpS;
    tmpS.num_tuples = args->numS;

    DEBUGMSG(1, "Thread-%d started running ... \n", my_tid);
#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_initPerformanceMonitor(NULL, NULL);
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        gettimeofday(&args->start, NULL);
        startTimer(&args->part);
        startTimer(&args->sort);
        startTimer(&args->mergedelta);
        startTimer(&args->merge);
        startTimer(&args->join);
    }

#if 1
    relation_t * partsR[PARTFANOUT];
    relation_t * partsS[PARTFANOUT];

    for(i = 0; i < PARTFANOUT; i++) {
        partsR[i] = (relation_t *) malloc(sizeof(relation_t));
        partsS[i] = (relation_t *) malloc(sizeof(relation_t));
    }

    /** Step.1) NUMA-local partitioning. */
    /* after partitioning tmpR, tmpS holds the partitioned data */
    int bitshift = ceil(log2(relR.num_tuples * args->nthreads));
    if(args->nthreads == 1)
        bitshift = bitshift - NRADIXBITS + 1;
    else
        bitshift = bitshift - NRADIXBITS - 2;

    /* printf("[INFO ] bitshift = %d\n", bitshift); */
    partition_relation_optimized(partsR, &relR, &tmpR, NRADIXBITS, bitshift);
    partition_relation_optimized(partsS, &relS, &tmpS, NRADIXBITS, bitshift);

#else
    /** Step.1) NUMA-local partitioning. */
    relation_t ** partsR = NULL;
    relation_t ** partsS = NULL;
    partition_relations(&partsR, &partsS, &relR, &relS, &tmpR, &tmpS);
#endif

#ifdef PERF_COUNTERS
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("========= 1) Profiling results of Partitioning Phase =========\n");
        PCM_printResults();
    }
#endif

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->part);
    }

#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    /** Step.2) NUMA-local sorting of cache-sized chunks */
    args->threadrelchunks[my_tid] = (relationpair_t *)
                                  malloc(PARTFANOUT * sizeof(relationpair_t));

    uint64_t ntuples_per_part;
    uint64_t offset = 0;
    tuple_t * optr = relR.tuples + my_tid * PADDINGX8;
    for(i = 0; i < PARTFANOUT; i++) {
        tuple_t * inptr  = (partsR[i]->tuples);
        tuple_t * outptr = (optr + offset);
        ntuples_per_part       = partsR[i]->num_tuples;
        offset                += ALIGN_NUMTUPLES(ntuples_per_part);

        DEBUGMSG(1, "PART-%d-SIZE: %"PRIu64"\n", i, partsR[i]->num_tuples);

        if(scalarsortflag)
            scalarsort_tuples(&inptr, &outptr, ntuples_per_part);
        else
            avxsort_tuples(&inptr, &outptr, ntuples_per_part);

        /*
        if(!is_sorted_helper((int64_t*)outptr, ntuples_per_part)){
            printf("===> %d-thread -> R is NOT sorted, size = %d\n", my_tid,
                   ntuples_per_part);
        }
        */

        args->threadrelchunks[my_tid][i].R.tuples     = outptr;
        args->threadrelchunks[my_tid][i].R.num_tuples = ntuples_per_part;
    }

    offset = 0;
    optr = relS.tuples + my_tid * PADDINGX8;
    for(i = 0; i < PARTFANOUT; i++) {
        tuple_t * inptr  = (partsS[i]->tuples);
        tuple_t * outptr = (optr + offset);

        ntuples_per_part       = partsS[i]->num_tuples;
        offset                += ALIGN_NUMTUPLES(ntuples_per_part);
        /*
        if(my_tid==0)
             fprintf(stdout, "PART-%d-SIZE: %d\n", i, partsS[i]->num_tuples);
        */
        if(scalarsortflag)
            scalarsort_tuples(&inptr, &outptr, ntuples_per_part);
        else
            avxsort_tuples(&inptr, &outptr, ntuples_per_part);

        /*
        if(!is_sorted_helper((int64_t*)outptr, ntuples_per_part)){
            printf("===> %d-thread -> S is NOT sorted, size = %d\n",
            my_tid, ntuples_per_part);
        }
        */

        args->threadrelchunks[my_tid][i].S.tuples = outptr;
        args->threadrelchunks[my_tid][i].S.num_tuples = ntuples_per_part;
        /* if(my_tid == 0) */
        /* printf("S-MYTID=%d FAN=%d OUT-START=%llu\nS-MYTID=%d FAN=%d OUT-END=%llu\n", */
        /*        my_tid, i, outptr, my_tid, i, (outptr+ntuples_per_part)); */

    }

    /**
     * Allocate shared merge buffer for multi-way merge tree.
     * This buffer is further divided into given number of threads
     * active in the same NUMA-region.
     */
    int numaregionid = get_numa_region_id(my_tid);
    if(is_first_thread_in_numa_region(my_tid)) {
        /* first thread in each numa region allocates a shared L3 buffer */
        tuple_t * sharedmergebuffer = (tuple_t *) malloc_aligned(MWAY_MERGE_BUFFER_SIZE);
        args->sharedmergebuffer[numaregionid] = sharedmergebuffer;
    }

#ifdef PERF_COUNTERS
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("========= 2) Profiling results of Sorting Phase =========\n");
        PCM_printResults();
    }
#endif

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->sort);
    }
    /* check whether local relations are sorted? */
#if 0
#include <string.h> /* memcpy() */
    tuple_t * tmparr = (tuple_t *) malloc(sizeof(tuple_t)*relR.num_tuples);
    uint32_t off = 0;
    for(i = 0; i < PARTFANOUT; i ++) {
        relationpair_t * rels = & args->threadrelchunks[my_tid][i];
        memcpy((void*)(tmparr+off), (void*)(rels->R.tuples), rels->R.num_tuples*sizeof(tuple_t));
        off += rels->R.num_tuples;
    }
    if(is_sorted_helper((int64_t*)tmparr, relR.num_tuples))
        printf("[INFO ] %d-thread -> relR is sorted, size = %d\n", my_tid, relR.num_tuples);
    else
        printf("[ERROR] %d-thread -> relR is NOT sorted, size = %d, off=%d********\n", my_tid, relR.num_tuples, off);
    free(tmparr);
    tmparr = (tuple_t *) malloc(sizeof(tuple_t)*relS.num_tuples);
    off = 0;
    for(i = 0; i < PARTFANOUT; i ++) {
        relationpair_t * rels = & args->threadrelchunks[my_tid][i];
        memcpy((void*)(tmparr+off), (void*)(rels->S.tuples), rels->S.num_tuples*sizeof(tuple_t));
        off += rels->S.num_tuples;
    }
    if(is_sorted_helper((int64_t*)tmparr, relS.num_tuples))
        printf("[INFO ] %d-thread -> relS is sorted, size = %d\n", my_tid, relS.num_tuples);
    else
        printf("[ERROR] %d-thread -> relS is NOT sorted, size = %d\n", my_tid, relS.num_tuples);
#endif

#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    /**
     * Step.3) Apply multi-way merging with in-cache resident buffers.
     */
    uint64_t mergeRtotal = 0, mergeStotal = 0;
    tuple_t * tmpoutR;
    tuple_t * tmpoutS;

    if(args->nthreads == 1) {
        /* single threaded execution; no multi-way merge. */
        for(i = 0; i < PARTFANOUT; i ++) {
            relationpair_t * rels = & args->threadrelchunks[my_tid][i];
            mergeRtotal += rels->R.num_tuples;
            mergeStotal += rels->S.num_tuples;

            /* evaluate join between each sorted part */
            partsR[i]->tuples = rels->R.tuples;
            partsR[i]->num_tuples = rels->R.num_tuples;
            partsS[i]->tuples = rels->R.tuples;
            partsS[i]->num_tuples = rels->S.num_tuples;
        }
        /* no merging, just pass around pointers */
        tmpoutR = tmpR.tuples;
        tmpoutS = tmpS.tuples;
    }
    else {
        uint32_t       j;
        const uint32_t perthread   = PARTFANOUT / args->nthreads;

        /* multi-threaded execution */
        /* merge remote relations and bring to local memory */
        const uint32_t start = my_tid * perthread;
        const uint32_t end = start + perthread;

        relation_t * Rparts[PARTFANOUT];
        relation_t * Sparts[PARTFANOUT];

        /* compute the size of merged relations to be stored locally */
        uint32_t f = 0;
        for(j = start; j < end; j ++) {
            for(i = 0; i < args->nthreads; i ++) {
                //uint32_t tid = (my_tid + i) % args->nthreads;
                uint32_t tid = numa_shuffle_strategy(my_tid, i, args->nthreads);
                relationpair_t * rels = & args->threadrelchunks[tid][j];
                //fprintf(stdout, "TID=%d Part-%d-size = %d\n", my_tid, f, rels->S.num_tuples);
                Rparts[f] = & rels->R;
                Sparts[f] = & rels->S;
                f++;

                mergeRtotal += rels->R.num_tuples;
                mergeStotal += rels->S.num_tuples;
            }
        }

        /* allocate memory at local node for temporary merge results */
        tmpoutR = malloc_aligned(mergeRtotal*sizeof(tuple_t));
        tmpoutS = malloc_aligned(mergeStotal*sizeof(tuple_t));

        /* determine the L3 cache-size per thread */
        /* int nnuma = get_num_numa_regions(); */

        /* active number of threads in the current NUMA-region: */
        int active_nthreads_in_numa = get_num_active_threads_in_numa(numaregionid);

        /* index of the current thread in its NUMA-region: */
        int numatidx = get_thread_index_in_numa(my_tid);

        /* get the exclusive part of the merge buffer for the current thread */
        int bufsz_thr = (MWAY_MERGE_BUFFER_SIZE/active_nthreads_in_numa)/sizeof(tuple_t);
        tuple_t * mergebuf = args->sharedmergebuffer[numaregionid]
                                                     + (numatidx * bufsz_thr);

        /* now do the multi-way merging */
        if(scalarmergeflag){
            scalar_multiway_merge(tmpoutR, Rparts, PARTFANOUT, mergebuf, bufsz_thr);
            scalar_multiway_merge(tmpoutS, Sparts, PARTFANOUT, mergebuf, bufsz_thr);
        }
        else {
            avx_multiway_merge(tmpoutR, Rparts, PARTFANOUT, mergebuf, bufsz_thr);
            avx_multiway_merge(tmpoutS, Sparts, PARTFANOUT, mergebuf, bufsz_thr);
        }
    }

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->mergedelta);
        args->merge = args->mergedelta; /* since we do merge in single go. */
        DEBUGMSG(1, "Multi-way merge is complete!\n");
        /* the thread that allocated the merge buffer releases it. */
        if(is_first_thread_in_numa_region(my_tid)) {
            free(args->sharedmergebuffer[numaregionid]);
        }
    }

#ifdef PERF_COUNTERS
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("========= 3) Profiling results of Multi-Way NUMA-Merge Phase =========\n");
        PCM_printResults();
        //PCM_cleanup();
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif


    /* To check whether sorted? */
    /*
    check_sorted((int64_t *)tmpoutR, (int64_t *)tmpoutS,
                 mergeRtotal, mergeStotal, my_tid);
     */

    /**
     * Step.4) NUMA-local merge-join on local sorted runs.
     */

#ifdef JOIN_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = chainedtuplebuffer_init();
#else
    void * chainedbuf = NULL;
#endif

    uint64_t nresults = 0;

    if(args->nthreads > 1){
        tuple_t * rtuples = (tuple_t *) tmpoutR;
        tuple_t * stuples = (tuple_t *) tmpoutS;

        nresults = merge_join(rtuples, stuples,
                                   mergeRtotal, mergeStotal, chainedbuf);

    } else {
        /* single-threaded execution: just join sorted partition-pairs */
        for(i = 0; i < PARTFANOUT; i ++) {
            /* evaluate join between each sorted part */
            nresults += merge_join(partsR[i]->tuples, partsS[i]->tuples,
                    partsR[i]->num_tuples, partsS[i]->num_tuples,
                    chainedbuf);
        }
    }
    args->result = nresults;
    /* printf("TID=%d --> #res = %d %d\n", my_tid, args->result, nresults); */

#ifdef JOIN_MATERIALIZE
    args->threadresult->nresults = nresults;
    args->threadresult->threadid = my_tid;
    args->threadresult->results  = (void *) chainedbuf;
#endif

    /* for proper timing */
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->join);
        gettimeofday(&args->end, NULL);
    }

    /* clean-up */
    for(i = 0; i < PARTFANOUT; i++) {
        free(partsR[i]);
        free(partsS[i]);
    }

    free(args->threadrelchunks[my_tid]);
    /* clean-up temporary relations */
    if(args->nthreads > 1){
        free(tmpoutR);
        free(tmpoutS);
    }

    if(my_tid == 0){
        free(tmpR.tuples);
        free(tmpS.tuples);
    }

    return 0;
}


