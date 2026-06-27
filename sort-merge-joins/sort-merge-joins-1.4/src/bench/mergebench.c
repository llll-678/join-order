/**
 * @file    mergebench.c
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Tue Dec 18 18:20:10 2012
 * @version $Id $
 *
 * @brief   A microbenchmark for 2-way AVX merging kernels of different size.
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
#include "avxsort_core.h"       /* mergeX_varlen(), mergeX_varlen_aligned() */

/** Size of lists to merge */
#define MERGE_LIST_SIZE (20*1000*1000)

/** Cache line aligned memory allocation method */
extern void *
malloc_aligned(size_t sz);

/** print out the execution time statistics of the merge */
void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end)
{
    double diff_usec = (((*end).tv_sec*1000000L + (*end).tv_usec)
                        - ((*start).tv_sec*1000000L+(*start).tv_usec));
                        fprintf(stdout, "\nMY-TOTAL-TIME-USECS1: %.4lf \n", diff_usec);
    fprintf(stdout, "TOTAL-TIME-USECS = %.4lf, TUPLES-PER-SECOND = %.4lf \n",
            diff_usec, (numtuples/(diff_usec/1000000L)));
}

extern int
keycmp(const void * k1, const void * k2);

/**
 * @addtogroup MicroBenchmarks
 * Microbenchmarks for 2-way AVX merging kernels of different size.
 * @{
 */

/**
 * A microbenchmark for 2-way merging kernels (_varlen).
 *
 * @param width size of the kernel to use : 4, 8, 16
 * @param size of list 1
 * @param size of list 2
 */
void
merge_bench(int width, int size1, int size2){

    int64_t * input = (int64_t *) malloc(sizeof(int64_t)*(size1+size2));
    int64_t * output = (int64_t *) malloc(sizeof(int64_t)*(size1+size2));
    int64_t * validate = (int64_t *) malloc(sizeof(int64_t)*(size1+size2));
    int64_t * A, * B;

    int64_t startA, startB;
    uint32_t j;
    uint32_t INCRMOD = 500;

    int32_t maxint = ~(1 << 31) - INCRMOD;

    A = input;
    B = input + size1;

    startA = rand() % INCRMOD;
    startB = rand() % INCRMOD;

    /* generate random increasing order data */
    for(j = 0; j < size1; j++) {
        A[j] = startA;

        if(startA < maxint)
            startA += rand() % INCRMOD;
    }

    for(j = 0; j < size2; j++) {
        B[j] = startB;

        if(startB < maxint)
            startB += rand() % INCRMOD;
    }

    memcpy(validate, input, size1 * sizeof(int64_t));
    memcpy(validate+size1, input+size1, size2 * sizeof(int64_t));

    qsort (validate, size1+size2, sizeof (int64_t), keycmp);

    struct timeval start, end;
    gettimeofday(&start, NULL);
    if(width == 16){
        merge16_varlen(input, input+size1, output, size1, size2);
        /* merge16(input, input+size1, output, size1); */
    }
    else if(width == 8){
        merge8_varlen(input, input+size1, output, size1, size2);
    }
    else if(width == 4){
        merge4_varlen(input, input+size1, output, size1, size2);
    }
    gettimeofday(&end, NULL);

    if(!is_array_equal(output, validate, (size1+size2), (size1+size2))){
        fprintf (stdout, "[ERROR] Error in merge-kernel-%d (_varlen)\n", width);
    }
    else{
        int size = (size1+size2)-((size1+size2)/2)*2;
        if(size > 0) {
            if(output[size1+size2-1] <= output[size1+size2-2]) {
                fprintf (stdout, "[ERROR] Error in merge-kernel-%d (_varlen)\n", width);
            }
        }
    }

    fprintf(stdout, "[INFO ] merge%d_varlen( %d,%d )\t==>\t", width, size1, size2);
    print_timing((size1+size2), &start, &end);

    free(input);
    free(output);
    free(validate);
}

/**
 * A microbenchmark for 2-way merging kernels (_varlen_aligned).
 *
 * @param width size of the kernel to use : 4, 8, 16
 * @param size of list 1
 * @param size of list 2
 */
void
merge_bench_aligned(int width, int size1, int size2){

    int64_t * input = (int64_t *) malloc_aligned(sizeof(int64_t)*(size1+size2));
    int64_t * output = (int64_t *) malloc_aligned(sizeof(int64_t)*(size1+size2));
    int64_t * validate = (int64_t *) malloc_aligned(sizeof(int64_t)*(size1+size2));
    int64_t * A, * B;

    int64_t startA, startB;
    uint32_t j;
    uint32_t INCRMOD = 500;

    int32_t maxint = ~(1 << 31) - INCRMOD;

    A = input;
    B = input + size1;

    startA = rand() % INCRMOD;
    startB = rand() % INCRMOD;

    /* generate random increasing order data */
    for(j = 0; j < size1; j++) {
        A[j] = startA;

        if(startA < maxint)
            startA += rand() % INCRMOD;
    }

    for(j = 0; j < size2; j++) {
        B[j] = startB;

        if(startB < maxint)
            startB += rand() % INCRMOD;
    }

    memcpy(validate, input, size1 * sizeof(int64_t));
    memcpy(validate+size1, input+size1, size2 * sizeof(int64_t));

    qsort (validate, size1+size2, sizeof (int64_t), keycmp);

    struct timeval start, end;
    gettimeofday(&start, NULL);
    if(width == 16){
        merge16_varlen(input, input+size1, output, size1, size2);
    }
    else if(width == 8){
        merge8_varlen(input, input+size1, output, size1, size2);
    }
    else if(width == 4){
        merge4_varlen(input, input+size1, output, size1, size2);
    }
    gettimeofday(&end, NULL);

    if(!is_array_equal(output, validate, (size1+size2), (size1+size2))){
        fprintf (stdout, "[ERROR] Error in merge-kernel-%d (_varlen_aligned)\n", width);
    }
    else{
        int size = (size1+size2)-((size1+size2)/2)*2;
        if(size > 0) {
            if(output[size1+size2-1] <= output[size1+size2-2]) {
                fprintf (stdout, "[ERROR] Error in merge-kernel-%d (_varlen_aligned)\n", width);
            }
        }
    }

    fprintf(stdout, "[INFO ] merge%d_varlen_aligned( %d,%d )\t==>\t", width, size1, size2);
    print_timing((size1+size2), &start, &end);

    free(input);
    free(output);
    free(validate);
}
/** @} */

int
main(void)
{
    /* start initially on CPU-0 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) <0) {
        perror("sched_setaffinity");
    }

    int seed = time(NULL);
    fprintf(stdout, "[INFO ] seed = %d\n", seed);
    srand(seed);

    /* equalsize whether input lists are of equal size? */
    int equalsize = 1;

    unsigned int size1 = MERGE_LIST_SIZE + (rand() % 1000000);
    unsigned int size2;

    if(equalsize){
        size2 = size1 & ~0xF;
        size1 = size2;
    }
    else {
        size2 = MERGE_LIST_SIZE + (rand() % 1000000);
    }

    int kernelwidth[] = {4, 8, 16};
    int numrepetitions = 3;

    /* first unaligned varlen mergekernels */
    for(int i = 0; i < 3; i++){
        for(int rep = 0; rep < numrepetitions; rep++){
            merge_bench(kernelwidth[i], size1, size2);
        }
    }
    /* then aligned varlen mergekernels */
    for(int i = 0; i < 3; i++){
        for(int rep = 0; rep < numrepetitions; rep++){
            merge_bench_aligned(kernelwidth[i], size1, size2);
        }
    }

    return 0;
}
