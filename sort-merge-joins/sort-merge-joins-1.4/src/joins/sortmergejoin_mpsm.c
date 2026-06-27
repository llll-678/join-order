/**
 * @file    sortmergejoin_mpsm.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:10:38 2012
 * @version $Id $
 *
 * @brief   Implementation of the Massively Parallel Sort-Merge Join (MPSM) as
 *          described in VLDB'12 Paper by Albutiu et al.
 *
 *
 */
#include <stdlib.h> /* malloc() */
#include <math.h>   /* log2(), ceil() */

#include "sortmergejoin_mpsm.h"
#include "joincommon.h"
#include "avxsort.h"    /* avxsort_tuples() */
#include "scalarsort.h" /* scalarsort_tuples() */
#include "rdtsc.h"      /* startTimer, stopTimer */

/** Modulo hash function using bitmask and shift */
#define HASH_BIT_MODULO(K, MASK, NBITS) (((K-1) & MASK) >> NBITS)

/**
 * MPSM: Main thread of Partial-Sort-Scan-Join as described by Albutiu et al.
 *
 * @param param parameters of the thread, see arg_t for details.
 */
void *
mpsmjoin_thread(void * param);

result_t *
sortmergejoin_mpsm(relation_t * relR, relation_t * relS, int nthreads)
{
    return sortmergejoin_initrun(relR, relS, nthreads, mpsmjoin_thread);
}

typedef struct part_t part_t;
/** holds arguments passed for partitioning */
struct part_t {
    tuple_t *  rel;
    tuple_t *  tmp;
    uint32_t **hist;
    uint32_t * output;
    arg_t   *  thrargs;
    uint32_t   num_tuples;
    uint64_t   total_tuples;
    int32_t    R;
    uint32_t   D;
    int        relidx;  /* 0: R, 1: S */
    uint32_t   padding;
} __attribute__((aligned(CACHE_LINE_SIZE)));

/**
 * This function implements the parallel radix partitioning of a given input
 * relation. Parallel partitioning is done by histogram-based relation
 * re-ordering as described by Kim et al. Parallel partitioning method is
 * commonly used by all parallel radix join algorithms.
 *
 * @param part description of the relation to be partitioned
 */
void
parallel_radix_partition(part_t * const part) ;

void *
mpsmjoin_thread(void * param)
{
    relation_t relR, relS, tmpS;
    arg_t * args   = (arg_t*) param;
    int i, rv, my_tid = args->my_tid;

    relR.tuples     = args->relR;
    relR.num_tuples = args->numR;
    relS.tuples     = args->relS;
    relS.num_tuples = args->numS;
    tmpS.tuples     = args->tmpS - my_tid * PADDINGX8;
    tmpS.num_tuples = args->numS;

    args->histR[my_tid] = (uint32_t*) calloc(PARTFANOUT, sizeof(uint32_t));
    uint32_t * outputR  = (uint32_t*) calloc((PARTFANOUT+1), sizeof(uint32_t));

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        gettimeofday(&args->start, NULL);
        startTimer(&args->part);
        startTimer(&args->sort);
        startTimer(&args->mergedelta);
        startTimer(&args->merge);
        startTimer(&args->join);
    }

    /* 1) Range-partition the private input, R relation */
    /* after partitioning tmpR holds the partitioned data */
    part_t part;
    part.R       = ceil(log2(args->numR * args->nthreads)) - NRADIXBITS;
    part.D       = NRADIXBITS;
    part.thrargs = args;
    part.padding = 0;//PADDING_TUPLES;

    // fprintf(stdout, "BITS=%d\n",part.R);

    /* 1. partitioning for relation R */
    part.rel          = args->relR;
    part.tmp          = args->tmpRglobal;
    part.hist         = args->histR;
    part.output       = outputR;
    part.num_tuples   = args->numR;
    part.total_tuples = args->totalR;
    part.relidx       = 0;

    parallel_radix_partition(&part);

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->part);
    }

    /* 2) Fully sort both inputs (partitioned-R and public-S) */
    /* First sort the public input, S */
    relation_t localSchunk;
    args->threadrelchunks[my_tid] = (relationpair_t *)
                                  malloc(PARTFANOUT * sizeof(relationpair_t));

    tuple_t * inptr  = (tuple_t *) (relS.tuples);
    tuple_t * outptr = (tuple_t *) (tmpS.tuples);

    if(scalarsortflag)
        scalarsort_tuples(&inptr, &outptr, relS.num_tuples);
    else
        avxsort_tuples(&inptr, &outptr, relS.num_tuples);

    DEBUGMSG(1, "SORT SIZE=%d\n", relS.num_tuples);

    localSchunk.tuples     = (tuple_t *) outptr;
    localSchunk.num_tuples = relS.num_tuples;

    /*
    if(!is_sorted_helper((int64_t *)localSchunk.tuples, localSchunk.num_tuples)){
        printf("===> %d-thread -> S is NOT sorted, size = %d\n", my_tid,
               localSchunk.num_tuples);
    } else {
        printf("===> %d-thread -> S is sorted, size = %d\n", my_tid,
               localSchunk.num_tuples);
    }
    */


    /* for other threads to access during MJ scans */
    args->threadrelchunks[my_tid][0].S.tuples     = localSchunk.tuples;
    args->threadrelchunks[my_tid][0].S.num_tuples = localSchunk.num_tuples;

    /* Next sort the range-partitioned private input, R */
    relation_t localRchunk;
    uint32_t   offset = (args->tmpR - PADDINGX8 * my_tid - args->tmpRglobal);

    localRchunk.tuples     = args->tmpRglobal + offset;
    localRchunk.num_tuples = args->numR;

    inptr  = (tuple_t *) (localRchunk.tuples);
    outptr = (tuple_t *) (relR.tuples);
    // if(my_tid == 0){
    //     int p = PARTFANOUT/args->nthreads;
    //     for(int f = 1; f <= PARTFANOUT; f+=p){
    //         int sum = 0;
    //         for(int pp = 0; pp < p; pp++)
    //             sum += outputR[f]-outputR[f-1];
    //         printf("SIZE-%d = %d\n", f, sum);
    //     }
    // }
    if(scalarsortflag)
        scalarsort_tuples(&inptr, &outptr, localRchunk.num_tuples);
    else
        avxsort_tuples(&inptr, &outptr, localRchunk.num_tuples);


    localRchunk.tuples = (tuple_t *) outptr;
    // args->thread_chunks[my_tid][0].R.tuples     = localRchunk.tuples;
    // args->thread_chunks[my_tid][0].R.num_tuples = localRchunk.num_tuples;

    /*
    if(!is_sorted_helper((int64_t *)localRchunk.tuples, localRchunk.num_tuples)){
        printf("===> %d-thread -> R is NOT sorted, size = %d\n", my_tid,
               localRchunk.num_tuples);
    } else {
        printf("===> %d-thread -> R is sorted, size = %d\n", my_tid,
               localRchunk.num_tuples);
    }
    */

    DEBUGMSG(1, "SORT SIZE=%d\n", localRchunk.num_tuples);

    /* At this point, input R is completely sorted, public input S is
       partially sorted where its local chunks are sorted. */
    // tuple_t * test = (tuple_t*)malloc(sizeof(tuple_t)*args->numR);
    // memcpy(test, localRchunk.tuples, sizeof(tuple_t)*args->numR);
    // localRchunk.tuples = test;
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->sort);
    }

    /* 3) Apply merge_join with remote relations by sequential scans */
    uint64_t matches = 0;

#ifdef JOIN_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = chainedtuplebuffer_init();
#else
    void * chainedbuf = NULL;
#endif

    for(i = 0; i < args->nthreads; i++) {

        uint32_t     remote_tid = (my_tid + i) % args->nthreads;
        relation_t * remoteS    = & args->threadrelchunks[remote_tid][0].S;

        // matches += merge_join(localRchunk.tuples, remoteS->tuples,
        //                       localRchunk.num_tuples, remoteS->num_tuples,
        //                       chainedbuf);

        // int64_t nn = (remoteS->num_tuples/64);
        // int64_t j = nn * my_tid;
        // nn += j;
        // while( j < nn ) {
        //     matches += remoteS->tuples[j].key;
        //     j++;
        // }

        // int64_t j = 0;
        // while( j < localRchunk.num_tuples)
        //     matches += localRchunk.tuples[j++].key;

        matches += merge_join_interpolation(localRchunk.tuples,
                                            remoteS->tuples,
                                            localRchunk.num_tuples,
                                            remoteS->num_tuples,
                                            chainedbuf);
    }

    args->result = matches;

#ifdef JOIN_MATERIALIZE
    args->threadresult->nresults = matches;
    args->threadresult->threadid = my_tid;
    args->threadresult->results  = (void *) chainedbuf;
#endif

    /* for proper timing */
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->join);
        args->merge = args->sort;
        args->mergedelta = args->merge;
        gettimeofday(&args->end, NULL);
    }

    free(args->threadrelchunks[my_tid]);
    free(args->histR[my_tid]);
    free(outputR);

    return 0;
}

void
parallel_radix_partition(part_t * const part)
{
    const tuple_t * restrict rel    = part->rel;
    uint32_t **              hist   = part->hist;
    uint32_t *      restrict output = part->output;

    const uint32_t my_tid     = part->thrargs->my_tid;
    const uint32_t nthreads   = part->thrargs->nthreads;
    const uint32_t num_tuples = part->num_tuples;

    const int32_t  R       = part->R;
    const int32_t  D       = part->D;
    const uint32_t fanOut  = 1 << D;
    const uint64_t MASK    = (fanOut - 1) << R;
    const uint32_t padding = part->padding;

    int32_t sum = 0;
    uint32_t i, j;
    int rv;

    int32_t dst[fanOut+1];

    /* compute local histogram for the assigned region of rel */
    /* compute histogram */
    uint32_t * my_hist = hist[my_tid];

    for(i = 0; i < num_tuples; i++) {
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        my_hist[idx] ++;
    }

    /* compute local prefix sum on hist */
    for(i = 0; i < fanOut; i++){
        sum += my_hist[i];
        my_hist[i] = sum;
    }

    /* wait at a barrier until each thread complete histograms */
    BARRIER_ARRIVE(part->thrargs->barrier, rv);

    /* determine the start and end of each cluster */
    for(i = 0; i < my_tid; i++) {
        for(j = 0; j < fanOut; j++)
            output[j] += hist[i][j];
    }
    for(i = my_tid; i < nthreads; i++) {
        for(j = 1; j < fanOut; j++)
            output[j] += hist[i][j-1];
    }

    for(i = 0; i < fanOut; i++ ) {
        output[i] += i * padding; //PADDING_TUPLES;
        dst[i] = output[i];
    }
    output[fanOut] = part->total_tuples + fanOut * padding; //PADDING_TUPLES;

    tuple_t * restrict tmp = part->tmp;
    /* intkey_t max[FANOUT]; */
    /* for(i=0; i < FANOUT; i++){ */
    /*     max[i] = 0; */
    /* } */
    /* Copy tuples to their corresponding clusters */
    for(i = 0; i < num_tuples; i++ ){
        uint32_t idx = HASH_BIT_MODULO(rel[i].key, MASK, R);
        tmp[dst[idx]] = rel[i];
        ++dst[idx];
        /* if(max[idx] != 0 && rel[i].key > max[idx]) max[idx] = rel[i].key; */
    }
    /* intkey_t prev = 0; */
    /* for(i=0; i < FANOUT; i++){ */
    /*     if(max[i] < prev){ */
    /*         printf("max[%d] = %d is less than max[%d] = %d\n", */
    /*                i, max[i], i-1, prev); */
    /*     } */
    /*     prev = max[i]; */
    /* } */
}
