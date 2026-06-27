#include <stdint.h>
#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include <time.h>
/* #include <limits.h> */

#include "testutil.h"
#include "generator.h"
#include "partition.h"

/* #define CK_FORK "no" */
#define MAXTESTSIZE (1<<24)
#define MINTESTSIZE (1<<20)
#define MAXRDXBITS 16
#define MINRDXBITS 2
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

#define ALLOC_ALIGNED(SZ) alloc_aligned(SZ)

/** Cache line aligned memory allocation method */
extern void *
alloc_aligned(size_t sz);

/** Test partitioning correctness with randomly shuffled sequential keys */
START_TEST (test_partitioning_correctness_seqkeys)
{
    int64_t ntuples = rand() % (MAXTESTSIZE - MINTESTSIZE) + MINTESTSIZE;
    relation_t relR;
    /* create sequential but randomly shuffled keys */
    create_relation_pk(&relR, ntuples);
    int genseed = time(NULL);
    fprintf(stderr, "[INFO ] partitioning_correctness_seqkeys seed value is %d\n", genseed);
    seed_generator(genseed);

    /* test radix bits between min and max */
    int RDXBITS = rand() % (MAXRDXBITS - MINRDXBITS) + MINRDXBITS;
    const int FOUT = 1 << RDXBITS;

    fprintf(stderr, "[INFO ] Partitioning with %" PRId64 " tuples and fanout = %d\n",
            ntuples, FOUT);

    relation_t * partsR_normal[FOUT];
    relation_t tmpRelR_normal;
    tmpRelR_normal.tuples     = (tuple_t*) ALLOC_ALIGNED(relR.num_tuples
                                                      * sizeof(tuple_t)
                                                      + FOUT*64);
    tmpRelR_normal.num_tuples = relR.num_tuples;
    for(int i = 0; i < FOUT; i++) {
        partsR_normal[i] = (relation_t *) malloc(sizeof(relation_t));
    }

    /* first normal partitioning */
    partition_relation(partsR_normal, &relR, &tmpRelR_normal, RDXBITS, 0);

    /* then optimized partitioning */
    relation_t * partsR[FOUT];
    relation_t tmpRelR;
    tmpRelR.tuples     = (tuple_t*) ALLOC_ALIGNED(relR.num_tuples
                                                       * sizeof(tuple_t)
                                                       + FOUT*64);
    tmpRelR.num_tuples = relR.num_tuples;
    for(int i = 0; i < FOUT; i++) {
        partsR[i] = (relation_t *) malloc(sizeof(relation_t));
    }

    partition_relation_optimized(partsR, &relR, &tmpRelR, RDXBITS, 0);

    /* first check for alignment of output partitions */
    for(int i = 0; i < FOUT; i++){
        tuple_t * parti = partsR[i]->tuples;
        ck_assert_int_eq ((((uintptr_t)parti) % CACHE_LINE_SIZE), 0);
    }

    /* int totalpadding = partsR[FOUT-1]->tuples
            - partsR[0]->tuples + partsR[FOUT-1]->num_tuples; */
    int totpad = 0;
    /* then check for correctness with normal partitioning */
    for(int i = 0; i < FOUT; i++){
        relation_t * relnormal = partsR_normal[i];
        relation_t * relopt = partsR[i];
        ck_assert_int_eq (relnormal->num_tuples, relopt->num_tuples);
        for(uint64_t j = 0; j < relopt->num_tuples; j++) {
            if(relopt->tuples[j].key != relnormal->tuples[j].key){
                ck_assert_int_eq(relopt->tuples[j].key, relnormal->tuples[j].key);
            }
        }
        if(i < FOUT-1)
            totpad += sizeof(tuple_t) - ((relnormal->num_tuples) % sizeof(tuple_t));
    }
    //ck_assert_msg(((int)(totalpadding-relR.num_tuples))==totpad,
    //        "[WARN ] total padding = %d -- actual req'd = %d\n",
    //        ((int)(totalpadding-relR.num_tuples)), totpad);

    /* clean up */
    free(relR.tuples);
    free(tmpRelR.tuples);
    free(tmpRelR_normal.tuples);
    for(int i = 0; i < FOUT; i++) {
        free(partsR_normal[i]);
        free(partsR[i]);
    }
}
END_TEST

/** Test partitioning correctness of optimizedV2 with randomly shuffled sequential keys */
START_TEST (test_partitioning_correctness_optV2)
{
    int64_t ntuples = rand() % (MAXTESTSIZE - MINTESTSIZE) + MINTESTSIZE;
    relation_t relR;
    /* create sequential but randomly shuffled keys */
    create_relation_pk(&relR, ntuples);
    int genseed = time(NULL);
    fprintf(stderr, "[INFO ] partitioning_correctness_optV2 seed value is %d\n", genseed);
    seed_generator(genseed);

    /* test radix bits between min and max */
    int RDXBITS = rand() % (MAXRDXBITS - MINRDXBITS) + MINRDXBITS;
    const int FOUT = 1 << RDXBITS;

    fprintf(stderr, "[INFO ] Partitioning with %" PRId64 " tuples and fanout = %d\n",
            ntuples, FOUT);

    relation_t * partsR_normal[FOUT];
    relation_t tmpRelR_normal;
    tmpRelR_normal.tuples     = (tuple_t*) ALLOC_ALIGNED(relR.num_tuples
                                                      * sizeof(tuple_t)
                                                      + FOUT*64);
    tmpRelR_normal.num_tuples = relR.num_tuples;
    for(int i = 0; i < FOUT; i++) {
        partsR_normal[i] = (relation_t *) malloc(sizeof(relation_t));
    }

    /* first normal partitioning */
    partition_relation(partsR_normal, &relR, &tmpRelR_normal, RDXBITS, 0);

    /* then optimized partitioning */
    relation_t * partsR[FOUT];
    relation_t tmpRelR;
    tmpRelR.tuples     = (tuple_t*) ALLOC_ALIGNED(relR.num_tuples
                                                       * sizeof(tuple_t)
                                                       + FOUT*64);
    tmpRelR.num_tuples = relR.num_tuples;
    for(int i = 0; i < FOUT; i++) {
        partsR[i] = (relation_t *) malloc(sizeof(relation_t));
    }

    partition_relation_optimized_V2(partsR, &relR, &tmpRelR, RDXBITS, 0);

    /* first check for alignment of output partitions */
    for(int i = 0; i < FOUT; i++){
        tuple_t * parti = partsR[i]->tuples;
        ck_assert_int_eq ((((uintptr_t)parti) % CACHE_LINE_SIZE), 0);
    }

    int TPL = CACHE_LINE_SIZE / sizeof(tuple_t);
    /* int totalpadding = partsR[FOUT-1]->tuples - partsR[0]->tuples
                        + partsR[FOUT-1]->num_tuples - relR.num_tuples
                        + TPL - (partsR[FOUT-1]->num_tuples % TPL); */
    int totpad = 0;
    /* then check for correctness with normal partitioning */
    for(int i = 0; i < FOUT; i++){
        relation_t * relnormal = partsR_normal[i];
        relation_t * relopt = partsR[i];
        ck_assert_int_eq (relnormal->num_tuples, relopt->num_tuples);
        for(uint64_t j = 0; j < relopt->num_tuples; j++) {
            if(relopt->tuples[j].key != relnormal->tuples[j].key){
                ck_assert_int_eq(relopt->tuples[j].key, relnormal->tuples[j].key);
            }
        }
        totpad += TPL - ((relnormal->num_tuples) % TPL);
    }
    //ck_assert_msg(totalpadding==totpad,
    //        "[WARN ] total padding = %d -- actual req'd = %d\n",
    //        totalpadding, totpad);

    /* clean up */
    free(relR.tuples);
    free(tmpRelR.tuples);
    free(tmpRelR_normal.tuples);
    for(int i = 0; i < FOUT; i++) {
        free(partsR_normal[i]);
        free(partsR[i]);
    }
}
END_TEST

Suite *
partitioning_suite (void)
{
    Suite *s = suite_create ("partitioningtests");

    int seed = time(NULL);
    fprintf(stderr, "[INFO ] partitioningtests seed value is %d\n", seed);
    srand(seed);

    /* Core test case */
    TCase *tc = tcase_create ("correctness");
    tcase_add_test (tc, test_partitioning_correctness_seqkeys);
    tcase_add_test (tc, test_partitioning_correctness_optV2);
    tcase_set_timeout(tc, 0);

    suite_add_tcase (s, tc);

    return s;
}

int
main (void)
{
    int number_failed;
    Suite *s = partitioning_suite ();
    SRunner *sr = srunner_create (s);
    //srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

