/**
 * @file    sortmergejoin_multiway.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:38:54 2012
 * @version $Id $
 *
 * @brief   m-way sort-merge-join algorithm with multi-way merging.
 *          It uses AVX-based sorting and merging if scalarsort and scalarmerge
 *          flags are not provided.
 *
 *
 * (c) 2012-2014, ETH Zurich, Systems Group
 *
 */

#ifndef SORTMERGEJOIN_MULTIWAY_H_
#define SORTMERGEJOIN_MULTIWAY_H_

#include "types.h"              /* relation_t, tuple_t, result_t */

/**
 * "m-may sort-merge join"
 *
 * A Sort-Merge Join variant with partitioning and complete
 * sorting of both input relations. The merging step in this algorithm overlaps
 * the entire merging and transfer of remote chunks using a multi-way merge tree.
 *
 * @param relR input relation R
 * @param relS input relation S
 * @param nthreads number of threads to use for the join
 *
 * @warning this algorithm must be run with number of threads that is power of 2
 */
result_t *
sortmergejoin_multiway(relation_t * relR, relation_t * relS, int nthreads);


#endif /* SORTMERGEJOIN_MULTIWAY_H_ */
