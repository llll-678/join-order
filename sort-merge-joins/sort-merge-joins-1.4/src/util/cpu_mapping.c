#include <stdio.h>  /* FILE, fopen */
#include <stdlib.h> /* exit, perror */
#include <unistd.h> /* sysconf */

#include "config.h" /* HAVE_LIBNUMA */

#ifdef HAVE_LIBNUMA
#include <numa.h>   /* for automatic NUMA-mappings */
#endif

#include "cpu_mapping.h"

/** \internal
 * @{
 */

#define MAX_NODES 1024

static int inited = 0;
static int max_cpus;
static int cpumapping[MAX_NODES];

/*** NUMA-Topology related variables ***/
static int numthreads;
static int numnodes;
static int thrpernuma;
static int ** numa;
static char ** numaactive;
static int * numaactivecount;

/** 
 * Initializes the cpu mapping from the file defined by CUSTOM_CPU_MAPPING.
 * The mapping used for our machine Intel L5520 is = "8 0 1 2 3 8 9 10 11".
 */
static int
init_mappings_from_file()
{
    FILE * cfg;
	int i;

    cfg = fopen(CUSTOM_CPU_MAPPING, "r");
    if (cfg!=NULL) {
        if(fscanf(cfg, "%d", &max_cpus) <= 0) {
            perror("Could not parse input!\n");
        }

        for(i = 0; i < max_cpus; i++){
            if(fscanf(cfg, "%d", &cpumapping[i]) <= 0) {
                perror("Could not parse input!\n");
            }
        }

        fclose(cfg);
        return 1;
    }


    /* perror("Custom cpu mapping file not found!\n"); */
    return 0;
}

/** 
 * Initialize NUMA-topology with libnuma.
 */
static void
numa_default_init()
{
        numnodes   = 1;
        numthreads = max_cpus;
        thrpernuma = max_cpus;
        numa = (int **) malloc(sizeof(int *));
        numa[0] = (int *) malloc(sizeof(int) * numthreads);
        numaactive = (char **) malloc(sizeof(char *));
        numaactive[0] = (char *) calloc(numthreads, sizeof(char));
        numaactivecount = (int *) calloc(numnodes, sizeof(int));
        for(int i = 0; i < max_cpus; i++){
                numa[0][i] = cpumapping[i];
        }
}

static void 
numa_init()
{
#ifdef HAVE_LIBNUMA
	int i, k, ncpus, j;
	struct bitmask *cpus;
   	//printf("init_mappings()\n");

	if (numa_available() < 0)  {
		//printf("no numa\n");
		numa_default_init();
		return;
	}

	numnodes   = numa_num_configured_nodes();
	cpus       = numa_allocate_cpumask();
	ncpus      = cpus->size;
	thrpernuma = max_cpus / numnodes;
	numa = (int **) malloc(sizeof(int *) * numnodes);
	numaactive = (char **) malloc(sizeof(char *) * numnodes);
	for (i = 0; i < numnodes ; i++) {
		numa[i] = (int *) malloc(sizeof(int) * thrpernuma);
		numaactive[i] = (char *) calloc(thrpernuma, sizeof(char));
	}
	numaactivecount = (int *) calloc(numnodes, sizeof(int));

    //printf("\n");
	for (i = 0; i < numnodes ; i++) {
		if (numa_node_to_cpus(i, cpus) < 0) {
			printf("node %d failed to convert\n",i); 
		}		
		//printf("Node-%d: ", i);
		j = 0;
		for (k = 0; k < ncpus; k++){
			if (numa_bitmask_isbitset(cpus, k)){
				//printf(" %s%d", k>0?", ":"", k);
                numa[i][j] = k;
                j++;
            }
		}
		//printf("\n");
	}
    numthreads = thrpernuma * numnodes;

    numa_free_cpumask(cpus);

#else
    numa_default_init();
#endif
}

void
cpu_mapping_cleanup()
{
    for (int i = 0; i < numnodes ; i++) {
        free(numa[i]);
        free(numaactive[i]);
    }
	free(numa);
	free(numaactive);
	free(numaactivecount);
}


/** @} */

/** 
 *  Try custom cpu mapping file first, if does not exist then round-robin
 *  initialization among available CPUs reported by the system. 
 */
void
cpu_mapping_init()
{
    if( init_mappings_from_file() == 0 ) {
        int i;
        
        max_cpus  = sysconf(_SC_NPROCESSORS_ONLN);
        for(i = 0; i < max_cpus; i++){
            cpumapping[i] = i;
        }
    }

    numa_init();
    inited = 1;
}

void
numa_thread_mark_active(int phytid)
{
    int numaregionid = -1;
    for(int i = 0; i < numnodes; i++){
        for(int j = 0; j < thrpernuma; j++){
            if(numa[i][j] == phytid){
                numaregionid = i;
                break;
            }
        }
        if(numaregionid != -1)
            break;
    }

    int thridx = -1;
    for(int i = 0; i < numnodes; i++){
        for(int j = 0; j < thrpernuma; j++){
            if(numa[i][j] == phytid){
                thridx = j;
                break;
            }
        }
        if(thridx != -1)
            break;
    }

    if(numaactive[numaregionid][thridx] == 0){
        numaactive[numaregionid][thridx] = 1;
        numaactivecount[numaregionid] ++;
    }
}

/**
 * Returns SMT aware logical to physical CPU mapping for a given thread id.
 * Also marks the active threads in each NUMA-region.
 */
int 
get_cpu_id(int thread_id) 
{
    if(!inited){
        cpu_mapping_init();
        //inited = 1;
#if 0
        printf("\n------ Thread mappings -----\n");
        for(int t = 0; t < max_cpus; t++){
            printf("Thread-%d --> CPU[%d] \n", t, get_cpu_id(t));
        }

        printf("------ NUMA-Topology -----\n");
        for(int n = 0; n < numnodes; n++){
            printf("NUMA-%d : ", n);
            for(int t = 0; t < thrpernuma; t++){
                printf("numa[%d][%d]=%d, ", n, t, numa[n][t]);
            }
            printf("\n");
        }
#endif
    }

    return cpumapping[thread_id % max_cpus];
}

/* TODO: These are just place-holder implementations. */
/**
 * Topology of Intel E5-4640
 node 0 cpus: 0 4 8 12 16 20 24 28 32 36 40 44 48 52 56 60
 node 1 cpus: 1 5 9 13 17 21 25 29 33 37 41 45 49 53 57 61
 node 2 cpus: 2 6 10 14 18 22 26 30 34 38 42 46 50 54 58 62
 node 3 cpus: 3 7 11 15 19 23 27 31 35 39 43 47 51 55 59 63
*/
#define INTEL_E5 0

/*
static int numa[][16] = {
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60},
    {1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61},
    {2, 6, 10, 14, 18, 22, 26, 30, 34, 38, 42, 46, 50, 54, 58, 62},
    {3, 7, 11, 15, 19, 23, 27, 31, 35, 39, 43, 47, 51, 55, 59, 63} };
*/

int
is_first_thread_in_numa_region(int logicaltid)
{
#if INTEL_E5
    return (tid == numa[0][0] || tid == numa[1][0] ||
            tid == numa[2][0] || tid == numa[3][0]);

#else
    int phytid = get_cpu_id(logicaltid);
	int ret = 0;
	for(int i = 0; i < numnodes; i++)
		ret = ret || (phytid == numa[i][0]);

    	return ret;
#endif
}

int 
get_nphythreads_per_numa(void)
{
#if INTEL_E5
    return 8;
#else
    /* TODO: FIXME: We assume that SMT is always enabled! */
    return (thrpernuma / 2);
#endif
}

int 
get_physiblingthread_id(int mytid, int * numaidx)
{
#if INTEL_E5
    int ret;
    
    if(mytid < 32)
        ret = mytid;
    else
        ret = (mytid-32);

    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 16; j++)
            if(numa[i][j] == ret){
                *numaidx = j;
                break;
            }

    return ret;
#else
    /* TODO: FIXME: We assume that SMT is always enabled! */
    /* Moreover, we assume first half of the threads are physical and
       next half are always logical (i.e., SMT) threads */
    int numphys = thrpernuma/2;
    int ret = mytid;
    int j = 0;
    for(int i = 0; i < numnodes; i++) {
        for(; j < thrpernuma; j++) {
            if(numa[i][j] == ret){
                
                if(j > numphys){
                    ret = numa[i][j-numphys];
                }
                *numaidx = j % numphys;
                break;
            }
        }
    }

    return ret;
	
	
/*
    if(mytid < 4) {
        *numaidx = mytid;
        return mytid;
    }
    *numaidx = mytid-4;

    return (mytid-4);
*/
#endif

}

int 
get_thread_index_in_numa(int logicaltid)
{
#if INTEL_E5
    int ret;
    
    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 16; j++)
            if(numa[i][j] == mytid){
                ret = j;
                break;
            }

    return ret;
#else    
    int ret = -1;
    int phytid = get_cpu_id(logicaltid);
    for(int i = 0; i < numnodes; i++){
        for(int j = 0; j < thrpernuma; j++){
            if(numa[i][j] == phytid){
                ret = j;
                break;
            }
        }
        if(ret != -1)
            break;
    }


    return ret;
#endif
}

int
get_numa_region_id(int logicaltid)
{
#if INTEL_E5
    int ret = 0;

    for(int i = 0; i < 4; i++)
        for(int j = 0; j < 16; j++)
            if(numa[i][j] == mytid){
                ret = i;
                break;
            }
    
    return ret;
#else
    int ret = -1;
    int phytid = get_cpu_id(logicaltid);

    for(int i = 0; i < numnodes; i++){
        for(int j = 0; j < thrpernuma; j++){
            if(numa[i][j] == phytid){
                ret = i;
                break;
            }
        }
        if(ret != -1)
            break;
    }

    return ret;
#endif
}

int
get_num_numa_regions(void)
{
#if INTEL_E5
    return 4;
#else
    return numnodes;
#endif
}

/* Returns the cpu offset in NUMA-order; i.e. first NUMA-0 then NUMA-1
   and so on. Used for making NUMA-regions in input relations contigious. */
int
get_cpu_in_numa_order(int logical_cpu)
{
    int numaid = 0;
    int id_in_numa = 0;

    /* numaid = get_numa_id(logical_cpu); */
    /* id_in_numa = get_thridx_in_numa(logical_cpu); */

    for(int i = 0; i < numnodes; i++)
        for(int j = 0; j < thrpernuma; j++)
            if(numa[i][j] == logical_cpu){
                numaid = i;
                id_in_numa = j;
                break;
            }

    return (numaid * thrpernuma + id_in_numa);

    /*
    int numaid = logical_cpu / thrpernuma;
    int thr = logical_cpu % thrpernuma;
    return numa[numaid][thr];
*/
}

int
get_num_active_threads_in_numa(int numaregionid)
{
    return numaactivecount[numaregionid];
}
