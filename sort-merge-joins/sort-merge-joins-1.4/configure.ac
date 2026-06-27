#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# AC_PREREQ([2.68])
AC_INIT([sort-merge-joins], [1.4], [cagri.balkesen@inf.ethz.ch])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([foreign])

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

# Checks for header files.
# AC_CHECK_HEADERS (header-file..., [action-if-found], [action-if-not-found], [includes])
AC_CHECK_HEADERS([stdint.h stdlib.h string.h sys/time.h unistd.h], [], [], [])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_C_RESTRICT
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for libraries.
AC_CHECK_LIB([m], [pow], [],[
         echo "math library is required for this program"
         exit -1])

AC_CHECK_LIB([pthread], [pthread_mutex_init], ,
             AC_MSG_ERROR(pthread library not found))

AC_CHECK_LIB([numa], [numa_num_configured_nodes], ,
             AC_MSG_WARN(libNUMA not found will use cpu-mapping.txt or round-robin))
             
# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gettimeofday memset pow])
AC_CHECK_FUNCS([pthread_attr_setaffinity_np pthread_barrier_wait])

# Check for conditional compilation options
# Use 8B keys?
AC_ARG_ENABLE(key8B,
  [  --enable-key8B  use 8B keys and values making tuples 16B  [default=no]],
  [enable_key8B="$enableval"], 
  [enable_key8B="no"])

AM_CONDITIONAL([KEY_8B], [test "$enable_key8B" = "yes"])

# Do performance counter monitoring?
AC_ARG_ENABLE(perfcounters,
   [  --enable-perfcounters  enable performance counter monitoring with Intel PCM  [default=no]],
   [enable_perfcounters="$enableval" CC="g++"], 
   [enable_perfcounters="no"])

AM_CONDITIONAL([PERF_COUNTERS], [test "$enable_perfcounters" = "yes"])

# Output debug messages?
AC_ARG_ENABLE(debug,
   [  --enable-debug  enable debug messages on commandline  [default=no]],
   [enable_debug="$enableval"], 
   [enable_debug="no"])

AM_CONDITIONAL([DEBUG], [test "$enable_debug" = "yes"])

# Disable timing?
AC_ARG_ENABLE(timing,
   [  --enable-timing  enable execution timing  [default=yes]],
   [enable_timing="$enableval"], 
   [enable_timing="yes"])

AM_CONDITIONAL([NOTIMING], [test "$enable_timing" = "no"])

# Materialize join results ?
AC_ARG_ENABLE(materialize,
  [  --enable-materialize  Materialize join results ?  [default=no]],
  [enable_materialize="$enableval"], 
  [enable_materialize="no"])

AM_CONDITIONAL([JOIN_MATERIALIZE], [test "$enable_materialize" = "yes"])

# Persist input tables and join result table ?
AC_ARG_ENABLE(persist,
  [  --enable-persist  Persist input tables and join result table ?  [default=no]],
  [enable_persist="$enableval"], 
  [enable_persist="no"])

AM_CONDITIONAL([PERSIST_RELATIONS], [test "$enable_persist" = "yes"])

AM_PATH_CHECK([0.9.12],[],[AC_MSG_WARN([check is missing, unit tests with 'make check' will not compile and run.])])
#PKG_CHECK_MODULES([CHECK], [check >= 0.9.12])
               
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
