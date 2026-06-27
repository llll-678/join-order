/**
 * @file   partitioningbench.c
 * @author Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date   Sat Jun 30 14:35:37 2012
 *
 * @brief  A benchmarking tool for single-threaded radix partitioning
 *
 * (c) 2014, ETH Zurich, Systems Group
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>           /* gettimeofday */
#include <stdlib.h>
#include <string.h>             /* memcpy */
//#include <assert.h>

#include "types.h" /* relation_t, tuple_t */
#include "rdtsc.h"              /* startTimer, stopTimer */
#include "generator.h"
#include "affinity.h"

#include "partition.h" /* partition_relation() */

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

#define ALLOC_ALIGNED(SZ) alloc_aligned(SZ)

int RDXBITS = 14;

/** Cache line aligned memory allocation method */
extern void *
alloc_aligned(size_t sz);

/** get usecs difference between to timeval */
double
get_diff_usec(struct timeval * start, struct timeval * end)
{
    double diff_usec = (((*end).tv_sec*1000000L + (*end).tv_usec)
                            - ((*start).tv_sec*1000000L+(*start).tv_usec));
    return diff_usec;
}

/** print out the execution time statistics */
void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end)
{
    double diff_usec = get_diff_usec(start, end);
    fprintf(stdout, "\nMY-TOTAL-TIME-USECS2: %.4lf \n", diff_usec);
    fprintf(stdout, "TOTAL-TIME-USECS; TUPLES-PER-SECOND = ");
    fflush(stdout);
    fprintf(stdout, "%.4lf %.4lf ",
            diff_usec, (numtuples/(diff_usec/1000000L)));
    fflush(stdout);
    fprintf(stdout, "\n");
    fflush(stdout);
}

int
main(int argc, char *argv[])
{
    struct timeval start, end;
    relation_t relR;

    int64_t ntuples = 4*1024*1024; /* default inp size 4M */

    /* 0: partition, 1: partition_optimized, 2: partition_optimized_v2, 3: histogram_memcpy_bench */
    int whattodo = 0;

    /* start initially on CPU-0 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) <0) {
        perror("sched_setaffinity");
    }

    if(argc <= 1) {
        printf("Usage %s NUMTUPLES WHATTODO RDXBITS\n", argv[0]);
        printf("\tWHATTODO:\t 0 --> partition \n\t\t\t 1 --> part-optimized\n");
        printf("\t\t\t 2 --> part-optimized-V2 \n\t\t\t 3 --> histogram_memcpy_bench\n");
        printf("\t\t\t 4 --> plain memcpy()\n");
        exit(0);
    }

    if(argc >= 2)
        ntuples = atol(argv[1]);

    if(argc >= 3)
        whattodo = atoi(argv[2]);

    if(argc >= 4)
        RDXBITS = atoi(argv[3]);

    /* create relation R */
    fprintf(stdout,
            "[INFO ] Creating relation with size = %.3lf MiB, #tuples = %" PRId64 " : ",
            (double) sizeof(tuple_t) * ntuples/(1024.0*1024.0),
            ntuples);
    fflush(stdout);
    gettimeofday(&start, NULL);
    seed_generator(12345);
    create_relation_pk(&relR, ntuples);
    gettimeofday(&end, NULL);
    fprintf(stdout, " OK -- time: %.4lf usecs \n", get_diff_usec(&start, &end));

    relation_t tmpRelR;

    const int FOUT = 1 << RDXBITS;
    tmpRelR.tuples     = (tuple_t*) ALLOC_ALIGNED(relR.num_tuples
                                                  * sizeof(tuple_t)
                                                  + FOUT*64);
                         /* malloc(relR.num_tuples * sizeof(tuple_t)  */
                         /*                   + FOUT*64); */
    tmpRelR.num_tuples = relR.num_tuples;
    relation_t * partsR[FOUT];
    int i;

#ifdef PERF_COUNTERS
    PCM_initPerformanceMonitor("pcm.cfg", NULL);
    PCM_start();
#endif

    if(whattodo == 0){
        fprintf(stdout, "[INFO ] Running normal partitioning ... \n");
        for(i = 0; i < FOUT; i++) {
            partsR[i] = (relation_t *) malloc(sizeof(relation_t));
        }

        gettimeofday(&start, NULL);

        partition_relation(partsR, &relR, &tmpRelR, RDXBITS, 0);

        gettimeofday(&end, NULL);
        print_timing(ntuples, &start, &end);
    }
    else if(whattodo == 1){
        /*************************************************************/

        fprintf(stdout, "[INFO ] Running optimized partitioning ... \n");

        for(i = 0; i < FOUT; i++) {
            partsR[i] = (relation_t *) malloc(sizeof(relation_t));
        }

        gettimeofday(&start, NULL);

        partition_relation_optimized(partsR, &relR, &tmpRelR, RDXBITS, 0);

        gettimeofday(&end, NULL);

        print_timing(ntuples, &start, &end);
    }
    else if(whattodo == 2){
        /*************************************************************/

        fprintf(stdout, "[INFO ] Running optimized partitioning v2 ... \n");

        for(i = 0; i < FOUT; i++) {
            partsR[i] = (relation_t *) malloc(sizeof(relation_t));
        }

        gettimeofday(&start, NULL);

        partition_relation_optimized_V2(partsR, &relR, &tmpRelR, RDXBITS, 0);

        gettimeofday(&end, NULL);

        print_timing(ntuples, &start, &end);
    }
    else if(whattodo == 3){
        /*************************************************************/
        /* Testing memcpy speed */
        fprintf(stdout, "[INFO ] Running histogram-memcpy bench ... \n");
        gettimeofday(&start, NULL);

        histogram_memcpy_bench(partsR, &relR, &tmpRelR, RDXBITS);

        gettimeofday(&end, NULL);
        print_timing(ntuples, &start, &end);
    }
    else if(whattodo == 4){
        /*************************************************************/
        /* Testing memcpy speed */
        fprintf(stdout, "[INFO ] Running just memcpy ... \n");
        gettimeofday(&start, NULL);

        memcpy(relR.tuples, tmpRelR.tuples, relR.num_tuples * sizeof(tuple_t));

        gettimeofday(&end, NULL);
        print_timing(ntuples, &start, &end);
    }

#ifdef PERF_COUNTERS
    PCM_stop();
    PCM_log("========== Profiling results ==========\n");
    PCM_printResults();
    PCM_cleanup();
#endif

    /* cleanup */
    free(relR.tuples);
    free(tmpRelR.tuples);

    return 0;
}
