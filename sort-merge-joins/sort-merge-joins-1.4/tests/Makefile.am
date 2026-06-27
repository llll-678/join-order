TESTS = check_scalarsort check_avxsort check_partitioning check_merge
check_PROGRAMS = check_scalarsort check_avxsort check_partitioning check_merge
CFLAGS = -g -O3 #-DNDEBUG
AM_CFLAGS = -Wall -std=c99 -Wno-format -fno-strict-aliasing -mavx

###### Scalar Sorting tests #####
check_scalarsort_SOURCES = check_scalarsort.c testutil.c \
    $(top_srcdir)/src/types.h \
	$(top_srcdir)/src/scalarsort/scalarsort.h \
	$(top_srcdir)/src/scalarsort/scalarsort.c
check_scalarsort_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/ -I$(top_srcdir)/src/scalarsort/ -I. @CHECK_CFLAGS@
check_scalarsort_LDADD = @CHECK_LIBS@
check_scalarsort_LDFLAGS =

###### AVX Sorting tests #####
check_avxsort_SOURCES = check_avxsort.c testutil.c \
    $(top_srcdir)/src/types.h \
	$(top_srcdir)/src/avxsort/avxcommon.h \
	$(top_srcdir)/src/avxsort/avxsort_core.h \
	$(top_srcdir)/src/avxsort/avxsort.h \
	$(top_srcdir)/src/avxsort/avxsort.c \
	$(top_srcdir)/src/merge/merge.h \
	$(top_srcdir)/src/merge/merge.c \
	$(top_srcdir)/src/merge/avx_multiwaymerge.h \
	$(top_srcdir)/src/merge/avx_multiwaymerge.c \
	$(top_srcdir)/src/avxsort/avxsort_multiway.h \
	$(top_srcdir)/src/avxsort/avxsort_multiway.c
check_avxsort_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/ -I$(top_srcdir)/src/merge/ -I$(top_srcdir)/src/avxsort/ -I. @CHECK_CFLAGS@
check_avxsort_LDADD = @CHECK_LIBS@
check_avxsort_LDFLAGS =

###### Partitioning tests #####
check_partitioning_SOURCES = check_partitioning.c testutil.c \
    $(top_srcdir)/src/types.h \
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
	$(top_srcdir)/src/partition/partition.c
check_partitioning_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/ -I$(top_srcdir)/src/partition/ -I$(top_srcdir)/src/util/ -I$(top_srcdir)/src/datagen/ -I. @CHECK_CFLAGS@
check_partitioning_LDADD = @CHECK_LIBS@
check_partitioning_LDFLAGS =

###### Merging tests (Scalar + AVX) #####
check_merge_SOURCES = check_merge.c testutil.c \
    $(top_srcdir)/src/types.h \
	$(top_srcdir)/src/avxsort/avxcommon.h \
	$(top_srcdir)/src/avxsort/avxsort_core.h \
	$(top_srcdir)/src/merge/merge.h \
	$(top_srcdir)/src/merge/merge.c \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.h \
	$(top_srcdir)/src/merge/scalar_multiwaymerge.c \
	$(top_srcdir)/src/merge/avx_multiwaymerge.h \
	$(top_srcdir)/src/merge/avx_multiwaymerge.c
check_merge_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/src/ -I$(top_srcdir)/src/avxsort/ -I$(top_srcdir)/src/merge/ -I. @CHECK_CFLAGS@
check_merge_LDADD = @CHECK_LIBS@
check_merge_LDFLAGS =