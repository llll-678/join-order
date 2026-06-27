/**
 * @file    joincommon.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:39:54 2012
 * @version $Id $
 *
 * @brief   Common structures, macros and functions of sort-merge join algorithms.
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */

#include <stdio.h>
#include <stdlib.h> /* posix_memalign() */

#include "joincommon.h"
#include "cpu_mapping.h"        /* get_cpu_id() */
#include "affinity.h"           /* CPU_SET, CPU_ZERO */

#define REQUIRED_STACK_SIZE (32*1024*1024)

/** Global command line arguments, whether to execute scalar code */
int scalarsortflag  = 0; /* by default AVX-code is executed */
int scalarmergeflag = 0;

result_t *
sortmergejoin_initrun(relation_t * relR,relation_t * relS, int nthreads,
                      void * (*jointhread)(void *))
{
    int i, rv;
    pthread_t tid[nthreads];
    pthread_attr_t attr;
    pthread_barrier_t barrier;
    cpu_set_t set;
    arg_t args[nthreads];

    tuple_t * tmpRelR, * tmpRelS;
    int32_t numperthr[2];
    int64_t result = 0;


    /* allocate temporary space for partitioning */
    /* tmpRelR = (tuple_t*) malloc(relR->num_tuples * sizeof(tuple_t)  */
    /*                             + RELATION_PADDING + nthreads*PADDINGX8); */
    /* tmpRelS = (tuple_t*) malloc(relS->num_tuples * sizeof(tuple_t)  */
    /*                             + RELATION_PADDING + nthreads*PADDINGX8); */

    tmpRelR = (tuple_t*) malloc_aligned(relR->num_tuples * sizeof(tuple_t)
                                       + RELATION_PADDING
                                       + nthreads*PADDINGX8*sizeof(tuple_t));
    tmpRelS = (tuple_t*) malloc_aligned(relS->num_tuples * sizeof(tuple_t)
                                       + RELATION_PADDING
                                       + nthreads*PADDINGX8*sizeof(tuple_t));

    /* this is just to make sure that chunks of the temporary memory
       will be numa local to threads. TODO: re-factor! */
    numa_localize(tmpRelR, relR->num_tuples, nthreads);
    numa_localize(tmpRelS, relS->num_tuples, nthreads);

    relationpair_t ** threadrelchunks;
    threadrelchunks = (relationpair_t **) malloc(nthreads *
                                                sizeof(relationpair_t*));

    /* allocate histograms arrays, actual allocation is local to threads */
    uint32_t ** histR = (uint32_t**) malloc(nthreads * sizeof(uint32_t*));

    rv = pthread_barrier_init(&barrier, NULL, nthreads);
    if(rv != 0){
        printf("[ERROR] Couldn't create the barrier\n");
        exit(EXIT_FAILURE);
    }

    /*  Initialize the attribute */
    pthread_attr_init(&attr);
    int             err = 0;
    size_t          stackSize = 0;

    /* Get the default value */
    err = pthread_attr_getstacksize(&attr, &stackSize);
    if (err) {
        perror("[ERROR] pthread stack size could not be get!");
        exit(0);
    }

    /* set the attribute with our required value */
    if (stackSize < REQUIRED_STACK_SIZE) {
        err = pthread_attr_setstacksize (&attr, REQUIRED_STACK_SIZE);
        if (err) {
            perror("[ERROR] pthread stack size could not be set!");
            exit(0);
        }
    }

    /* first assign chunks of relR & relS for each thread */
    numperthr[0] = relR->num_tuples / nthreads;
    numperthr[1] = relS->num_tuples / nthreads;

    result_t * joinresult;
    joinresult = (result_t *) malloc(sizeof(result_t));
    joinresult->resultlist = (threadresult_t *) malloc(sizeof(threadresult_t)
                                                       * nthreads);

    int num_numa = get_num_numa_regions();
    tuple_t ** ptrs_to_sharedmergebufs = malloc_aligned(num_numa*sizeof(tuple_t*));

    for(i = 0; i < nthreads; i++){
        int cpu_idx = get_cpu_id(i);
        numa_thread_mark_active(cpu_idx);
        DEBUGMSG(1, "Assigning thread-%d to CPU-%d\n", i, cpu_idx);

        CPU_ZERO(&set);
        CPU_SET(cpu_idx, &set);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &set);

        args[i].relR = relR->tuples + i * (numperthr[0]);
        args[i].tmpR = tmpRelR + i * (numperthr[0] + PADDINGX8);// + SMALL_PADDING_TUPLES);

        args[i].relS = relS->tuples + i * (numperthr[1]);
        args[i].tmpS = tmpRelS + i * (numperthr[1] + PADDINGX8);// + SMALL_PADDING_TUPLES);

        args[i].numR = (i == (nthreads-1)) ?
            (relR->num_tuples - i * numperthr[0]) : numperthr[0];
        args[i].numS = (i == (nthreads-1)) ?
            (relS->num_tuples - i * numperthr[1]) : numperthr[1];

        args[i].my_tid        = i;
        args[i].barrier       = &barrier;
        args[i].nthreads      = nthreads;
        args[i].threadrelchunks = threadrelchunks;
        args[i].sharedmergebuffer = ptrs_to_sharedmergebufs;

        /** information specific to mpsm-join */
        args[i].histR         = histR;
        args[i].tmpRglobal    = tmpRelR;
        args[i].totalR        = relR->num_tuples;

#ifdef JOIN_MATERIALIZE
        args[i].threadresult        = &(joinresult->resultlist[i]);
#endif

        /* run the selected join algorithm thread */
        rv = pthread_create(&tid[i], &attr, jointhread, (void*)&args[i]);

        if (rv) {
            printf("[ERROR] return code from pthread_create() is %d\n", rv);
            exit(-1);
        }

    }

    /* wait for threads to finish */
    for(i = 0; i < nthreads; i++){
        pthread_join(tid[i], NULL);
        result += args[i].result;
    }
    joinresult->totalresults = result;
    joinresult->nthreads     = nthreads;

    /* stats */
    uint64_t total = args[0].join;
    fprintf(stdout, "Total, Partitioning, Sort, First-Merge, Merge, Join\n");
    fflush(stdout);
    fprintf(stdout, "%llu, %llu, %llu, %llu, %llu, %llu\n", total, args[0].part,
            args[0].sort, args[0].mergedelta, args[0].merge, args[0].join);
    fflush(stdout);
    fprintf(stdout, "Perstage: ");
    fflush(stdout);
    fprintf(stdout, "%llu, %llu, %llu, %llu, %llu, ",
            args[0].part,
            args[0].sort - args[0].part,
            args[0].mergedelta - args[0].sort,
            args[0].merge - args[0].mergedelta,
            args[0].join - args[0].merge);
    fflush(stdout);
    fprintf(stdout, "\n");

#ifndef NO_TIMING
    /* now print the timing results: */
    print_timing(relS->num_tuples, &args[0].start, &args[0].end, stdout);
#endif

    /* clean-up */
    free(threadrelchunks);
    free(ptrs_to_sharedmergebufs);
    //free(tmpRelR);
    //free(tmpRelS);
    free(histR);
    pthread_barrier_destroy(&barrier);

    return joinresult;
}

void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end,
             FILE * out)
{
    double diff_usec = (((*end).tv_sec*1000000L + (*end).tv_usec)
                        - ((*start).tv_sec*1000000L+(*start).tv_usec));
    fprintf(stdout, "\nMY-TOTAL-TIME-USECS4: %.4lf \n", diff_usec);
    fprintf(out, "NUM-TUPLES = %lld TOTAL-TIME-USECS = %.4lf ", numtuples, diff_usec);
    fprintf(out, "TUPLES-PER-SECOND = ");
    fflush(out);

    fprintf(out, "%.4lf ", (numtuples/(diff_usec/1000000L)));
    fflush(out);
}

/**
 * Does merge join on two sorted relations. Just a naive scalar
 * implementation. TODO: consider AVX for this code.
 *
 * @param rtuples sorted relation R
 * @param stuples sorted relation S
 * @param numR number of tuples in R
 * @param numS number of tuples in S
 * @param output join results, if JOIN_MATERIALE defined.
 */
uint64_t
merge_join(tuple_t * rtuples, tuple_t * stuples,
           const uint64_t numR, const uint64_t numS, void * output)
{
    uint64_t i = 0, j = 0;
    uint64_t matches = 0;

    /* if(is_sorted_helper((int64_t*)rtuples, numR)) */
    /*     printf("merge_join() -> R is sorted, size = %d\n", numR); */
    /* else */
    /*     printf("merge_join() -> R is NOT sorted, size = %d\n", numR); */

    /* if(is_sorted_helper((int64_t*)stuples, numS)) */
    /*     printf("merge_join() -> S is sorted, size = %d\n", numS); */
    /* else */
    /*     printf("merge_join() -> S is NOT sorted, size = %d\n", numS); */

#ifdef JOIN_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = (chainedtuplebuffer_t *) output;
#endif

    while( i < numR && j < numS ) {
        if( rtuples[i].key < stuples[j].key )
            i ++;
        else if(rtuples[i].key > stuples[j].key)
            j ++;
        else {

#ifdef JOIN_MATERIALIZE
            tuple_t * outtuple = cb_next_writepos(chainedbuf);
            *outtuple = stuples[j];
#endif

            matches ++;
            //i ++;
            j ++;
        }
    }
    /* printf("More-S = %d, More-R = %d, remS=%d\n", (j<numS), (i<numR), numS-j); */
    /* if(rtuples[numR-1].key == stuples[j].key) */
    /*     printf("lastS equal lastR = %d\n", 1); */
    /* matches = merge_join8((int64_t *)rtuples, (int64_t*)stuples, 0, numR); */

    return matches;
}

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
                         void * output)
{
    uint64_t i = 0, j = 0, k = 0;
    uint64_t matches = 0;

#ifdef JOIN_MATERIALIZE
    chainedtuplebuffer_t * chainedbuf = (chainedtuplebuffer_t *) output;
#endif

    /* find search start index with interpolation, only 2-steps */
    if( rtuples[0].key > stuples[0].key ) {
        double r0 = rtuples[0].key;
        double s0 = stuples[0].key;
        double sN = stuples[numS-1].key;
        double sK;

        k = (numS - 1) * (r0 - s0) / (sN - s0) + 1;

        sK = stuples[k].key;

        k = (k - 1) * (r0 - s0) / (sK - s0) + 1;

        /* go backwards for the interpolation error */
        while(stuples[k].key >= r0)
            k--;

        j = k;
    }
    else if( rtuples[0].key < stuples[0].key ){
        double s0 = stuples[0].key;
        double r0 = rtuples[0].key;
        double rN = rtuples[numS-1].key;
        double rK;

        k = (numR - 1) * (s0 - r0) / (rN - r0) + 1;

        rK = rtuples[k].key;

        k = (k - 1) * (s0 - r0) / (rK - r0) + 1;

        /* go backwards for the interpolation error */
        while(rtuples[k].key >= s0)
            k--;

        i = k;
    }

    while( i < numR && j < numS ) {
        if( rtuples[i].key < stuples[j].key )
            i ++;
        else if(rtuples[i].key > stuples[j].key)
            j ++;
        else {

#ifdef JOIN_MATERIALIZE
            tuple_t * outtuple = cb_next_writepos(chainedbuf);
            *outtuple = stuples[j];
#endif

            matches ++;
            //i ++;
            j ++;
        }
    }

    return matches;
}


void *
malloc_aligned(size_t size)
{
    void * ret;
    int rv;
    rv = posix_memalign((void**)&ret, CACHE_LINE_SIZE, size);

    if (rv) {
        perror("[ERROR] malloc_aligned() failed: out of memory");
        return 0;
    }

    return ret;
}


int
is_sorted_helper(int64_t * items, uint64_t nitems)
{
#if defined(KEY_8B)

    intkey_t curr = 0;
    uint64_t i;

    tuple_t * tuples = (tuple_t *) items;

    for (i = 0; i < nitems; i++)
    {
        if(tuples[i].key < curr)
            return 0;

        curr = tuples[i].key;
    }

    return 1;

#else

    /* int64_t curr = 0; */
    /* uint64_t i; */

    /* for (i = 0; i < nitems; i++) */
    /* { */
    /*     if(items[i] < curr){ */
    /*         printf("ERR: item[%d]=%llu ; item[%d]=%llu\n", i, items[i], i-1, curr); */
    /*         /\* return 0; *\/ */
    /*         exit(0); */
    /*     } */

    /*     curr = items[i]; */
    /* } */

    /* return 1; */

    /* int64_t curr = 0; */
    /* uint64_t i; */
    /* int warned = 0; */
    /* for (i = 0; i < nitems; i++) */
    /* { */
    /*     if(items[i] == curr) { */
    /*         if(!warned){ */
    /*             printf("[WARN ] Equal items, still ok... "\ */
    /*                    "item[%d]=%lld is equal to item[%d]=%lld\n", */
    /*                    i, items[i], i-1, curr); */
    /*             warned =1; */
    /*         } */
    /*     } */
    /*     else if(items[i] < curr){ */
    /*         printf("[ERROR] item[%d]=%lld is less than item[%d]=%lld\n", */
    /*                i, items[i], i-1, curr); */
    /*         return 0; */
    /*     } */

    /*     curr = items[i]; */
    /* } */

    /* return 1; */

    intkey_t curr = 0;
    uint64_t i;
    int warned = 0;
    tuple_t * tuples = (tuple_t *)items;
    for (i = 0; i < nitems; i++)
    {
        if(tuples[i].key == curr) {
            if(!warned){
                printf("[WARN ] Equal items, still ok... "\
                       "item[%d].key=%d is equal to item[%d].key=%d\n",
                       i, tuples[i].key, i-1, curr);
                warned =1;
            }
        }
        else if(tuples[i].key < curr){
            printf("[ERROR] item[%d].key=%d is less than item[%d].key=%d\n",
                   i, tuples[i].key, i-1, curr);
            return 0;
        }

        curr = tuples[i].key;
    }

    return 1;

#endif
}

/** utility method to check whether arrays are sorted */
void
check_sorted(int64_t * R, int64_t * S, uint64_t nR, uint64_t nS, int my_tid)
{

    if(is_sorted_helper(R, nR))
        printf("%d-thread -> R is sorted, size = %d\n", my_tid, nR);
    else
        printf("%d-thread -> R is NOT sorted, size = %d\n", my_tid, nR);

    if(is_sorted_helper(S, nS))
        printf("%d-thread -> S is sorted, size = %d\n", my_tid, nS);
    else
        printf("%d-thread -> S is NOT sorted, size = %d\n", my_tid, nS);
}

