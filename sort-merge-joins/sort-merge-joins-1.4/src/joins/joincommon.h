/**
 * @file    joincommon.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:39:54 2012
 * @version $Id $
 *
 * @brief   Common structures, macros and functions of sort-merge join algorithms.
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */

#ifndef JOINCOMMON_H_
#define JOINCOMMON_H_

#include <stdint.h>
#include <stdlib.h>             /* posix_memalign, EXIT_FAILURE */
#include <sys/time.h>           /* gettimeofday */
#include <stdio.h>              /* FILE */

#include "types.h"              /* relation_t, tuple_t, result_t */
#include "barrier.h"            /* pthread_barrier_* */
#include "generator.h"          /* numa_localize() --> TODO: refactor */

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

/** The partitioning fan-out for the inital step of sort-merge joins */
#ifndef NRADIXBITS
#define NRADIXBITS 7
#endif
#ifndef PARTFANOUT
#define PARTFANOUT (1<<NRADIXBITS)
#endif

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

/** This is a padding at the end of partitions to align to multiples of 8 for
    AVX merge routine */
#ifndef PADDINGX8
#define PADDINGX8 (PARTFANOUT * CACHE_LINE_SIZE/sizeof(tuple_t))
#endif

/** Put an odd number of cache lines between partitions in pass-2:
    Here we put 3 cache lines */
#ifndef SMALL_PADDING_TUPLES
#define SMALL_PADDING_TUPLES (3 * CACHE_LINE_SIZE / sizeof(tuple_t))
#endif

/** @warning This padding must be allocated at the end of relation */
#ifndef RELATION_PADDING
#define RELATION_PADDING (SMALL_PADDING_TUPLES * PARTFANOUT * 2 * sizeof(tuple_t))
#endif

/** Global command line arguments, whether to execute scalar code */
extern int scalarsortflag; /* by default AVX-code is executed */
extern int scalarmergeflag;

/** Join thread arguments struct */
typedef struct arg_t arg_t;

/** To keep track of the input relation pairs fitting into L2 cache */
typedef struct relationpair_t relationpair_t;

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                        \
    if(COND) {                                                          \
        fprintf(sterr,                                                  \
                "[DEBUG @ %s:%d\] "MSG, __FILE__, __LINE__, ## __VA_ARGS__); \
    }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif

/**
 * Cache-line aligned memory allocation with posix_memalign()
 *
 * @param size
 */
void *
malloc_aligned(size_t size);

/** Initialize and run the given join algorithm with given number of threads */
result_t *
sortmergejoin_initrun(relation_t * relR, relation_t * relS, int nthreads,
                      void * (*jointhread)(void *));

/** Print out timing stats for the given start and end timestamps */
void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end,
             FILE * out);

/**
 * Does merge join on two sorted relations. Just a naive scalar
 * implementation. TODO: consider AVX for this code.
 *
 * @param rtuples sorted relation R
 * @param stuples sorted relation S
 * @param numR number of tuples in R
 * @param numS number of tuples in S
 * @param output join results, if JOIN_MATERIALIZE defined.
 */
uint64_t
merge_join(tuple_t * rtuples, tuple_t * stuples,
           const uint64_t numR, const uint64_t numS, void * output);

/**
 * Does merge join on two sorted relations with interpolation
 * searching in the beginning to find the search start index. Just a
 * naive scalar implementation. TODO: consider AVX for this code.
 *
 * @param rtuples sorted relation R
 * @param stuples sorted relation S
 * @param numR number of tuples in R
 * @param numS number of tuples in S
 * @param output join results, if JOIN_MATERIALIZE defined.
 */
uint64_t
merge_join_interpolation(tuple_t * rtuples, tuple_t * stuples,
                         const uint64_t numR, const uint64_t numS,
                         void * output);


int
is_sorted_helper(int64_t * items, uint64_t nitems);
/** utility method to check whether arrays are sorted */
void
check_sorted(int64_t * R, int64_t * S, uint64_t nR, uint64_t nS, int my_tid);

/** holds the arguments passed to each thread */
struct arg_t {
    tuple_t *  relR;
    tuple_t *  tmpR;
    tuple_t *  relS;
    tuple_t *  tmpS;

    int32_t numR;
    int32_t numS;

    pthread_barrier_t * barrier;
    int64_t result;
    int32_t my_tid;
    int     nthreads;

    relationpair_t ** threadrelchunks;

    /** used for multi-way merging, shared by active threads in each NUMA. */
    tuple_t ** sharedmergebuffer;

    /** arguments specific to mpsm-join: */
    uint32_t ** histR;
    tuple_t * tmpRglobal;
    uint64_t totalR;

#ifdef JOIN_MATERIALIZE
    /* results of the thread */
    threadresult_t * threadresult;
#endif

    /* timing stats */
    struct timeval start, end;
    uint64_t part, sort, mergedelta, merge, join;

} __attribute__((aligned(CACHE_LINE_SIZE)));


/** To keep track of the input relation pairs fitting into L2 cache */
struct relationpair_t {
    relation_t R;
    relation_t S;
};

#endif /* JOINCOMMON_H_ */
