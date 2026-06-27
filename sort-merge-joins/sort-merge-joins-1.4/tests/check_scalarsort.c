#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <check.h>

#include "testutil.h"
#include "scalarsort.h"

#define MAXTESTSIZE (1<<20)

START_TEST (test_scalarsort_int32)
{
    int64_t sz = rand() % MAXTESTSIZE;
    int32_t * in = generate_rand_int32(sz);
    int32_t * out = 0;
    scalarsort_int32(&in, &out, sz);
    ck_assert_int_eq (is_sorted_int32(out, sz), 1);
    free(in);
}
END_TEST

START_TEST (test_scalarsort_int64)
{
    int64_t sz = rand() % MAXTESTSIZE;
    int64_t * in = generate_rand_int64(sz);
    int64_t * out = 0;
    scalarsort_int64(&in, &out, sz);
    ck_assert_int_eq (is_sorted_int64(out, sz), 1);
    free(in);
}
END_TEST

START_TEST (test_scalarsort_tuples)
{
    int64_t sz = rand() % MAXTESTSIZE;
    tuple_t * in = generate_rand_tuples(sz);
    tuple_t * out = 0;
    scalarsort_tuples(&in, &out, sz);
    ck_assert_int_eq (is_sorted_tuples(out, sz), 1);
    free(in);
}
END_TEST

START_TEST (test_scalarsort_int32_ordered)
{
    int64_t sz = rand() % MAXTESTSIZE;
    int32_t * in = generate_rand_ordered_int32(sz);
    int32_t * out = 0;
    scalarsort_int32(&in, &out, sz);
    ck_assert_int_eq (is_sorted_int32(out, sz), 1);
    free(in);
}
END_TEST

START_TEST (test_scalarsort_int64_ordered)
{
    int64_t sz = rand() % MAXTESTSIZE;
    int64_t * in = generate_rand_ordered_int64(sz);
    int64_t * out = 0;
    scalarsort_int64(&in, &out, sz);
    ck_assert_int_eq (is_sorted_int64(out, sz), 1);
    free(in);
}
END_TEST

START_TEST (test_scalarsort_tuples_ordered)
{
    int64_t sz = rand() % MAXTESTSIZE;
    tuple_t * in = generate_rand_ordered_tuples(sz);
    tuple_t * out = 0;
    scalarsort_tuples(&in, &out, sz);
    ck_assert_int_eq (is_sorted_tuples(out, sz), 1);
    free(in);
}
END_TEST

Suite *
scalarsort_suite (void)
{
    Suite *s = suite_create ("scalarsort");
    int seed = time(NULL);
    fprintf(stderr, "[INFO ] scalarsort_suite seed value is %d\n", seed);
    srand(seed);

    /* Core test case */
    TCase *tc_int32 = tcase_create ("int32");
    tcase_add_test (tc_int32, test_scalarsort_int32);
    tcase_add_test (tc_int32, test_scalarsort_int32_ordered);
    tcase_set_timeout(tc_int32, 0);

    TCase *tc_int64 = tcase_create ("int64");
    tcase_add_test (tc_int64, test_scalarsort_int64);
    tcase_add_test (tc_int64, test_scalarsort_int64_ordered);
    tcase_set_timeout(tc_int64, 0);

    TCase *tc_tuples = tcase_create ("tuples");
    tcase_add_test (tc_tuples, test_scalarsort_tuples);
    tcase_add_test (tc_tuples, test_scalarsort_tuples_ordered);
    tcase_set_timeout(tc_tuples, 0);

    suite_add_tcase (s, tc_int32);
    suite_add_tcase (s, tc_int64);
    suite_add_tcase (s, tc_tuples);

    return s;
}

int
main (void)
{
    int number_failed;
    Suite *s = scalarsort_suite ();
    SRunner *sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

