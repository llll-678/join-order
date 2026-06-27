#include <stdint.h>
#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include <time.h>

#include "testutil.h"
#include "merge.h"
#include "scalar_multiwaymerge.h"
#include "avx_multiwaymerge.h"
#include "avxsort_core.h"

/* #define CK_FORK "no" */
#define MAXTESTSIZE (1<<24)
#define MINTESTSIZE (1<<20)
/* to determine the merge fan-in */
#define MAXFANIN (12) /* 2^12 */
#define MINFANIN (2)  /* 2^2 */
/* size of each chunks to merge */
#define MAXCHUNKSIZE (16) /* 2^16 */
#define MINCHUNKSIZE (15) /* 2^15 */
#define MAXINPUTSIZE (1<<25) /* 32M */

/* number of tests to run for merge-kernel testing */
#define NUM_MERGEKERNEL_TESTS 128

#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

/* random number seed */
int seed;

static inline int __attribute__((always_inline))
tuplekeycmp(const void * k1, const void * k2)
{
    int val = ((tuple_t *)k1)->key - ((tuple_t *)k2)->key;
    return val;
}

extern int
keycmp(const void * k1, const void * k2);

/** Test 2-way merge correctness */
START_TEST (test_merge)
{
    int64_t size1 = rand() % (MAXTESTSIZE - MINTESTSIZE) + MINTESTSIZE;
    int64_t size2 = rand() % (MAXTESTSIZE - MINTESTSIZE) + MINTESTSIZE;
    /** \todo FIXME There is a bug when size2 = size1 and eqlen enabled in merge.c */

    tuple_t * input1 = generate_rand_ordered_tuples(size1);
    tuple_t * input2 = generate_rand_ordered_tuples(size2);
    tuple_t * output = (tuple_t *) malloc_aligned(sizeof(tuple_t)*(size1+size2));
    tuple_t * validate = (tuple_t *) malloc_aligned(sizeof(tuple_t)*(size1+size2));

    fprintf(stderr, "[INFO ] mergetests on input1(%" PRId64 ") and input2(%" PRId64 ")\n",
            size1, size2);

    memcpy(validate, input1, size1 * sizeof(tuple_t));
    memcpy(validate+size1, input2, size2 * sizeof(tuple_t));

    qsort (validate, size1+size2, sizeof (tuple_t), tuplekeycmp);

    uint64_t num = scalar_merge_tuples(input1, input2, output, size1, size2);
    ck_assert_int_eq(is_array_equal((int64_t*)validate, (int64_t*)output, (size1+size2), num), 1);

    memset(output, 0, size1+size2);
    num = avx_merge_tuples(input1, input2, output, size1, size2);
    ck_assert_int_eq(is_array_equal((int64_t*)validate, (int64_t*)output, (size1+size2), num), 1);

    free(input1);
    free(input2);
    free(output);
    free(validate);
}
END_TEST

/** Test multi-way merge correctness */
START_TEST (test_multiwaymerge)
{
    /* test radix bits between min and max */
    int f = rand() % (MAXFANIN - MINFANIN) + MINFANIN;
    int fanIn = (1 << f);

    uint32_t L3buffersize = 4*1024*1024;
    relation_t outrel;
    uint64_t ntuples = 0;

    relation_t chunks[fanIn];
    relation_t chunks_cpy[fanIn];

    int64_t chunksize = 1 << (rand() % (MAXCHUNKSIZE-MINCHUNKSIZE) + MINCHUNKSIZE);
    if((fanIn * chunksize) > MAXINPUTSIZE){
        chunksize = MAXINPUTSIZE / fanIn;
    }

    for(int i = 0; i < fanIn; i++){
        chunks[i].tuples = generate_rand_ordered_tuples(chunksize);
        chunks[i].num_tuples = chunksize;

        chunks_cpy[i].num_tuples = chunksize;
        chunks_cpy[i].tuples = chunks[i].tuples;

        ntuples += chunksize;
    }

    relation_t * chunkptrs[fanIn];

    for(int i = 0; i < fanIn; i++) {
        chunkptrs[i] = &chunks[i];
    }

    uint32_t bufntuples = L3buffersize/sizeof(tuple_t);
    tuple_t * fifobuffer = (tuple_t *) malloc_aligned(L3buffersize);

    outrel.num_tuples = ntuples;
    posix_memalign((void**)&outrel.tuples, CACHE_LINE_SIZE, ntuples * sizeof(tuple_t));

    fprintf(stderr, "[INFO ] merge fan-in=%d, chunksize=%" PRId64 ", total tuples %" PRId64 "\n",
            fanIn, chunksize, ntuples);
    uint64_t num;

    /* 1st test: scalar_multiway_merge() */
    num = scalar_multiway_merge(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);
    ck_assert_int_eq (num, ntuples);
    ck_assert_int_eq (is_sorted_tuples(outrel.tuples, ntuples), 1);
    memset(outrel.tuples, 0, ntuples * sizeof(tuple_t));
    for(int i = 0; i < fanIn; i++) {
        chunks[i] = chunks_cpy[i];
        chunkptrs[i] = &chunks[i];
    }

    /* 2nd test: scalar_multiway_merge_modulo() */
    num = scalar_multiway_merge_modulo(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);
    ck_assert_int_eq (num, ntuples);
    ck_assert_int_eq (is_sorted_tuples(outrel.tuples, ntuples), 1);
    memset(outrel.tuples, 0, ntuples * sizeof(tuple_t));
    for(int i = 0; i < fanIn; i++) {
        chunks[i] = chunks_cpy[i];
        chunkptrs[i] = &chunks[i];
    }

    /* 3rd test: avx_multiway_merge() */
    num = avx_multiway_merge(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);
    ck_assert_int_eq (num, ntuples);
    ck_assert_int_eq (is_sorted_tuples(outrel.tuples, ntuples), 1);
    for(int i = 0; i < fanIn; i++) {
        chunks[i] = chunks_cpy[i];
        chunkptrs[i] = &chunks[i];
    }


    /* 4th test: scalar_multiway_merge_bitand() */
    /*
    num = scalar_multiway_merge_bitand(outrel.tuples, chunkptrs, fanIn, fifobuffer, bufntuples);
    ck_assert_int_eq (num, ntuples);
    ck_assert_int_eq (is_sorted_tuples(outrel.tuples, ntuples), 1);
    for(int i = 0; i < fanIn; i++) {
        chunks[i] = chunks_cpy[i];
        chunkptrs[i] = &chunks[i];
    }
    */

    /* clean-up */
    free(outrel.tuples);
    for(int i = 0; i < fanIn; i++){
        free(chunks_cpy[i].tuples);
    }
    free(fifobuffer);
}
END_TEST

/** Test mergekernels (i.e. mergeX() functions), `ntests` random times with given seed */
START_TEST (test_mergekernels)
{
    uint32_t ntests = NUM_MERGEKERNEL_TESTS;
    int kernelwidth [] = {4, 8, 16, 44, 88, 1616};

    for(int t = 0; t < 6; t++){

        int WIDTH = kernelwidth[t];
        int size = ((rand() % 1000) + 100);
        /* fprintf(stderr, "[INFO ] merge%d-size = %d\n", WIDTH, size); */
        /* lists must have size in multiples of WIDTH */
        if(WIDTH == 44){
            size *= 4;
        }
        else if(WIDTH == 88){
            size *= 8;
        }
        else if(WIDTH == 1616){
            size *= 16;
        }
        else
            size *= WIDTH;

        int64_t input1[size] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t input2[size] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t output[size*2] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t validate[size*2] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t * A, * B;

        int64_t startA, startB;
        uint32_t i, j;
        uint32_t INCRMOD = 16384;

        int32_t maxint = ~(1 << 31) - INCRMOD;

        startA = rand() % INCRMOD;
        startB = rand() % INCRMOD;

        A = input1;
        B = input2;

        for(i = 0; i < ntests; i++) {
            /* generate random increasing order data */
            for(j = 0; j < size; j++) {
                A[j] = startA;
                B[j] = startB;

                if(startA < maxint)
                    startA += rand() % INCRMOD;

                if(startB < maxint)
                    startB += rand() % INCRMOD;
            }

            /* printf("MAX-A = %d, MAX-B = %d \n", A[j-1], B[j-1]); */
            memcpy(validate, input1, size * sizeof(int64_t));
            memcpy(validate + size, input2, size * sizeof(int64_t));
            qsort (validate, size*2, sizeof (int64_t), keycmp);

            /* test merge operations */
            if(WIDTH == 4)
                merge4_eqlen(input1, input2, output, size);
            else if(WIDTH == 8)
                merge8_eqlen(input1, input2, output, size);
            else if(WIDTH == 16)
                merge16_eqlen(input1, input2, output, size);
            /* eqlen_aligned versions : */
            else if(WIDTH == 44){
                merge4_eqlen_aligned(input1, input2, output, size);
            }
            else if(WIDTH == 88){
                merge8_eqlen_aligned(input1, input2, output, size);
            }
            else if(WIDTH == 1616){
                merge16_eqlen_aligned(input1, input2, output, size);
            }
            else {
                fprintf(stderr, "[WARN ] Only merge4(), merge8() and merge16() (+ _eqlen variants)\n");
            }

            if(!is_array_equal(output, validate, 2*size, 2*size)){
                ck_assert_msg (1, "[ERROR] Error in merge-kernel-%d (double digits are _eqlen() variants)\n", WIDTH);
            }
        }
    }
}
END_TEST

/** Test mergekernels with varying lenght input (i.e. mergeX_varlen() functions),
 * `ntests` random times with given seed
 * */
START_TEST (test_mergekernels_varlen)
{
    uint32_t ntests = NUM_MERGEKERNEL_TESTS;
    int kernelwidth [] = {4, 8, 16, 44, 88, 1616};

    for(int t = 0; t < 6; t++){

        int WIDTH = kernelwidth[t];
        unsigned int size1 = ((rand() % 1000) + 100);
        unsigned int size2 = ((rand() % 1000) + 100);
        /* lists must have size in multiples of WIDTH */
        if(WIDTH == 44){
            size1 *= 4;
            size2 *= 4;
        }
        else if(WIDTH == 88){
            size1 *= 8;
            size2 *= 8;
        }
        else if(WIDTH == 1616){
            size1 *= 16;
            size2 *= 16;
        }
        else{
            size1 *= WIDTH;
            size2 *= WIDTH;
        }

        size1 = (size1 & ~7) + (rand() % 16);
        size2 = (size2 & ~7) + (rand() % 16);

        int64_t input1[size1] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t input2[size2] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t output[size1+size2] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t validate[size1+size2] __attribute__((aligned(CACHE_LINE_SIZE)));
        int64_t * A, * B;

        int64_t startA, startB;
        uint32_t i, j;
        uint32_t INCRMOD = 16384;

        int32_t maxint = ~(1 << 31) - INCRMOD;

        A = input1;
        B = input2;

        for(i = 0; i < ntests; i++) {
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

            /* printf("MAX-A = %d, MAX-B = %d \n", A[j-1], B[j-1]); */
            memcpy(validate, input1, size1 * sizeof(int64_t));
            memcpy(validate+size1, input2, size2 * sizeof(int64_t));

            qsort (validate, size1+size2, sizeof (int64_t), keycmp);

            /*
            fprintf(stderr, "[INFO ] Trying merge%d(A=%d [m16=%d,m8=%d,m4=%d],"\
                    " B=%d [m16=%d,m8=%d,m4=%d]) ", WIDTH,
                    size1, (size1%16), (size1%8), (size1%4),
                    size2, (size2%16), (size2%8), (size2%4));
            */

            /* test merge operations */
            if(WIDTH == 4)
                merge4_varlen(input1, input2, output, size1, size2);
            else if(WIDTH == 8)
                merge8_varlen(input1, input2, output, size1, size2);
            else if(WIDTH == 16)
                merge16_varlen(input1, input2, output, size1, size2);
            /* varlen_aligned versions : */
            else if(WIDTH == 44)
                merge4_varlen_aligned(input1, input2, output, size1, size2);
            else if(WIDTH == 88)
                merge8_varlen_aligned(input1, input2, output, size1, size2);
            else if(WIDTH == 1616)
                merge16_varlen_aligned(input1, input2, output, size1, size2);
            else {
                fprintf(stderr, "[WARN ] Only merge4(), merge8() and merge16()\n");
            }

            if(!is_array_equal(output, validate, (size1+size2), (size1+size2))){
                ck_assert_msg (1, "[ERROR] Error in merge-kernel-%d (_varlen)\n", WIDTH);
            }
            else{
                int size = (size1+size2)-((size1+size2)/2)*2;
                if(size > 0) {
                    if(output[size1+size2-1] <= output[size1+size2-2]) {
                        ck_assert_msg (1, "[ERROR] Error in merge-kernel-%d (_varlen)\n", WIDTH);
                    }
                }
            }
        }
    }
}
END_TEST

/** checks the 4-sortedness of output */
int
checksort16(int64_t* inp)
{

    for(int i = 0; i < 4; i++){
        int64_t * in = inp + i * 4;
        if(!(in[0] <= in[1] && in[1] <= in[2] && in[2] <= in[3]))
        {
            fprintf(stderr,
                    "[ERROR] Output not sorted: %"PRId64" -> %"PRId64" -> %"PRId64" -> %"PRId64"\n",
                    in[0], in[1], in[2], in[3]);
            return 0;
        }
    }

    return 1;
}

/** Test inregister sorting of 4-elements */
START_TEST (test_inregistersort)
{
    int64_t * items;
    int64_t * output;
    int N = 64;

    items = (int64_t*) malloc(sizeof(int64_t) * N);
    output = (int64_t*) malloc(sizeof(int64_t) * N);

    for(uint32_t i = 0; i < N; i++) {
        int32_t * key = (int32_t *) &items[i];
        int32_t * val = key + 1;
        *key  = rand() % 1024;
        *val  = *key + 1;
        output[i] = 0;
    }

    for(uint32_t i = 0; i < N; i += 16) {
        int64_t * inp = &items[i];
        int64_t * out = &output[i];

        inregister_sort_keyval32(inp, out);

        if(!checksort16(out)){
            fprintf(stderr, "[ERROR] After sorting: \n");

            for(uint32_t j = 0; j < 4; j++) {
                for(uint32_t k = 0; k < 4; k++) {
                    int32_t * key = (int32_t *) &out[j*4+k];
                    int32_t * val = key + 1;
                    fprintf(stderr, "(%d,%d=>%"PRId64") ", *key, *val, out[j*4+k]);
                }
                fprintf(stderr, "\n");
            }
            ck_assert_msg (1, "[ERROR] Error in inregister-sort-kernel\n");
        }
    }

    free(items);
    free(output);
}
END_TEST

Suite *
merge_suite (void)
{
    Suite *s = suite_create ("mergetests");

    seed = time(NULL);//1398710365;//1398714073;//
    fprintf(stderr, "[INFO ] mergetests seed value is %d\n", seed);
    srand(seed);

    /* Core test case */
    TCase *tc = tcase_create ("correctness");
    tcase_add_test (tc, test_mergekernels);
    tcase_add_test (tc, test_mergekernels_varlen);
    tcase_add_test (tc, test_inregistersort);
    tcase_add_test (tc, test_merge);
    tcase_add_test (tc, test_multiwaymerge);
    tcase_set_timeout(tc, 0);

    suite_add_tcase (s, tc);

    return s;
}

int
main (void)
{
    int number_failed;
    Suite *s = merge_suite ();
    SRunner *sr = srunner_create (s);
    //srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

