/**
 * @file    sortmergejoin_multipass.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:37:54 2012
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

#ifndef SORTMERGEJOIN_MULTIPASS_H_
#define SORTMERGEJOIN_MULTIPASS_H_

#include "types.h"              /* relation_t, tuple_t, result_t */

/**
 * "m-pass sort-merge join"
 *
 * A Sort-Merge Join variant with partitioning and complete
 * sorting of both input relations. The merging step in this algorithm tries to
 * overlap the first merging and transfer of remote chunks. However, in compared
 * to the other variant (m-way), merge phase still takes a significant amount
 * of time as it is done through a multi-pass merging scheme of sorted-runs.
 *
 * @param relR input relation R
 * @param relS input relation S
 * @param nthreads number of threads to use for the join
 *
 * @warning this algorithm must be run with number of threads that is power of 2
 */
result_t *
sortmergejoin_multipass(relation_t * relR, relation_t * relS, int nthreads);

#endif /* SORTMERGEJOIN_MULTIPASS_H_ */
