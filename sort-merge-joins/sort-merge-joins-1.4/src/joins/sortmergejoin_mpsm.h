/**
 * @file    sortmergejoin_mpsm.h
 * @author  Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date    Sat Dec 15 15:10:38 2012
 * @version $Id $
 *
 * @brief   Implementation of the Massively Parallel Sort-Merge Join (MPSM) as
 *          described in VLDB'12 Paper by Albutiu et al.
 *
 *
 */
#ifndef SORTMERGEJOIN_MPSM_H
#define SORTMERGEJOIN_MPSM_H


#include "types.h"              /* relation_t, tuple_t, result_t */

/**
 * "mpsm sort-merge join"
 *
 * "Massively Parallel Sort-Merge joins in main-memory multi-core database
 * systems", Albutiu et al., PVLDB'12. MPSM is a Partial-Sort-Scan-Join.
 *
 * @param relR input relation R
 * @param relS input relation S
 * @param nthreads number of threads to use for the join
 *
 * @warning this algorithm must be run with number of threads that is power of 2
 */
result_t *
sortmergejoin_mpsm(relation_t * relR, relation_t * relS, int nthreads);

#endif /* SORTMERGEJOIN_MPSM_H */
