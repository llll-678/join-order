/**
 * @file   types.h
 * @author Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date   Tue May 22 16:43:30 2012
 *
 * @brief  Provides general type definitions used by all join algorithms.
 *
 * (c) 2014, ETH Zurich, Systems Group
 *
 */
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <inttypes.h>

/**
 * @defgroup Types Common Types
 * Common type definitions used by all join implementations.
 * @{
 */

#ifdef KEY_8B /* 64-bit key/payload, 16B tuples */
typedef int64_t intkey_t;
typedef int64_t value_t;
#else /* 32-bit key/payload, 8B tuples */
typedef int32_t intkey_t;
typedef int32_t value_t;
#endif

typedef struct tuple_t    tuple_t;
typedef struct relation_t relation_t;
typedef struct result_t result_t;
typedef struct threadresult_t threadresult_t;

/**
 * Type definition for a tuple, depending on KEY_8B a tuple can be 16B or 8B
 * @note this layout is chosen as a work-around for AVX double operations.
 */
struct tuple_t {
    value_t  payload;
    intkey_t key;
};


/**
 * Type definition for a relation.
 * It consists of an array of tuples and a size of the relation.
 */
struct relation_t {
  tuple_t * tuples;
  uint64_t  num_tuples;
};


/** Holds the join results of a thread */
struct threadresult_t {
    int64_t  nresults;
    void *   results;
    uint32_t threadid;
};

/** Type definition for join results. */
struct result_t {
    int64_t          totalresults;
    threadresult_t * resultlist;
    int              nthreads;
};

/** @} */

#endif /* TYPES_H */
