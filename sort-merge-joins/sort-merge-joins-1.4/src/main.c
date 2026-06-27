#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>           /* gettimeofday */
#include <stdlib.h>
#include <limits.h>             /* LONG_MAX */
#include <math.h>
#include <string.h>             /* strlen(), memcpy() */
#include <getopt.h>             /* getopt */
/* #include <assert.h> */

#include "affinity.h"           /* CPU_SET, setaffinity(), pthread_attr_setaffinity_np */
#include "cpu_mapping.h"        /* cpu_mapping_cleanup() */
#include "types.h"
#include "generator.h"


/**************** include join algorithm thread implementations ***************/
#include "joins/sortmergejoin_multipass.h"
#include "joins/sortmergejoin_multiway.h"
#include "joins/sortmergejoin_mpsm.h"

#ifdef JOIN_MATERIALIZE
#include "tuple_buffer.h"       /* for materialization */
#endif

#ifdef PERF_COUNTERS
#include "perf_counters.h"      /* PCM_x */
#endif

#include "config.h"          /* autoconf header */

/** Debug msg logging method */
#ifdef DEBUG
#define DEBUGMSG(COND, MSG, ...)                                        \
    if(COND) {                                                          \
        fprintf(stderr,                                                 \
                "[DEBUG @ %s:%d\] "MSG, __FILE__, __LINE__, ## __VA_ARGS__); \
    }
#else
#define DEBUGMSG(COND, MSG, ...)
#endif


/** Global command line arguments, whether to execute scalar code */
extern int scalarsortflag; /* defined in joincommon.c */
extern int scalarmergeflag;

/** Print out timing stats for the given start and end timestamps */
extern void
print_timing(uint64_t numtuples, struct timeval * start, struct timeval * end,
             FILE * out);

/******************************************************************************
 *                                                                            *
 *                 Command-line handling & Driver Program                     *
 *                                                                            *
 ******************************************************************************/

#if !defined(__cplusplus)
int getopt(int argc, char * const argv[],
           const char *optstring);
#endif

typedef struct algo_t  algo_t;
typedef struct cmdparam_t cmdparam_t;

struct algo_t {
    char name[128];
    result_t * (*joinalgorithm)(relation_t *, relation_t *, int);
};

struct cmdparam_t {
    algo_t * algo;
    uint64_t r_size;
    uint64_t s_size;
    uint32_t nthreads;
    uint32_t r_seed;
    uint32_t s_seed;
    double skew;
    int nonunique_keys;  /* non-unique keys allowed? */
    int verbose;
    int fullrange_keys;  /* keys covers full int range? */
    int basic_numa;/* alloc input chunks thread local? */
    char * perfconf;
    char * perfout;
    int scalar_sort;
    int scalar_merge;
};

extern char * optarg;
extern int    optind, opterr, optopt;

/** An experimental feature to allocate input relations numa-local */
extern int numalocalize;  /* defined in generator.c */
extern int nthreads;      /* defined in generator.c */

/** All available algorithms */
static struct algo_t algos [] =
  {
      {"m-pass", sortmergejoin_multipass},
      {"m-way", sortmergejoin_multiway},
      {"mpsm", sortmergejoin_mpsm},
      {{0}, 0}
  };

/* command line handling functions */
void
print_help();

void
print_version();

void
parse_args(int argc, char ** argv, cmdparam_t * cmd_params);

int
main(int argc, char *argv[])
{
    struct timeval start, end;
    relation_t relR;
    relation_t relS;

    /* start initially on CPU-0 */
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    if (sched_setaffinity(0, sizeof(set), &set) <0) {
        perror("sched_setaffinity");
    }

    /* Command line parameters */
    cmdparam_t cmd_params;

    /* Default values if not specified on command line */
    cmd_params.algo     = &algos[0]; /* m-pass: sort-merge join with multi-pass merge */
    cmd_params.nthreads = 2;
    /* default dataset is Workload B (described in paper) */
    cmd_params.r_size   = 128000000;
    cmd_params.s_size   = 128000000;
    cmd_params.r_seed   = 12345;
    cmd_params.s_seed   = 54321;
    cmd_params.skew     = 0.0;
    cmd_params.verbose  = 0;
    cmd_params.perfconf = NULL;
    cmd_params.perfout  = NULL;
    cmd_params.nonunique_keys   = 0;
    cmd_params.fullrange_keys   = 0;
    cmd_params.basic_numa   = 0;
    cmd_params.scalar_sort  = 0;
    cmd_params.scalar_merge = 0;

    parse_args(argc, argv, &cmd_params);

    scalarsortflag  = cmd_params.scalar_sort;
    scalarmergeflag = cmd_params.scalar_merge;

#ifdef PERF_COUNTERS
    PCM_CONFIG = cmd_params.perfconf;
    PCM_OUT    = cmd_params.perfout;
#endif

    /***** create relation R *****/
    fprintf(stdout,
            "[INFO ] Creating relation R with size = %.3lf MiB, #tuples = %lld : ",
            (double) sizeof(tuple_t) * cmd_params.r_size/(1024.0*1024.0),
            cmd_params.r_size);
    fflush(stdout);

    seed_generator(cmd_params.r_seed);

    /* to pass information to the create_relation methods */
    numalocalize = cmd_params.basic_numa;
    nthreads     = cmd_params.nthreads;

    gettimeofday(&start, NULL);
    if(cmd_params.fullrange_keys) {
        create_relation_nonunique(&relR, cmd_params.r_size, INT_MAX);
    }
    else if(cmd_params.nonunique_keys) {
        create_relation_nonunique(&relR, cmd_params.r_size, cmd_params.r_size);
    }
    else {
        parallel_create_relation(&relR, cmd_params.r_size,
                                 cmd_params.nthreads, cmd_params.r_size);
        /* create_relation_pk(&relR, cmd_params.r_size); */
    }
    gettimeofday(&end, NULL);
    fprintf(stdout, "OK \n");
    fprintf(stdout, "[INFO ] Time: ");
    print_timing(cmd_params.r_size, &start, &end, stdout);
    fprintf(stdout, "\n");

    /***** create relation S *****/
    fprintf(stdout,
            "[INFO ] Creating relation S with size = %.3lf MiB, #tuples = %lld : ",
            (double) sizeof(tuple_t) * cmd_params.s_size/1024.0/1024.0,
            cmd_params.s_size);
    fflush(stdout);

    seed_generator(cmd_params.s_seed);

    gettimeofday(&start, NULL);
    if(cmd_params.fullrange_keys) {
        create_relation_fk_from_pk(&relS, &relR, cmd_params.s_size);
    }
    else if(cmd_params.nonunique_keys) {
        /* use size of R as the maxid */
        create_relation_nonunique(&relS, cmd_params.s_size, cmd_params.r_size);
    }
    else {
        /* if r_size == s_size then equal-dataset, else non-equal dataset */

        if(cmd_params.skew > 0){
            /* S is skewed */
            create_relation_zipf(&relS, cmd_params.s_size,
                                 cmd_params.r_size, cmd_params.skew);
        }
        else {
            /* S is uniform foreign key */
            parallel_create_relation(&relS, cmd_params.s_size,
                                     cmd_params.nthreads, cmd_params.r_size);
            /* create_relation_pk(&relS, cmd_params.s_size); */
        }
    }
    gettimeofday(&end, NULL);
    fprintf(stdout, "OK \n");
    fprintf(stdout, "[INFO ] Time: ");
    print_timing(cmd_params.s_size, &start, &end, stdout);
    fprintf(stdout, "\n");

    /* Run the selected join algorithm */
    fprintf(stdout, "[INFO ] Running join algorithm %s ...\n", cmd_params.algo->name);

    result_t * result = cmd_params.algo->joinalgorithm(&relR, &relS, cmd_params.nthreads);

    fprintf(stdout, "\n[INFO ] Results = %llu. DONE.\n", result->totalresults);

#if (defined(PERSIST_RELATIONS) && defined(JOIN_MATERIALIZE))
    printf("[INFO ] Persisting the output result to \"Out.tbl\" ...\n");
    write_result_relation(result, "Out.tbl");
#endif


    /* cleanup */
    delete_relation(&relR);
    delete_relation(&relS);

#ifdef JOIN_MATERIALIZE
    for(int i = 0; i < nthreads; i++){
        chainedtuplebuffer_free(result->resultlist[i].results);
    }
#endif
    free(result->resultlist);
    free(result);
    cpu_mapping_cleanup();

    return 0;
}

/* command line handling functions */
void
print_help(char * progname)
{
    printf("Usage: %s [options]\n", progname);

    printf("Join algorithm selection, algorithms : ");
    int i = 0;
    while(algos[i].joinalgorithm) {
        printf("%s ", algos[i].name);
        i++;
    }
    printf("\n");

    printf("\
       -a --algo=<name>    Run the join algorithm named <name> [m-pass]       \n\
                                                                              \n\
    Other join configuration options, with default values in [] :             \n\
       -n --nthreads=<N>  Number of threads to use <N> [2]                    \n\
       -r --r-size=<R>    Number of tuples in build relation R <R> [128000000]\n\
       -s --s-size=<S>    Number of tuples in probe relation S <S> [128000000]\n\
       -x --r-seed=<x>    Seed value for generating relation R <x> [12345]    \n\
       -y --s-seed=<y>    Seed value for generating relation S <y> [54321]    \n\
       -z --skew=<z>      Zipf skew parameter for probe relation S <z> [0.0]  \n\
       --non-unique       Use non-unique (duplicated) keys in input relations \n\
       --full-range       Spread keys in relns. in full 32-bit integer range  \n\
       --basic-numa       Numa-localize relations to threads (Experimental)   \n\
       --scalarsort       Use scalar sorting; sort() -> g++ or qsort() -> gcc \n\
       --scalarmerge      Use scalar merging algorithm instead of AVX-merge   \n\
                                                                              \n\
    Performance profiling options, when compiled with --enable-perfcounters.  \n\
       -p --perfconf=<P>  Intel PCM config file with upto 4 counters [none]   \n\
       -o --perfout=<O>   Output file to print performance counters [stdout]  \n\
                                                                              \n\
    Basic user options                                                        \n\
        -h --help         Show this message                                   \n\
        --verbose         Be more verbose -- show misc extra info             \n\
        --version         Show version                                        \n\
    \n");
}

void
print_version()
{
    printf("\n%s\n", PACKAGE_STRING);
    printf("Copyright (c) 2012, ETH Zurich, Systems Group.\n");
    printf("http://www.systems.ethz.ch/projects/paralleljoins\n\n");
}

static char *
mystrdup (const char *s)
{
    char *ss = (char*) malloc (strlen (s) + 1);

    if (ss != NULL)
        memcpy (ss, s, strlen(s) + 1);

    return ss;
}

void
parse_args(int argc, char ** argv, cmdparam_t * cmd_params)
{

    int c, i, found;
    /* Flag set by --verbose */
    static int verbose_flag;
    static int nonunique_flag;
    static int fullrange_flag;
    static int basic_numa;
    static int scalarsort;
    static int scalarmerge;


    while(1) {
        static struct option long_options[] =
            {
                /* These options set a flag. */
                {"verbose",    no_argument,    &verbose_flag,   1},
                {"brief",      no_argument,    &verbose_flag,   0},
                {"non-unique", no_argument,    &nonunique_flag, 1},
                {"full-range", no_argument,    &fullrange_flag, 1},
                {"basic-numa", no_argument,    &basic_numa, 1},
                {"scalarsort", no_argument,    &scalarsort, 1},
                {"scalarmerge",no_argument,    &scalarmerge,1},
                {"help",       no_argument,    0, 'h'},
                {"version",    no_argument,    0, 'v'},
                /* These options don't set a flag.
                   We distinguish them by their indices. */
                {"algo",    required_argument, 0, 'a'},
                {"nthreads",required_argument, 0, 'n'},
                {"perfconf",required_argument, 0, 'p'},
                {"r-size",  required_argument, 0, 'r'},
                {"s-size",  required_argument, 0, 's'},
                {"perfout", required_argument, 0, 'o'},
                {"r-seed",  required_argument, 0, 'x'},
                {"s-seed",  required_argument, 0, 'y'},
                {"skew",    required_argument, 0, 'z'},
                {0, 0, 0, 0}
            };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, "a:n:p:r:s:o:x:y:z:hv",
                         long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;
        switch (c)
        {
          case 0:
              /* If this option set a flag, do nothing else now. */
              if (long_options[option_index].flag != 0)
                  break;
              printf ("option %s", long_options[option_index].name);
              if (optarg)
                  printf (" with arg %s", optarg);
              printf ("\n");
              break;

          case 'a':
              i = 0; found = 0;
              while(algos[i].joinalgorithm) {
                  if(strcmp(optarg, algos[i].name) == 0) {
                      cmd_params->algo = &algos[i];
                      found = 1;
                      break;
                  }
                  i++;
              }

              if(found == 0) {
                  printf("[ERROR] Join algorithm named `%s' does not exist!\n",
                         optarg);
                  print_help(argv[0]);
                  exit(EXIT_SUCCESS);
              }
              break;

          case 'h':
          case '?':
              /* getopt_long already printed an error message. */
              print_help(argv[0]);
              exit(EXIT_SUCCESS);
              break;

          case 'v':
              print_version();
              exit(EXIT_SUCCESS);
              break;

          case 'n':
              cmd_params->nthreads = atoi(optarg);
              break;

          case 'p':
              cmd_params->perfconf = mystrdup(optarg);
              break;

          case 'r':
              cmd_params->r_size = atol(optarg);
              break;

          case 's':
              cmd_params->s_size = atol(optarg);
              break;

          case 'o':
              cmd_params->perfout = mystrdup(optarg);
              break;

          case 'x':
              cmd_params->r_seed = atoi(optarg);
              break;

          case 'y':
              cmd_params->s_seed = atoi(optarg);
              break;

          case 'z':
              cmd_params->skew = atof(optarg);
              break;

          default:
              break;
        }
    }

    /* if (verbose_flag) */
    /*     printf ("verbose flag is set \n"); */

    cmd_params->nonunique_keys = nonunique_flag;
    cmd_params->verbose        = verbose_flag;
    cmd_params->fullrange_keys = fullrange_flag;
    cmd_params->basic_numa     = basic_numa;
    cmd_params->scalar_sort    = scalarsort;
    cmd_params->scalar_merge   = scalarmerge;

    /* Print any remaining command line arguments (not options). */
    if (optind < argc) {
        printf ("non-option arguments: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        printf ("\n");
    }
}
