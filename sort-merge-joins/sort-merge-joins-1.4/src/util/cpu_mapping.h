/**
 * @file   cpu_mapping.h
 * @author Cagri Balkesen <cagri.balkesen@inf.ethz.ch>
 * @date   Tue May 22 16:35:12 2012
 * 
 * @brief  Provides NUMA-aware CPU mapping utility functions.
 * 
 * (c) 2014, ETH Zurich, Systems Group
 * 
 */
#ifndef CPU_MAPPING_H
#define CPU_MAPPING_H

/** 
 * @defgroup cpumapping CPU mapping tool
 * @{
 */

/** 
 * if the custom cpu mapping file exists, logical to physical mappings are
 * initialized from that file, next it will first try libNUMA if available,
 * and finally round-robin as last option.
 */
#ifndef CUSTOM_CPU_MAPPING
#define CUSTOM_CPU_MAPPING "cpu-mapping.txt"
#endif

/** 
 * Initialize cpu mappings and NUMA topology (if libNUMA available).
 * Try custom cpu mapping file first, if does not exist then round-robin
 * initialization among available CPUs reported by the system. 
 */
void
cpu_mapping_init();

/**
 * De-initialize/free cpu mapping data structures.
 */
void
cpu_mapping_cleanup();


/**
 * Returns SMT aware logical to physical CPU mapping for a given logical thread id.
 */
int get_cpu_id(int thread_id);


/** @} */

/**
 * @defgroup numa NUMA related utility methods.
 * @{
 */

/** 
 * Returns whether given logical thread id is the first thread in its NUMA region.
 *
 * @param logicaltid logical thread id
 * 
 * @return true or false
 */
int
is_first_thread_in_numa_region(int logicaltid);

/** 
 * Returns number of physical threads per NUMA region. Only counts physical
 * threads not SMT threads.
 * 
 * @return count
 */
int 
get_nphythreads_per_numa(void);

/** 
 * Returns the physical sibling thread id if this thread is an SMT
 * (i.e. hyper-thread). If this thread is already a physical thread, returns its
 * thread id.
 * 
 * @param mytid requesting threads id returned from get_cpu_id(int)
 * 
 * @return 
 */
int 
get_physiblingthread_id(int mytid, int * numaidx);

/** 
 * Returns the index of the given logical thread within its NUMA-region.
 * 
 * @param logicaltid logical thread id
 * 
 * @return index of the thread within its NUMA-region
 */
int 
get_thread_index_in_numa(int logicaltid);

/** 
 * Returns the NUMA-region id of the given logical thread id.
 * 
 * @param logicaltid the logical thread id
 * 
 * @return NUMA-region id
 */
int
get_numa_region_id(int logicaltid);

/** 
 * Returns number of NUMA regions.
 * 
 * @return 
 */
int
get_num_numa_regions(void);

/* Returns the cpu offset in NUMA-order; i.e. first NUMA-0 then NUMA-1
   and so on. Used for making NUMA-regions in input relations contigious. */
int
get_cpu_in_numa_order(int logical_cpu);

/**
 * Set the given thread by physical thread-id (i.e. returned from get_cpu_id())
 * as active in its NUMA-region.
 *
 * @param phytid physical thread id returned by get_cpu_id()
 */
void
numa_thread_mark_active(int phytid);

/**
 * Return the active number of threads in the given NUMA-region.
 *
 * @param numaregionid id of the NUMA-region
 *
 * @return active (i.e. running) number of threads in the NUMA-region.
 */
int
get_num_active_threads_in_numa(int numaregionid);

/** @} */

#endif /* CPU_MAPPING_H */

