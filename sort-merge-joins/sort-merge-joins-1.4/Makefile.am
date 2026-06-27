AUTOMAKE_OPTIONS = foreign

LIBDIR = 

ACLOCAL_AMFLAGS = -Ilib/check-0.9.12/

if PERF_COUNTERS
LIBDIR += lib/intel-pcm-2.35
endif

SUBDIRS = $(LIBDIR) src . tests

EXTRA_DIST = lib        \
	     doc/Doxyfile  	\
	     README