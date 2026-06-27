/**
 * @file    sortmergejoin_multipass.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:38:34 2012
 * @version $Id $
 *
 * @brief   m-pass sort-merge-join algorithm with multi-pass merging.
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

#include "sortmergejoin_multipass.h"
#include "joincommon.h"
#include "partition.h"  /* partition_relation_optimized() */
#include "avxsort.h"    /* avxsort_tuples() */
#include "scalarsort.h" /* scalarsort_tuples() */
#include "merge.h"      /* avx_merge_tuples(), scalar_merge_tuples() */

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
sortmergejoin_multipass_thread(void * param);

result_t *
sortmergejoin_multipass(relation_t * relR, relation_t * relS, int nthreads)
{
    return sortmergejoin_initrun(relR, relS, nthreads,
                                 sortmergejoin_multipass_thread);
}

#if 0
/** Implements the Step.1 of the algorithm: NUMA-local partitioning. */
void
partition_relations(relation_t *** ppartsR, relation_t *** ppartsS,
                    relation_t * relR, relation_t * relS,
                    relation_t * tmpR, relation_t * tmpS)
{
    relation_t * partsR[PARTFANOUT];
    relation_t * partsS[PARTFANOUT];

    for(int i = 0; i < PARTFANOUT; i++) {
        partsR[i] = (relation_t *) malloc(sizeof(relation_t));
        partsS[i] = (relation_t *) malloc(sizeof(relation_t));
    }


    /* after partitioning tmpR, tmpS holds the partitioned data */
    int bitshift = ceil(log2(relR.num_tuples * args->nthreads)) - NRADIXBITS - 1;
    // printf("bitshift = %d\n", bitshift);

    partition_relation_optimized(partsR, &relR, &tmpR, NRADIXBITS, bitshift);
    partition_relation_optimized(partsS, &relS, &tmpS, NRADIXBITS, bitshift);

    /* return values, a list of partitioned relations */
    *ppartsR = (relation_t **)(partsR);
    *ppartsS = (relation_t **)(partsS);
}
#endif

void *
sortmergejoin_multipass_thread(void * param)
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
        //PCM_start();
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
    int bitshift = ceil(log2(relR.num_tuples * args->nthreads)) - NRADIXBITS - 1;
    // printf("bitshift = %d\n", bitshift);
    partition_relation_optimized(partsR, &relR, &tmpR, NRADIXBITS, bitshift);
    partition_relation_optimized(partsS, &relS, &tmpS, NRADIXBITS, bitshift);

#else
    /** Step.1) NUMA-local partitioning. */
    relation_t ** partsR = NULL;
    relation_t ** partsS = NULL;
    partition_relations(&partsR, &partsS, &relR, &relS, &tmpR, &tmpS);
#endif

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->part);
    }

    /** Step.2) NUMA-local sorting of cache-sized chunks */
    args->threadrelchunks[my_tid] = (relationpair_t *)
                                  malloc(PARTFANOUT * sizeof(relationpair_t));

    uint64_t ntuples_per_part;
    uint64_t offset = 0;

    for(i = 0; i < PARTFANOUT; i++) {
        tuple_t * inptr  = (partsR[i]->tuples);
        tuple_t * outptr = (relR.tuples + offset);
        ntuples_per_part       = partsR[i]->num_tuples;
        offset                += ntuples_per_part;

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
    for(i = 0; i < PARTFANOUT; i++) {
        tuple_t * inptr  = (partsS[i]->tuples);
        tuple_t * outptr = (relS.tuples + offset);

        ntuples_per_part       = partsS[i]->num_tuples;
        offset                += ntuples_per_part;
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

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->sort);
    }
#ifdef PERF_COUNTERS
    if(my_tid == 0){
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    /**
     * Step.3.1) Bringing remote runs to local NUMA-node while
     * merging pair-wise at the same time.
     */
    int j, cnt = 0;
    const int perthread = PARTFANOUT / args->nthreads;
    uint64_t offsetR = 0, offsetS = 0;
    uint64_t mergeRtotal = 0, mergeStotal = 0;
    tuple_t *  tmpoutR;
    tuple_t *  tmpoutS;
    relation_t Smergeouts[PARTFANOUT];
    relation_t Rmergeouts[PARTFANOUT];

    if(args->nthreads == 1) {
        /* single threaded execution */
        for(i = 0; i < PARTFANOUT; i ++) {

            relationpair_t * rels = & args->threadrelchunks[my_tid][i];

            /* output offset in the first merge step */
            Rmergeouts[i].tuples = rels->R.tuples;
            Rmergeouts[i].num_tuples = rels->R.num_tuples;
            mergeRtotal += rels->R.num_tuples;

            Smergeouts[i].tuples = rels->S.tuples;
            Smergeouts[i].num_tuples = rels->S.num_tuples;
            mergeStotal += rels->S.num_tuples;
        }
        cnt = PARTFANOUT;
        tmpoutR = tmpR.tuples;
        tmpoutS = tmpS.tuples;
    }
    else {
        /* multi-threaded execution */
        /* merge remote relations and bring to local memory */
        const int start = my_tid * perthread;
        const int end = start + perthread;

        /* compute the size of merged relations to be stored locally */
        for(j = start; j < end; j ++) {
            for(i = 0; i < args->nthreads; i += 2) {

                relationpair_t * rels1 = & args->threadrelchunks[i][j];
                relationpair_t * rels2 = & args->threadrelchunks[i+1][j];

                /* if(rels1->R.num_tuples % 8 != 0 || */
                /*    rels2->R.num_tuples % 8 != 0 || */
                /*    rels1->S.num_tuples % 8 != 0 || */
                /*    rels2->S.num_tuples % 8 != 0 */
                /*    ) */
                /* { */
                /*     DEBUGMSG(1,"THERE IS A PROBLEM WITH ALIGNMENT!\n"); */
                /*     exit(0); */
                /* } */

                mergeRtotal += rels1->R.num_tuples + rels2->R.num_tuples;
                mergeStotal += rels1->S.num_tuples + rels2->S.num_tuples;
            }
        }

        /* allocate memory at local node for temporary merge results */
        tmpoutR = (tuple_t *) malloc(mergeRtotal * sizeof(tuple_t));
        tmpoutS = (tuple_t *) malloc(mergeStotal * sizeof(tuple_t));

        for(j = start; j < end; j ++) {
            for(i = 0; i < args->nthreads; i += 2) {

                // int tid1 = (((my_tid + i)/2) % (args->nthreads/2)) * 2;
                // int tid2 = tid1 + 1;
                int tid1 = (my_tid + i) % (args->nthreads);
                int tid2 = (tid1 + 1) % (args->nthreads);

                relationpair_t * rels1 = & args->threadrelchunks[tid1][j];
                relationpair_t * rels2 = & args->threadrelchunks[tid2][j];

                /* output offset in the first merge step */
                Rmergeouts[cnt].num_tuples = rels1->R.num_tuples
                                             + rels2->R.num_tuples;
                Rmergeouts[cnt].tuples = tmpoutR + offsetR;
                tuple_t * Rmergeout = Rmergeouts[cnt].tuples;
                /*
                if(!is_sorted_helper((int64_t *) rels1->R.tuples, rels1->R.num_tuples))
                {
                    printf("***NOT_SORTED tid=%d, part-j=%d\n", i+1, j);
                }

                if(!is_sorted_helper((int64_t *) rels2->R.tuples, rels2->R.num_tuples))
                {
                    printf("***NOT_SORTED tid=%d, part-j=%d\n", i+1, j);
                }
                */

                if(scalarmergeflag)
                    scalar_merge_tuples(rels1->R.tuples,
                                 rels2->R.tuples,
                                 Rmergeout,
                                 rels1->R.num_tuples,
                                 rels2->R.num_tuples);
                else
                    avx_merge_tuples(rels1->R.tuples,
                                   rels2->R.tuples,
                                   Rmergeout,
                                   rels1->R.num_tuples,
                                   rels2->R.num_tuples);



                /* if(Rmergeouts[cnt].num_tuples > 0 && !is_sorted_helper(Rmergeout, Rmergeouts[cnt].num_tuples)) { */
                /*     printf("===> %d-thread -> MERGE-R is NOT sorted, size = %d\n", my_tid, Rmergeouts[cnt].num_tuples); */
                /*     printf("===> lenA=%d, lenB=%d\n", rels1->R.num_tuples, rels2->R.num_tuples); */

                /* } */


                /* if(!is_sorted_helper(Rmergeout, Rmergeouts[cnt].num_tuples)) { */
                /*     DEBUGMSG(1,"===> %d-thread -> R is NOT sorted, size = %d -- R1=%d, R2=%d\n",  */
                /*              my_tid, Rmergeouts[cnt].num_tuples,  */
                /*              rels1->R.num_tuples, */
                /*              rels2->R.num_tuples); */
                /*     char fn[64]; */
                /*     sprintf(fn,"merge-tid%d.log", my_tid); */
                /*     FILE * log = fopen(fn, "w"); */
                /*     fprintf(log, "==== Input 1 ====\n"); */
                /*     for(int m = 0; m < rels1->R.num_tuples; m++){ */
                /*         fprintf(log, "%llu\n", rels1->R.tuples[m].key); */
                /*     } */
                /*     fprintf(log, "==== Input 2 ====\n"); */
                /*     for(int m = 0; m < rels2->R.num_tuples; m++){ */
                /*         fprintf(log, "%llu\n", rels2->R.tuples[m].key); */
                /*     } */
                /*     fprintf(log, "==== Merge-output ====\n"); */
                /*     for(int m = 0; m < Rmergeouts[cnt].num_tuples; m++){ */
                /*         fprintf(log, "%llu\n", Rmergeout[m]); */
                /*     } */
                /*     fclose(log); */
                /*     exit(0); */
                /* } */

                Smergeouts[cnt].num_tuples = rels1->S.num_tuples + rels2->S.num_tuples;
                Smergeouts[cnt].tuples = tmpoutS + offsetS;
                tuple_t * Smergeout = Smergeouts[cnt].tuples;

                /* if(!is_sorted_helper((int64_t *) rels1->S.tuples, rels1->S.num_tuples)) */
                /* { */
                /*     DEBUGMSG(1,"***NOT_SORTED tid=%d, part-j=%d\n", i+1, j); */
                /* } */

                /* if(!is_sorted_helper((int64_t *) rels2->S.tuples, rels2->S.num_tuples)) */
                /* { */
                /*     DEBUGMSG(1,"***NOT_SORTED tid=%d, part-j=%d\n", i+1, j); */
                /* } */



                if(scalarmergeflag)
                    scalar_merge_tuples(rels1->S.tuples, rels2->S.tuples,
                                 Smergeout,
                                 rels1->S.num_tuples,
                                 rels2->S.num_tuples);
                else
                    avx_merge_tuples(rels1->S.tuples, rels2->S.tuples,
                                   Smergeout,
                                   rels1->S.num_tuples,
                                   rels2->S.num_tuples);

                /*
                if(!is_sorted_helper(Rmergeout, Rmergeouts[cnt].num_tuples))
                    printf("===> %d-thread -> R is NOT sorted, size = %d\n", my_tid,
                           Rmergeouts[cnt].num_tuples);

                if(!is_sorted_helper(Smergeout, Smergeouts[cnt].num_tuples))
                    printf("===> %d-thread -> S is NOT sorted, size = %d\n",
                             my_tid, Smergeouts[cnt].num_tuples);
                */

                offsetR += Rmergeouts[cnt].num_tuples;
                offsetS += Smergeouts[cnt].num_tuples;

                cnt ++;
            }
        }

    }

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->mergedelta);
        DEBUGMSG(1, "First level numa-remote merge is complete!\n");
    }
#ifdef PERF_COUNTERS
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("========== Profiling results of First NUMA-Merge Phase ==========\n");
        PCM_printResults();
        //PCM_cleanup();
        PCM_start();
    }
    BARRIER_ARRIVE(args->barrier, rv);
#endif

    /**
     * Step.3.2) Full merge of NUMA-local runs with a pair-wise multi-pass merge.
     */
    /* Full-merge of relation R */
    tuple_t * tmpoutR2 = (tuple_t *) malloc(mergeRtotal * sizeof(tuple_t));
    tuple_t * reclaimR2 = tmpoutR2; /* for releasing */
    int cntS = cnt;
    for(; cnt > 1; cnt >>= 1) {
        offsetR = 0;
        for(i = 0, j = 0; i < cnt; i += 2) {
            tuple_t * inpA = (Rmergeouts[i].tuples);
            tuple_t * inpB = (Rmergeouts[i+1].tuples);
            tuple_t * out  = (tmpoutR2 + offsetR);

            uint64_t len1 = Rmergeouts[i].num_tuples;
            uint64_t len2 = Rmergeouts[i+1].num_tuples;

            if(scalarmergeflag)
                scalar_merge_tuples(inpA, inpB, out, len1, len2);
            else
                avx_merge_tuples(inpA, inpB, out, len1, len2);

            Rmergeouts[j].tuples     = out;
            Rmergeouts[j].num_tuples = len1 + len2;
            offsetR += Rmergeouts[j].num_tuples;
            j++;
        }
        tuple_t * swap = (tuple_t *) tmpoutR;
        tmpoutR        = (tuple_t *) tmpoutR2;
        tmpoutR2       = (tuple_t *) swap;
    }
    /* clean-up tempoutR2 */
    if(reclaimR2 == tmpoutR2){
        free(reclaimR2);
    }
    else if(my_tid == 0){
        free(tmpoutR2);
    }

    /* Full-merge of relation S */
    tuple_t * tmpoutS2 = (tuple_t *) malloc(mergeStotal * sizeof(tuple_t));
    tuple_t * reclaimS2 = tmpoutS2; /* for releasing */
    cnt = cntS;
    for(; cnt > 1; cnt >>= 1) {
        offsetS = 0;
        for(i = 0, j = 0; i < cnt; i += 2) {
            tuple_t * inpA = (Smergeouts[i].tuples);
            tuple_t * inpB = (Smergeouts[i+1].tuples);
            tuple_t * out  = (tmpoutS2 + offsetS);

            uint64_t len1 = Smergeouts[i].num_tuples;
            uint64_t len2 = Smergeouts[i+1].num_tuples;

            if(scalarmergeflag)
                scalar_merge_tuples(inpA, inpB, out, len1, len2);
            else
                avx_merge_tuples(inpA, inpB, out, len1, len2);

            Smergeouts[j].tuples     = out;
            Smergeouts[j].num_tuples = len1 + len2;
            offsetS += Smergeouts[j].num_tuples;
            j++;
        }
        tuple_t * swap = (tuple_t *) tmpoutS;
        tmpoutS        = (tuple_t *) tmpoutS2;
        tmpoutS2       = (tuple_t *) swap;
    }
    /* clean-up tempoutS2 */
    if(reclaimS2 == tmpoutS2){
        free(reclaimS2);
    }
    else if(my_tid == 0){
        free(tmpoutS2);
    }

    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        stopTimer(&args->merge);
    }
#ifdef PERF_COUNTERS
    BARRIER_ARRIVE(args->barrier, rv);
    if(my_tid == 0) {
        PCM_stop();
        PCM_log("========== Profiling results of Rest of the Local Merge Phase ==========\n");
        PCM_printResults();
        //PCM_cleanup();
    }
#endif

    /* To check whether sorted? */
    /*
    check_sorted((int64_t *)tmpoutR, (int64_t *)tmpoutS,
                 mergeRtotal, mergeStotal, my_tid);
    */

    /* Merge Join */
    tuple_t * rtuples = (tuple_t *) tmpoutR;
    tuple_t * stuples = (tuple_t *) tmpoutS;

#ifdef JOIN_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = chainedtuplebuffer_init();
#else
    void * chainedbuf = NULL;
#endif

    /**
     * Step.4) NUMA-local merge-join on local sorted runs.
     */

    uint64_t nresults = merge_join(rtuples, stuples,
                                   mergeRtotal, mergeStotal, chainedbuf);

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
    if(tmpoutR == reclaimR2){
        free(tmpoutR);
    }
    else if(my_tid == 0){
        free(tmpoutR);
    }
    if(tmpoutS == reclaimS2){
        free(tmpoutS);
    }
    else if(my_tid == 0){
        free(tmpoutS);
    }

    return 0;
}


