AUTOMAKE_OPTIONS = foreign

INCLUDE  = -I. -I$(top_srcdir)/src/util/ -I$(top_srcdir)/src/datagen/ 
INCLUDE += -I$(top_srcdir)/src/merge/ -I$(top_srcdir)/src/avxsort/ -I$(top_srcdir)/src/scalarsort/ 
INCLUDE += -I$(top_srcdir)/src/partition/ -I$(top_srcdir)/src/joins/

DEFINES = -D_GNU_SOURCE
MYLDFLAGS =
MISC = 

if KEY_8B
DEFINES += -DKEY_8B
endif

if PERF_COUNTERS
DEFINES += -DPERF_COUNTERS
MYLDFLAGS += -lperf
MISC += -I$(top_srcdir)/lib/intel-pcm-2.35/ -L$(top_srcdir)/lib/intel-pcm-2.35/lib/
LIBS += -lperf
endif

if DEBUG
DEFINES += -DDEBUG
endif

if NOTIMING
DEFINES += -DNO_TIMING
endif

if JOIN_MATERIALIZE
DEFINES += -DJOIN_MATERIALIZE
endif

if PERSIST_RELATIONS
DEFINES += -DPERSIST_RELATIONS
endif

# for compiling with Intel Compiler (icc)
#ICC_FLAGS =
#-opt_report

AM_CFLAGS = -Wall -std=c99 -Wno-format -fno-strict-aliasing -mavx $(DEFINES) #-ansi 
AM_LDFLAGS = #-pthread
CFLAGS = -O3 -g

bin_PROGRAMS = sortmergejoins bench_partitioning bench_merge bench_multiwaymerge bench_sort tputbench


### 
# Main program for the sort-merge join algorithms
###
sortmergejoins_LDFLAGS = $(AM_LDFLAGS) $(MYLDFLAGS) -lpthread #-pthread 
sortmergejoins_CFLAGS  = $(AM_CFLAGS) $(MISC) $(INCLUDE)
sortmergejoins_SOURCES = \
	$(top_srcdir)/config.h \
	$(top_srcdir)/src/types.h \
	$(top_srcdir)/src/util/affinity.h \
	$(top_srcdir)/src/util/cpu_mapping.h \
	$(top_srcdir)/src/util/cpu_mapping.c \
	$(top_srcdir)/src/util/barrier.h \
	$(top_srcdir)/src/util/rdtsc.h \
	$(top_srcdir)/src/avxsort/avxcommon.h \
	$(top_srcdir)/src/avxsort/avxsort_core.h \
	$(top_srcdir)/src/avxsort/avxsort.h \
	$(top_srcdir)/src/avxsort/avxsort.c \
	$(top_srcdir)/src/merge/merge.h \
	$(top_srcdir)/src/merge/merge.c \
	$(top_srcdir)/src/scalarsort/scalarsort.h \
	$(top_srcdir)/src/scalarsort/scalarsort.c \
	$(top_srcdir)/src/datagen/genzipf.h \
	$(top_srcdir)/src/datagen/genzipf.c \
	$(top_srcdir)/src/datagen/generator.h \
	$(top_srcdir)/src/datagen/generator.c \
	$(top_srcdir)/src/partition/partition.h \
	$(top_srcdir)/src/partition/partition.c \
	$(top_srcdir)/src/joins/joincommon.h \
	$(top_srcdir)/src/joins/joincommon.c \
	$(top_srcdir)/src/joins/sortmergejoin_multipass.h \
	$(top_srcdir)/src/joins/sortmergejoin_multipass.c \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.h \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.c \
	$(top_srcdir)/src/merge/avx_multiwaymerge.h \
	$(top_srcdir)/src/merge/avx_multiwaymerge.c \
	$(top_srcdir)/src/joins/sortmergejoin_multiway.h \
	$(top_srcdir)/src/joins/sortmergejoin_multiway.c \
	$(top_srcdir)/src/joins/sortmergejoin_mpsm.h \
	$(top_srcdir)/src/joins/sortmergejoin_mpsm.c \
	$(top_srcdir)/src/main.c

###
# Partitioning microbenchmark
###
bench_partitioning_LDFLAGS = 
bench_partitioning_CFLAGS  = $(AM_CFLAGS) -I. -I$(top_srcdir)/src/partition/ -I$(top_srcdir)/src/util/ -I$(top_srcdir)/src/datagen/
bench_partitioning_SOURCES = $(top_srcdir)/src/types.h \
							 $(top_srcdir)/src/util/rdtsc.h \
							 $(top_srcdir)/src/util/cpu_mapping.h \
							 $(top_srcdir)/src/util/cpu_mapping.c \
							 $(top_srcdir)/src/util/affinity.h \
							 $(top_srcdir)/src/util/lock.h \
							 $(top_srcdir)/src/util/barrier.h \
							 $(top_srcdir)/src/datagen/genzipf.h \
							 $(top_srcdir)/src/datagen/genzipf.c \
							 $(top_srcdir)/src/datagen/generator.h \
							 $(top_srcdir)/src/datagen/generator.c \
							 $(top_srcdir)/src/partition/partition.h \
							 $(top_srcdir)/src/partition/partition.c \
							 $(top_srcdir)/src/bench/partitioningbench.c

###
# Merge microbenchmarks
###
bench_merge_LDFLAGS = 
bench_merge_CFLAGS  = $(AM_CFLAGS) -I. -I$(top_srcdir)/src/util/ -I$(top_srcdir)/src/avxsort/ -I$(top_srcdir)/tests/
bench_merge_SOURCES = $(top_srcdir)/src/util/affinity.h \
					  $(top_srcdir)/src/avxsort/avxsort_core.h \
					  $(top_srcdir)/tests/testutil.h \
					  $(top_srcdir)/tests/testutil.c \
					  $(top_srcdir)/src/bench/mergebench.c

###
# Multi-way merge microbenchmarks
###
bench_multiwaymerge_LDFLAGS = 
bench_multiwaymerge_CFLAGS  = $(AM_CFLAGS) -I. -I$(top_srcdir)/src/util/ -I$(top_srcdir)/src/avxsort/ -I$(top_srcdir)/src/merge/ -I$(top_srcdir)/tests/
bench_multiwaymerge_SOURCES = $(top_srcdir)/src/util/affinity.h \
							  $(top_srcdir)/src/merge/avx_multiwaymerge.h \
							  $(top_srcdir)/src/merge/avx_multiwaymerge.c \
							  $(top_srcdir)/src/merge/scalar_multiwaymerge.h \
							  $(top_srcdir)/src/merge/scalar_multiwaymerge.c \
							  $(top_srcdir)/tests/testutil.h \
							  $(top_srcdir)/tests/testutil.c \
							  $(top_srcdir)/src/bench/multiwaymergebench.c
 
###
# Sorting microbenchmarks
### 
bench_sort_LDFLAGS = 
bench_sort_CFLAGS  = $(AM_CFLAGS) $(INCLUDE) -I$(top_srcdir)/tests/
bench_sort_SOURCES = $(top_srcdir)/src/util/affinity.h \
				     $(top_srcdir)/src/util/cpu_mapping.h \
				     $(top_srcdir)/src/util/cpu_mapping.c \
				     $(top_srcdir)/tests/testutil.h \
				     $(top_srcdir)/tests/testutil.c \
				     $(top_srcdir)/src/avxsort/avxcommon.h \
				     $(top_srcdir)/src/avxsort/avxsort_core.h \
				     $(top_srcdir)/src/avxsort/avxsort.h \
				     $(top_srcdir)/src/avxsort/avxsort.c \
					 $(top_srcdir)/src/merge/merge.h \
                	 $(top_srcdir)/src/merge/merge.c \
                	 $(top_srcdir)/src/merge/avx_multiwaymerge.h \
                	 $(top_srcdir)/src/merge/avx_multiwaymerge.c \
                	 $(top_srcdir)/src/avxsort/avxsort_multiway.h \
                	 $(top_srcdir)/src/avxsort/avxsort_multiway.c \
                	 $(top_srcdir)/src/datagen/genzipf.h \
                	 $(top_srcdir)/src/datagen/genzipf.c \
                	 $(top_srcdir)/src/datagen/generator.h \
                	 $(top_srcdir)/src/datagen/generator.c \
                	 $(top_srcdir)/src/bench/sortbench.c


### 
# Throughput benchmarks for partitioning/merging and NUMA memcpy/read using m-way join
###
tputbench_LDFLAGS = $(AM_LDFLAGS) $(MYLDFLAGS) -lpthread #-pthread 
tputbench_CFLAGS  = $(AM_CFLAGS) $(MISC) $(INCLUDE)
tputbench_SOURCES = \
	$(top_srcdir)/config.h \
	$(top_srcdir)/src/types.h \
	$(top_srcdir)/src/util/affinity.h \
	$(top_srcdir)/src/util/cpu_mapping.h \
	$(top_srcdir)/src/util/cpu_mapping.c \
	$(top_srcdir)/src/util/barrier.h \
	$(top_srcdir)/src/util/rdtsc.h \
	$(top_srcdir)/src/avxsort/avxcommon.h \
	$(top_srcdir)/src/avxsort/avxsort_core.h \
	$(top_srcdir)/src/avxsort/avxsort.h \
	$(top_srcdir)/src/avxsort/avxsort.c \
	$(top_srcdir)/src/merge/merge.h \
	$(top_srcdir)/src/merge/merge.c \
	$(top_srcdir)/src/scalarsort/scalarsort.h \
	$(top_srcdir)/src/scalarsort/scalarsort.c \
	$(top_srcdir)/src/datagen/genzipf.h \
	$(top_srcdir)/src/datagen/genzipf.c \
	$(top_srcdir)/src/datagen/generator.h \
	$(top_srcdir)/src/datagen/generator.c \
	$(top_srcdir)/src/partition/partition.h \
	$(top_srcdir)/src/partition/partition.c \
	$(top_srcdir)/src/joins/joincommon.h \
	$(top_srcdir)/src/joins/joincommon.c \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.h \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.c \
	$(top_srcdir)/src/merge/avx_multiwaymerge.h \
	$(top_srcdir)/src/merge/avx_multiwaymerge.c \
	$(top_srcdir)/src/bench/tputbench.c
