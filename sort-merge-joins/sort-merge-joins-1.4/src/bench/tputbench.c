/**
 * @file    tputbench.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @version $Id $
 *
 * @brief   Measures overall partitioning and multi-way merge througputs.
 *          The code is adapted from the sortmergejoin_multiway.c. It actually
 *          almost evaluates the steps of normal join processing for measuring
 *          the throughput of parallel partitioning and parallel merging phases.
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>           /* gettimeofday */
#include <limits.h>             /* LONG_MAX */
#include <string.h>             /* strlen(), memcpy() */
#include <getopt.h>             /* getopt */
/* #include <assert.h> */

#include "affinity.h"           /* CPU_SET, setaffinity(), pthread_attr_setaffinity_np */
#include "types.h"
#include "generator.h"

#include <stdlib.h> /* malloc() */
#include <math.h>   /* log2(), ceil() */

#include "rdtsc.h"              /* startTimer, stopTimer */
#include "barrier.h"            /* pthread_barrier_* */
#include "cpu_mapping.h"        /* cpu_id NUMA related methods */

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

/* NUMA-bench options: */
int numa_memcpy_bench = 0; /* default */
int numa_readonly_bench = 0;
/**
 * Various NUMA shuffling strategies as also described by NUMA-aware
 * data shuffling paper:
 * NUMA_SHUFFLE_RANDOM, NUMA_SHUFFLE_RING, NUMA_SHUFFLE_NEXT
 */
enum numa_strategy_t {RANDOM, RING, NEXT};
enum numa_strategy_t numastrategy;

int
numa_shuffle_strategy(int my_tid, int i, int nthreads)
{
    if(numastrategy == RANDOM){
        static tuple_t select[64];
        static int shuffled = 0;
        if(shuffled == 0){
            relation_t ss;
            ss.tuples = (tuple_t*)select;
            ss.num_tuples = nthreads;
            for(int s=0; s < nthreads; s++)
                ss.tuples[s].key = s;
            knuth_shuffle(&ss);
            shuffled = 1;
        }

        return select[i].key;
    }
    else if(numastrategy == RING){
        int nid;
        /* for Intel-E5-4640 */
        /*
        static int numa[64] = {
                0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60,
                1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61,
                2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 62,
                3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63 };
        nid = numa[my_tid];
        */
        nid = get_cpu_id(my_tid);
        return (nid + i) % nthreads; // --> NUMA-SHUFF-RING
    }
    else /* NEXT */ {
        return (my_tid + i) % nthreads; // --> NUMA-SHUFF-NEXT-THR
    }
}

/**
 * Tputbench uses almost the same code as the m-way sort-merge join.
 */
void *
tputbench_thread(void * param);

result_t *
tputbench(relation_t * relR, relation_t * relS, int nthreads)
{
    return sortmergejoin_initrun(relR, relS, nthreads, tputbench_thread);
}

/**
 * Numabench uses almost the same code as the m-way sort-merge join.
 */
void *
numabench_thread(void * param);

result_t *
numabench(relation_t * relR, relation_t * relS, int nthreads)
{
    return sortmergejoin_initrun(relR, relS, nthreads, numabench_thread);
}

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

#include "config.h"          /* autoconf header */

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                        \
    if(COND) {                                                          \
        fprintf(stderr,                                                 \
                "[DEBUG @ %s:%d\] "MSG, __FILE__, __LINE__, ## __VA_ARGS__); \
    }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif


/** Global command line arguments, whether to execute scalar code */
extern int scalarsortflag; /* defined in joincommon.c */
extern int scalarmergeflag;

/** Print out timing stats for the given start and end timestamps */
extern void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end,
             FILE * out);

/******************************************************************************
 *                                                                            *
 *                 Command-line handling & Driver Program                     *
 *                                                                            *
 ******************************************************************************/

#if !defined(__cplusplus)
int getopt(int argc, char * const argv[],
           const char *optstring);
#endif

typedef struct algo_t  algo_t;
typedef struct cmdparam_t cmdparam_t;

struct algo_t {
    char name[128];
    result_t * (*joinalgorithm)(relation_t *, relation_t *, int);
};

struct cmdparam_t {
    algo_t * algo;
    uint64_t r_size;
    uint64_t s_size;
    uint32_t nthreads;
    uint32_t r_seed;
    uint32_t s_seed;
    double skew;
    int nonunique_keys;  /* non-unique keys allowed? */
    int verbose;
    int fullrange_keys;  /* keys covers full int range? */
    int basic_numa;/* alloc input chunks thread local? */
    char * perfconf;
    char * perfout;
    int scalar_sort;
    int scalar_merge;
};

extern char * optarg;
extern int    optind, opterr, optopt;

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize;  /* defined in generator.c */
extern int nthreads;      /* defined in generator.c */

/** All available algorithms */
static struct algo_t algos [] =
  {
      {"tputbench", tputbench},
      {"numabench", numabench},
      {{0}, 0}
  };

/* command line handling functions */
void
print_help();

void
print_version();

void
parse_args(int argc, char ** argv, cmdparam_t * cmd_params);


static char *
mystrdup (const char *s)
{
    char *ss = (char*) malloc (strlen (s) + 1);

    if (ss != NULL)
        memcpy (ss, s, strlen(s) + 1);

    return ss;
}

/**
 * Main execution thread of tputbench (actually "m-way" sort-merge join).
 *
 * @param param
 */
void *
tputbench_thread(void * param)
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

    /** TPUTBENCH timing */
    struct timeval start, end;
    gettimeofday(&start, NULL);

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

    /** TPUTBENCH timing */
    gettimeofday(&end, NULL);
    double diff_usec = ((end.tv_sec*1000000L + end.tv_usec)
                        - (start.tv_sec*1000000L + start.tv_usec));
    double tput = (((relR.num_tuples+relS.num_tuples))*1000000L/diff_usec);
    fprintf(stderr, "[Thread-%d] PART-TIME: %.2lf, TPUT: %.2lf, MEMTPUT: %.2lf\n",
            my_tid, diff_usec, tput, tput*2*sizeof(tuple_t)/1024.0/1024.0);

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
        if(!is_sorted_helper(outptr, ntuples_per_part)){
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
        if(!is_sorted_helper(outptr, ntuples_per_part)){
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

        /** TPUTBENCH timing */
        struct timeval startts, endts;
        gettimeofday(&startts, NULL);

        /* now do the multi-way merging */
        if(scalarmergeflag){
            scalar_multiway_merge(tmpoutR, Rparts, PARTFANOUT, mergebuf, bufsz_thr);
            scalar_multiway_merge(tmpoutS, Sparts, PARTFANOUT, mergebuf, bufsz_thr);
        }
        else {
            avx_multiway_merge(tmpoutR, Rparts, PARTFANOUT, mergebuf, bufsz_thr);
            avx_multiway_merge(tmpoutS, Sparts, PARTFANOUT, mergebuf, bufsz_thr);
        }

        /** TPUTBENCH timing */
        gettimeofday(&endts, NULL);
        double diff_usec = ((endts.tv_sec*1000000L + endts.tv_usec)
                            - (startts.tv_sec*1000000L + startts.tv_usec));
        double tput = (((mergeRtotal+mergeStotal))*1000000L/diff_usec);
        fprintf(stderr, "[Thread-%d] MERGE-TIME: %.2lf, TPUT: %.2lf, MEMTPUT: %.2lf\n",
                my_tid, diff_usec, tput, tput*2*sizeof(tuple_t)/1024.0/1024.0);
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

/**
 * Main execution thread of numabench (actually "m-way" sort-merge join).
 *
 * @param param
 */
void *
numabench_thread(void * param)
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
        if(!is_sorted_helper(outptr, ntuples_per_part)){
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
        if(!is_sorted_helper(outptr, ntuples_per_part)){
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

    struct timeval numastartts, numaendts;
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->sort);
        gettimeofday(&numastartts, NULL);
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
        /***
         * NUMA-memcpy/read-throughput bench.
         * Basically we read data from all NUMA-regions and compute the throughput.
         * There are two options: numa_memcpy_bench and numa_readonly_bench
         */
        {
            uint32_t       j;
            const uint32_t perthread   = PARTFANOUT / args->nthreads;

            uint64_t       mergeRtotal = 0, mergeStotal = 0;
            tuple_t *      tmpoutR;
            tuple_t *      tmpoutS;

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
                    uint32_t tid = numa_shuffle_strategy(my_tid, i, args->nthreads);
                    //if(get_numa_id(tid) != numaid){
                    relationpair_t * rels = & args->threadrelchunks[tid][j];

                    Rparts[f] = & rels->R;
                    Sparts[f] = & rels->S;
                    f++;

                    mergeRtotal += rels->R.num_tuples;
                    mergeStotal += rels->S.num_tuples;
                    //}
                }
            }
            //printf("TID=%d fanout=%d\n", my_tid,f);
            /* allocate memory at local node for temporary merge results */
            tmpoutR = (tuple_t *) malloc_aligned(mergeRtotal * sizeof(tuple_t));
            tmpoutS = (tuple_t *) malloc_aligned(mergeStotal * sizeof(tuple_t));

            /** NUMABENCH timing */
            struct timeval startts, endts;
            gettimeofday(&startts, NULL);

            uint64_t off = 0;
            int64_t sum = 0;
            if(numa_memcpy_bench){
                /* NUMA-MEMCPY-BENCH */
                for(j = 0; j < f; j++){
                    memcpy(tmpoutR+off, Rparts[j]->tuples, sizeof(tuple_t) * Rparts[j]->num_tuples);
                    off += Rparts[j]->num_tuples;
                    //BARRIER_ARRIVE(args->barrier, rv); //--> with BARRIERS
                }
            }
            else if(numa_readonly_bench){
                /* NUMA-READONLY-AGG-BENCH */
                /* Do just reading only aggregation */
                for(j = 0; j < f; j++){
                    for(int64_t n = 0; n < Rparts[j]->num_tuples; n++){
                        sum += Rparts[j]->tuples[n].key;
                    }
                }
                //BARRIER_ARRIVE(args->barrier, rv); //--> with BARRIERS
            }

            // printf("TOTAL=%d\n",off);
            off = 0;
            if(numa_memcpy_bench){
                /* NUMA-MEMCPY-BENCH */
                for(j = 0; j < f; j++){
                    memcpy(tmpoutS+off, Sparts[j]->tuples, sizeof(tuple_t) * Sparts[j]->num_tuples);
                    off += Sparts[j]->num_tuples;
                    //BARRIER_ARRIVE(args->barrier, rv); //--> with BARRIERS
                }
            }
            else if(numa_readonly_bench){
                /* NUMA-READONLY-AGG-BENCH */
                /* Do just reading only aggregation */
                for(j = 0; j < f; j++){
                    for(int64_t n = 0; n < Sparts[j]->num_tuples; n++){
                        sum += Sparts[j]->tuples[n].key;
                    }
                }
                //BARRIER_ARRIVE(args->barrier, rv); //--> with BARRIERS
            }

            /** NUMABENCH timing */
            gettimeofday(&endts, NULL);
            double diff_usec = ((endts.tv_sec*1000000L + endts.tv_usec)
                                - (startts.tv_sec*1000000L + startts.tv_usec));
            double tput = (((mergeRtotal+mergeStotal))*1000000L/diff_usec);
            fprintf(stderr, "[Thread-%d] NUMA-%s(%s)-TIME: %.2lf, TPUT: %.2lf, MEMTPUT: %.2lf\n",
                    my_tid, (numa_memcpy_bench ? "MEMCPYBENCH":"READONLYBENCH"),
                    (numastrategy==NEXT ? "NEXT" : (numastrategy==RING) ? "RING" : "RANDOM"),
                    diff_usec, tput, tput*2*sizeof(tuple_t)/1024.0/1024.0);


            BARRIER_ARRIVE(args->barrier, rv);
            if(my_tid == 0) {
                stopTimer(&args->mergedelta);
                gettimeofday(&numaendts, NULL);
                uint64_t cycles = args[0].mergedelta - args[0].sort;
                double diffusec = ((numaendts.tv_sec*1000000L + numaendts.tv_usec)
                                                - (numastartts.tv_sec*1000000L + numastartts.tv_usec));
                fprintf(stderr, "[INFO ] Overall NUMA-%s-TIME: %.2lf, CYCLES: %llu\n",
                                    (numa_memcpy_bench ? "MEMCPYBENCH":"READONLYBENCH"),
                                    diffusec, cycles);
            }
            printf("GARBAGE to avoid optimization=%lu\n",sum);

        }
        /*** END of NUMA-memcpy/read-throughput bench ***/

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

int
main(int argc, char *argv[])
{
    struct timeval start, end;
    relation_t relR;
    relation_t relS;

    /* start initially on CPU-0 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) <0) {
        perror("sched_setaffinity");
    }

    /* Command line parameters */
    cmdparam_t cmd_params;

    /* Default values if not specified on command line */
    cmd_params.algo     = &algos[0]; /* tputbench with m-way sort-merge join */
    cmd_params.nthreads = 2;
    /* default dataset is Workload B (described in paper) */
    cmd_params.r_size   = 128000000;
    cmd_params.s_size   = 128000000;
    cmd_params.r_seed   = 12345;
    cmd_params.s_seed   = 54321;
    cmd_params.skew     = 0.0;
    cmd_params.verbose  = 0;
    cmd_params.perfconf = NULL;
    cmd_params.perfout  = NULL;
    cmd_params.nonunique_keys   = 0;
    cmd_params.fullrange_keys   = 0;
    cmd_params.basic_numa   = 0;
    cmd_params.scalar_sort  = 0;
    cmd_params.scalar_merge = 0;

    parse_args(argc, argv, &cmd_params);

    scalarsortflag  = cmd_params.scalar_sort;
    scalarmergeflag = cmd_params.scalar_merge;

#ifdef PERF_COUNTERS
    PCM_CONFIG = cmd_params.perfconf;
    PCM_OUT    = cmd_params.perfout;
#endif

    /***** create relation R *****/
    fprintf(stdout,
            "[INFO ] Creating relation R with size = %.3lf MiB, #tuples = %lld : ",
            (double) sizeof(tuple_t) * cmd_params.r_size/(1024.0*1024.0),
            cmd_params.r_size);
    fflush(stdout);

    seed_generator(cmd_params.r_seed);

    /* to pass information to the create_relation methods */
    numalocalize = cmd_params.basic_numa;
    nthreads     = cmd_params.nthreads;

    gettimeofday(&start, NULL);
    if(cmd_params.fullrange_keys) {
        create_relation_nonunique(&relR, cmd_params.r_size, INT_MAX);
    }
    else if(cmd_params.nonunique_keys) {
        create_relation_nonunique(&relR, cmd_params.r_size, cmd_params.r_size);
    }
    else {
        parallel_create_relation(&relR, cmd_params.r_size,
                                 cmd_params.nthreads, cmd_params.r_size);
        /* create_relation_pk(&relR, cmd_params.r_size); */
    }
    gettimeofday(&end, NULL);
    fprintf(stdout, "OK \n");
    fprintf(stdout, "[INFO ] Time: ");
    print_timing(cmd_params.r_size, &start, &end, stdout);
    fprintf(stdout, "\n");

    /***** create relation S *****/
    fprintf(stdout,
            "[INFO ] Creating relation S with size = %.3lf MiB, #tuples = %lld : ",
            (double) sizeof(tuple_t) * cmd_params.s_size/1024.0/1024.0,
            cmd_params.s_size);
    fflush(stdout);

    seed_generator(cmd_params.s_seed);

    gettimeofday(&start, NULL);
    if(cmd_params.fullrange_keys) {
        create_relation_fk_from_pk(&relS, &relR, cmd_params.s_size);
    }
    else if(cmd_params.nonunique_keys) {
        /* use size of R as the maxid */
        create_relation_nonunique(&relS, cmd_params.s_size, cmd_params.r_size);
    }
    else {
        /* if r_size == s_size then equal-dataset, else non-equal dataset */

        if(cmd_params.skew > 0){
            /* S is skewed */
            create_relation_zipf(&relS, cmd_params.s_size,
                                 cmd_params.r_size, cmd_params.skew);
        }
        else {
            /* S is uniform foreign key */
            parallel_create_relation(&relS, cmd_params.s_size,
                                     cmd_params.nthreads, cmd_params.r_size);
            /* create_relation_pk(&relS, cmd_params.s_size); */
        }
    }
    gettimeofday(&end, NULL);
    fprintf(stdout, "OK \n");
    fprintf(stdout, "[INFO ] Time: ");
    print_timing(cmd_params.s_size, &start, &end, stdout);
    fprintf(stdout, "\n");

    /* Run the selected join algorithm */
    fprintf(stdout, "[INFO ] Running %s using m-way join algorithm ...\n", cmd_params.algo->name);

    result_t * result = cmd_params.algo->joinalgorithm(&relR, &relS, cmd_params.nthreads);

    fprintf(stdout, "\n[INFO ] Results = %llu. DONE.\n", result->totalresults);

#if (defined(PERSIST_RELATIONS) && defined(JOIN_MATERIALIZE))
    printf("[INFO ] Persisting the output result to \"Out.tbl\" ...\n");
    write_result_relation(result, "Out.tbl");
#endif


    /* cleanup */
    delete_relation(&relR);
    delete_relation(&relS);

#ifdef JOIN_MATERIALIZE
    for(int i = 0; i < nthreads; i++){
        chainedtuplebuffer_free(result->resultlist[i].results);
    }
#endif
    free(result->resultlist);
    free(result);
    cpu_mapping_cleanup();

    return 0;
}

/* command line handling functions */
void
print_help(char * progname)
{
    printf("Usage: %s [options]\n", progname);

    printf("Benchmark selection, benchmarks : ");
    int i = 0;
    while(algos[i].joinalgorithm) {
        printf("%s ", algos[i].name);
        i++;
    }
    printf("\n");

    printf("\
       -a --algo=<name>    Run the join algorithm named <name> [tputbench]    \n\
                                                                              \n\
    NUMA-bench options:                                                       \n\
       --numamemcpy       Use memcpy (r+w) benchmark (remote chunks -> local) \n\
       --numareadonly     Use readonly aggregation of remote chunks           \n\
       --numastrategy(-S) NUMA-shuffling strategies: NEXT, RANDOM, RING       \n\
                                                                              \n\
    Other join configuration options, with default values in [] :             \n\
       -n --nthreads=<N>  Number of threads to use <N> [2]                    \n\
       -r --r-size=<R>    Number of tuples in build relation R <R> [128000000]\n\
       -s --s-size=<S>    Number of tuples in probe relation S <S> [128000000]\n\
       -x --r-seed=<x>    Seed value for generating relation R <x> [12345]    \n\
       -y --s-seed=<y>    Seed value for generating relation S <y> [54321]    \n\
       -z --skew=<z>      Zipf skew parameter for probe relation S <z> [0.0]  \n\
       --non-unique       Use non-unique (duplicated) keys in input relations \n\
       --full-range       Spread keys in relns. in full 32-bit integer range  \n\
       --basic-numa       Numa-localize relations to threads (Experimental)   \n\
       --scalarsort       Use scalar sorting; sort() -> g++ or qsort() -> gcc \n\
       --scalarmerge      Use scalar merging algorithm instead of AVX-merge   \n\
                                                                              \n\
    Performance profiling options, when compiled with --enable-perfcounters.  \n\
       -p --perfconf=<P>  Intel PCM config file with upto 4 counters [none]   \n\
       -o --perfout=<O>   Output file to print performance counters [stdout]  \n\
                                                                              \n\
    Basic user options                                                        \n\
        -h --help         Show this message                                   \n\
        --verbose         Be more verbose -- show misc extra info             \n\
        --version         Show version                                        \n\
    \n");
}

void
print_version()
{
    printf("\n%s\n", PACKAGE_STRING);
    printf("Copyright (c) 2012, ETH Zurich, Systems Group.\n");
    printf("http://www.systems.ethz.ch/projects/paralleljoins\n\n");
}

void
parse_args(int argc, char ** argv, cmdparam_t * cmd_params)
{

    int c, i, found;
    /* Flag set by --verbose */
    static int verbose_flag;
    static int nonunique_flag;
    static int fullrange_flag;
    static int basic_numa;
    static int scalarsort;
    static int scalarmerge;
    numastrategy = NEXT; /* default */

    while(1) {
        static struct option long_options[] =
            {
                /* These options set a flag. */
                {"verbose",    no_argument,    &verbose_flag,   1},
                {"brief",      no_argument,    &verbose_flag,   0},
                {"non-unique", no_argument,    &nonunique_flag, 1},
                {"full-range", no_argument,    &fullrange_flag, 1},
                {"basic-numa", no_argument,    &basic_numa, 1},
                {"scalarsort", no_argument,    &scalarsort, 1},
                {"scalarmerge",no_argument,    &scalarmerge,1},
                {"numamemcpy", no_argument,    &numa_memcpy_bench,1},
                {"numareadonly",no_argument,   &numa_readonly_bench,1},
                {"help",       no_argument,    0, 'h'},
                {"version",    no_argument,    0, 'v'},
                /* These options don't set a flag.
                   We distinguish them by their indices. */
                {"algo",    required_argument, 0, 'a'},
                {"nthreads",required_argument, 0, 'n'},
                {"perfconf",required_argument, 0, 'p'},
                {"r-size",  required_argument, 0, 'r'},
                {"s-size",  required_argument, 0, 's'},
                {"perfout", required_argument, 0, 'o'},
                {"r-seed",  required_argument, 0, 'x'},
                {"s-seed",  required_argument, 0, 'y'},
                {"skew",    required_argument, 0, 'z'},
                {"numastrategy",    required_argument, 0, 'S'},
                {0, 0, 0, 0}
            };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "a:n:p:r:s:o:x:y:z:hvS:",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;
        switch (c)
        {
          case 0:
              /* If this option set a flag, do nothing else now. */
              if (long_options[option_index].flag != 0)
                  break;
              printf ("option %s", long_options[option_index].name);
              if (optarg)
                  printf (" with arg %s", optarg);
              printf ("\n");
              break;

          case 'a':
              i = 0; found = 0;
              while(algos[i].joinalgorithm) {
                  if(strcmp(optarg, algos[i].name) == 0) {
                      cmd_params->algo = &algos[i];
                      found = 1;
                      break;
                  }
                  i++;
              }

              if(found == 0) {
                  printf("[ERROR] Join algorithm named `%s' does not exist!\n",
                         optarg);
                  print_help(argv[0]);
                  exit(EXIT_SUCCESS);
              }
              break;

          case 'h':
          case '?':
              /* getopt_long already printed an error message. */
              print_help(argv[0]);
              exit(EXIT_SUCCESS);
              break;

          case 'v':
              print_version();
              exit(EXIT_SUCCESS);
              break;

          case 'n':
              cmd_params->nthreads = atoi(optarg);
              break;

          case 'p':
              cmd_params->perfconf = mystrdup(optarg);
              break;

          case 'r':
              cmd_params->r_size = atol(optarg);
              break;

          case 's':
              cmd_params->s_size = atol(optarg);
              break;

          case 'o':
              cmd_params->perfout = mystrdup(optarg);
              break;

          case 'x':
              cmd_params->r_seed = atoi(optarg);
              break;

          case 'y':
              cmd_params->s_seed = atoi(optarg);
              break;

          case 'z':
              cmd_params->skew = atof(optarg);
              break;

          case 'S':
              if (strcmp(optarg, "NEXT") == 0)
                  numastrategy = NEXT;
              else if (strcmp(optarg, "RANDOM") == 0)
                  numastrategy = RANDOM;
              else if (strcmp(optarg, "RING") == 0)
                  numastrategy = RING;
              else {
                  printf("Invalid NUMA-shuffle strategy. Options: NEXT, RANDOM, RING\n");
                  printf("Using NEXT as default.\n");
              }
              break;

          default:
              break;
        }
    }

    /* if (verbose_flag) */
    /*     printf ("verbose flag is set \n"); */

    cmd_params->nonunique_keys = nonunique_flag;
    cmd_params->verbose        = verbose_flag;
    cmd_params->fullrange_keys = fullrange_flag;
    cmd_params->basic_numa     = basic_numa;
    cmd_params->scalar_sort    = scalarsort;
    cmd_params->scalar_merge   = scalarmerge;

    if(strcmp(cmd_params->algo->name, "numabench") == 0
            &&!(numa_memcpy_bench ^ numa_readonly_bench)){
        printf ("\nPlease choose just one of the NUMA-benchmarks: --numamemcpy or --numareadonly\n\n");
        exit(0);
    }

    /* Print any remaining command line arguments (not options). */
    if (optind < argc) {
        printf ("non-option arguments: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        printf ("\n");
    }
}



